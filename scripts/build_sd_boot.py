"""Assemble the SD-card BOOT.BIN for the Z7-Nano interpreter.

Partitions: FSBL, the verified PL bitstream, the relocation stub, payload.bin
(CPU0 + CPU1 ELF segments, placed by the stub as xsdb dow would), then the
pinned Qwen weights and tokenizer at the firmware's fixed DDR windows. The
firmware rehashes both assets itself. No credentials go into BOOT.BIN;
z7cfg.bin (scripts/make_sd_config.py) is a separate file on the card.

Prerequisites: scripts/build_sd_boot_platform.tcl and build_sd_boot_fsbl.py
(output/z7_nano/sd_boot/fsbl.elf), an application built with --sd-boot, the
matching PL platform and CPU1 host build.
"""
import argparse, hashlib, json, struct, subprocess, zlib
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path("D:/AMDDesignTools/2026.1/Vitis")
GCC = TOOLS / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe"
MODEL = ("models/qwen2.5-0.5b-instruct-g64-q6.tqw", 0x04000000, "e7ebb0a8435ac90173ef9cefa753adc7fe2f6a5fb8fa86757a98f1344dcf4863")
TOKENIZER = ("models/qwen2_0.5b.tokenizer.bin", 0x1C000000, "b6aa0fa09ec4913e8892cc1f25c0c76f24a72dbd3074131782bea58b6a944aea")
STUB_ADDRESS, PAYLOAD_ADDRESS, PAYLOAD_LIMIT = 0x1F800000, 0x1F900000, 0x00600000
PAYLOAD_MAGIC = 0x4C50375A


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_segments(path):
    """PT_LOAD segments of a 32-bit little-endian ARM ELF: (vaddr, data, memsz)."""
    b = Path(path).read_bytes()
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
        raise RuntimeError("Not a 32-bit little-endian ELF: " + str(path))
    entry, phoff = struct.unpack_from("<II", b, 24)[0], struct.unpack_from("<I", b, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", b, 42)
    segments = []
    for i in range(phnum):
        kind, off, vaddr, _, filesz, memsz = struct.unpack_from("<6I", b, phoff + i * phentsize)
        if kind == 1 and memsz:
            segments.append((vaddr, b[off:off + filesz], memsz))
    return entry, segments


def build_stub(out):
    elf = out / "stub.elf"
    src = ROOT / "software/sd_boot"
    subprocess.run([str(GCC), "-mcpu=cortex-a9", "-marm", "-O2", "-ffreestanding", "-fno-builtin",
                    "-fno-tree-loop-distribute-patterns", "-nostdlib", "-Wall", "-Wextra", "-Werror",
                    "-Wl,--build-id=none", "-T", str(src / "stub.ld"), str(src / "stub.S"), str(src / "stub.c"),
                    "-o", str(elf)], check=True)
    entry, segments = load_segments(elf)
    if entry != STUB_ADDRESS or any(v < STUB_ADDRESS or v + m > PAYLOAD_ADDRESS for v, _, m in segments):
        raise RuntimeError("Stub must lie inside its DDR window")
    return elf


def build_payload(out, app_elf, host_elf):
    cpu0_entry, cpu0 = load_segments(app_elf)
    cpu1_entry, cpu1 = load_segments(host_elf)
    segments = sorted(cpu0 + cpu1, key=lambda s: s[0])
    for (a, _, n), (b, _, _) in zip(segments, segments[1:]):
        if a + n > b:
            raise RuntimeError("CPU0/CPU1 segments overlap at 0x%08x" % b)
    for v, _, m in segments:
        if v < 0x2F000 + 0x1000 and v + m > 0x2F000 or v + m > STUB_ADDRESS:
            raise RuntimeError("Segment 0x%08x collides with the SD boot handshake or stub" % v)
    head = 32
    offset = head + 16 * len(segments)
    records, blobs = [], []
    for vaddr, data, memsz in segments:
        offset = (offset + 15) & ~15
        records.append(struct.pack("<4I", vaddr, len(data), memsz, offset))
        blobs.append((offset, data))
        offset += len(data)
    body = bytearray(offset - head)
    body[:16 * len(records)] = b"".join(records)
    for off, data in blobs:
        body[off - head:off - head + len(data)] = data
    if head + len(body) > PAYLOAD_LIMIT:
        raise RuntimeError("Payload exceeds its DDR window")
    header = struct.pack("<8I", PAYLOAD_MAGIC, 1, len(records), cpu0_entry, cpu1_entry, len(body),
                         zlib.crc32(bytes(body)) & 0xFFFFFFFF, 0)
    path = out / "payload.bin"
    path.write_bytes(header + bytes(body))
    return path, dict(cpu0_entry=cpu0_entry, cpu1_entry=cpu1_entry, records=len(records),
                      zero_fill_bytes=sum(m - len(d) for _, d, m in segments), bytes=head + len(body))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app-dir", type=Path, default=ROOT / "output/network_history/microphone")
    p.add_argument("--out", type=Path, default=ROOT / "output/z7_nano/sd_boot")
    a = p.parse_args()
    app, out = a.app_dir.resolve(), a.out.resolve()
    build = json.loads((app / "build.json").read_text(encoding="utf-8"))
    elf = app / Path(build["ELF"]).name
    if build.get("Status") != "passed" or not build.get("SDBoot") or sha(elf) != build["ELFSHA256"]:
        raise RuntimeError("A passed --sd-boot application build is required")
    if any(sha(ROOT / f) != h for f, h in build["InputSHA256"].items()):
        raise RuntimeError("Application sources changed; rebuild first")
    platform = ROOT / "output/z7_nano/remote_display"
    pl = json.loads((platform / "build.json").read_text(encoding="utf-8"))
    bit = platform / "z7_nano_audio_top.bit"
    if pl.get("Status") != "passed" or sha(bit) != pl["BitstreamSHA256"] or bool(pl.get("SpeakerCNN")) != bool(build.get("SpeakerCNN")):
        raise RuntimeError("Matching verified PL bitstream required")
    host = ROOT / "output/z7_nano/usb"
    cpu1 = json.loads((host / "host_build.json").read_text(encoding="utf-8"))
    if cpu1.get("status") != "passed" or sha(host / "host.elf") != cpu1["elf_sha256"]:
        raise RuntimeError("Matching verified CPU1 touch host required")
    fsbl = out / "fsbl.elf"
    for name, _, digest in (MODEL, TOKENIZER):
        if sha(ROOT / name) != digest:
            raise RuntimeError("Firmware-pinned asset required: " + name)
    stub = build_stub(out)
    payload, layout = build_payload(out, elf, host / "host.elf")
    bif = out / "boot.bif"
    bif.write_text("the_ROM_image:\n{\n"
                   f"\t[bootloader] {fsbl.as_posix()}\n"
                   f"\t{bit.as_posix()}\n"
                   f"\t{stub.as_posix()}\n"
                   f"\t[load=0x{PAYLOAD_ADDRESS:08x}] {payload.as_posix()}\n"
                   f"\t[load=0x{MODEL[1]:08x}] {(ROOT / MODEL[0]).as_posix()}\n"
                   f"\t[load=0x{TOKENIZER[1]:08x}] {(ROOT / TOKENIZER[0]).as_posix()}\n"
                   "}\n", encoding="ascii")
    image = out / "BOOT.BIN"
    subprocess.run([str(TOOLS / "bin/bootgen.bat"), "-arch", "zynq", "-image", str(bif), "-o", str(image), "-w", "on"],
                   check=True, cwd=out)
    record = dict(status="built", built_at_utc=datetime.now(timezone.utc).isoformat(), boot_bin_sha256=sha(image),
                  boot_bin_bytes=image.stat().st_size, fsbl_sha256=sha(fsbl), bitstream_sha256=sha(bit),
                  stub_sha256=sha(stub), payload_sha256=sha(payload), app_elf_sha256=build["ELFSHA256"],
                  cpu1_elf_sha256=cpu1["elf_sha256"], model_sha256=MODEL[2], tokenizer_sha256=TOKENIZER[2], layout=layout,
                  scope="built only; not a boot or board result")
    (out / "sd_boot.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: record[k] for k in ("boot_bin_bytes", "layout")}))
    print("PASS_SD_BOOT_IMAGE")


if __name__ == "__main__":
    main()
