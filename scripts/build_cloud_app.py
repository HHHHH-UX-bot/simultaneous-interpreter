"""Build the ARM cloud application (output/cloud_app/cloud_app.elf).

Same freestanding flags as scripts/run_network_ui.py plus the cloud modules
(cloud_events/cloud_wire/cloud_live/cloud_client/cloud_frame, bundled wslay
frame codec), the MMU/cache boot path and the fixed-point libopus encoder. The
byte-for-byte ELF used for a board session is recorded in
output/cloud_app/build.json; the API key is never compiled in (it is written to
the .cloud_config OCM window by the host).

Usage:
  python -B scripts/build_cloud_app.py [--gcc <arm-none-eabi-gcc>]
"""
import argparse
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/cloud_app"
DEFAULT_GCC = Path(r"C:\Xilinx\SDK\2018.3\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe")

FLAGS = [
    "-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access",
    "-O2", "-g", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-strict-aliasing", "-fno-short-enums", "-Wall", "-Wextra", "-Werror",
    "-std=c11", "-nostdlib", "-Wl,--build-id=none", "-Wl,--print-memory-usage",
    "-DZ7_NETWORK_CAPABILITIES", "-DZ7_MMU_CACHE", "-DZ7_OPUS",
    "-I", str(ROOT / "software/subtitle_ui"),
    "-I", str(ROOT / "output/display"),
    "-I", str(ROOT / "third_party/wslay/lib/includes"),
    "-I", str(ROOT / "third_party/opus/include"),
    "-T", str(ROOT / "software/network_ui/link.ld"),
    "-Wl,-Map=" + str(OUT / "cloud_app.map"),
]

SOURCES = [
    "software/ps_bringup/start.S",
    "software/ps_bringup/cache.c",
    "software/network_ui/main.c",
    "software/network_ui/network.c",
    "software/network_ui/protocol.c",
    "software/network_ui/cloud_events.c",
    "software/network_ui/cloud_wire.c",
    "software/network_ui/cloud_live.c",
    "software/network_ui/cloud_client.c",
    "software/network_ui/cloud_frame.c",
    "software/network_ui/stream_pilot.c",
    "software/network_ui/opus_codec.c",
    "software/network_ui/ogg_opus.c",
    "software/ps_wifi/wifi_at_probe.c",
    "software/subtitle_ui/ui_model.c",
    "software/subtitle_ui/ui_render.c",
    "software/subtitle_ui/ui_demo.c",
    "software/subtitle_ui/runtime.c",
    "output/subtitle_ui/ui_assets.c",
]

OPUS_DIR = ROOT / "third_party/opus"
OPUS_CFLAGS = [
    "-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access",
    "-Os", "-g", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-strict-aliasing", "-fno-short-enums", "-std=c11",
    "-DFIXED_POINT", "-DOPUS_BUILD", "-DDISABLE_FLOAT_API=1", "-DVAR_ARRAYS=1",
    "-include", str(OPUS_DIR / "config/opus_alloc_override.h"),
    "-I", str(OPUS_DIR / "config"),
    "-I", str(OPUS_DIR / "include"),
    "-I", str(OPUS_DIR / "celt"),
    "-I", str(OPUS_DIR / "silk"),
    "-I", str(OPUS_DIR / "silk/fixed"),
    "-I", str(OPUS_DIR / "src"),
    "-I", str(OPUS_DIR),
]
OPUS_EXCLUDE = ("/x86/", "/arm/", "/neon/", "/ne10/", "/float/", "/mips/", "/tests/", "dump_modes")
OPUS_SKIP = ("src/analysis.c", "src/mlp.c", "src/mlp_data.c", "celt/opus_custom_demo.c",
             "src/opus_demo.c", "src/opus_compare.c", "src/repacketizer_demo.c")


def opus_sources():
    files = []
    for sub in ("celt", "silk", "src"):
        for path in sorted((OPUS_DIR / sub).rglob("*.c")):
            rel = path.relative_to(OPUS_DIR).as_posix()
            if any(token in "/" + rel for token in OPUS_EXCLUDE):
                continue
            if rel in OPUS_SKIP:
                continue
            files.append(path)
    return files


# Mailboxes and windows pinned by the host tooling and the PS-side ABI.
SYMBOLS = [
    ("_start", 0x10000),
    ("report", 0x1E000),
    ("ui_info", 0x1E080),
    ("ui_command", 0x1E0C0),
    ("wifi_info", 0x1E100),
    ("network_report", 0x1E140),
    ("network_config", 0x1E180),
    ("cloud_client_config", 0x1E200),
    ("snapshots", 0x1B00000),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", type=Path, default=DEFAULT_GCC)
    args = parser.parse_args()
    gcc = args.gcc
    nm = gcc.with_name("arm-none-eabi-nm.exe")
    if not gcc.is_file() or not nm.is_file():
        raise SystemExit("ARM GCC not found: " + str(gcc))
    OUT.mkdir(parents=True, exist_ok=True)
    opus_objects = OUT / "opus_obj"
    opus_objects.mkdir(exist_ok=True)
    for stale in opus_objects.glob("*.o"):
        stale.unlink()
    opus_log = []
    for source in opus_sources():
        obj = opus_objects / (source.relative_to(OPUS_DIR).as_posix().replace("/", "_") + ".o")
        compile_cmd = [str(gcc), *OPUS_CFLAGS, "-c", str(source), "-o", str(obj)]
        proc = subprocess.run(compile_cmd, cwd=str(ROOT), stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        output = proc.stdout.decode("utf-8", "replace")
        if output:
            opus_log.append(source.name + ":\n" + output)
        if proc.returncode != 0:
            (OUT / "build.log").write_text("\n".join(opus_log), encoding="utf-8")
            print("\n".join(opus_log))
            raise SystemExit("opus build failed: " + str(source))
    opus_list = sorted(str(p) for p in opus_objects.glob("*.o"))
    cmd = [str(gcc), *FLAGS, *[str(ROOT / s) for s in SOURCES], *opus_list, "-lgcc",
           "-o", str(OUT / "cloud_app.elf")]
    proc = subprocess.run(cmd, cwd=str(ROOT), stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    text = proc.stdout.decode("utf-8", "replace")
    (OUT / "build.log").write_text(" ".join(cmd) + "\n\n" + text, encoding="utf-8")
    print(text)
    if proc.returncode != 0:
        raise SystemExit("cloud_app build failed")
    symbols = subprocess.run([str(nm), "-n", str(OUT / "cloud_app.elf")],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    table = symbols.stdout.decode("utf-8", "replace")
    (OUT / "symbols.log").write_text(table, encoding="utf-8")
    for name, address in SYMBOLS:
        m = re.search(r"^([0-9a-fA-F]+)\s+\w\s+" + re.escape(name) + r"\r?$", table, re.M)
        if not m or int(m.group(1), 16) != address:
            raise SystemExit("layout mismatch: " + name)
    record = {
        "Status": "passed",
        "BuiltAtUTC": datetime.now(timezone.utc).isoformat(),
        "Compiler": str(gcc),
        "Flags": FLAGS,
        "Sources": SOURCES,
        "ELF": "output/cloud_app/cloud_app.elf",
    }
    (OUT / "build.json").write_text(
        json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("PASS_CLOUD_APP_BUILD")


if __name__ == "__main__":
    main()
