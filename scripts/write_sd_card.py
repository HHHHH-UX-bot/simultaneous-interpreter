"""Write the SD-boot files to a FAT32 TF card root and read them back.

BOOT.BIN comes from scripts/build_sd_boot.py; qwen.tok and qwen.tqw are the
firmware-pinned Qwen assets (CARD_FILES) that the firmware loads itself after
startup. z7cfg.bin is left alone (scripts/make_sd_config.py writes it).
A card without a drive letter can be given as its volume path \\\\?\\Volume{...}\\.
Run without -I: it imports build_sd_boot from this directory.
"""
import argparse, json, os, shutil
from build_sd_boot import CARD_FILES, ROOT, sha


def copy(source, target):
    with open(source, "rb") as fin, open(target, "wb") as fout:
        shutil.copyfileobj(fin, fout, 1 << 20)
        fout.flush()
        os.fsync(fout.fileno())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("card", help=r"card root, e.g. E:\ or \\?\Volume{...}\ ")
    p.add_argument("--boot-only", action="store_true", help="replace only BOOT.BIN and keep the model files")
    a = p.parse_args()
    out = ROOT / "output/z7_nano/sd_boot"
    record = json.loads((out / "sd_boot.json").read_text(encoding="utf-8"))
    files = {"BOOT.BIN": (out / "BOOT.BIN", record["boot_bin_sha256"])}
    if sha(out / "BOOT.BIN") != record["boot_bin_sha256"]:
        raise RuntimeError("BOOT.BIN differs from sd_boot.json; rerun build_sd_boot.py")
    if not a.boot_only:
        for name, (path, _, digest) in CARD_FILES.items():
            if sha(ROOT / path) != digest:
                raise RuntimeError("Firmware-pinned asset required: " + path)
            files[name] = (ROOT / path, digest)
    card = a.card.strip()
    card = card if card.endswith(("\\", "/")) else card + "\\"
    for name, (source, digest) in files.items():
        copy(source, card + name)
        if sha(card + name) != digest:
            raise RuntimeError("Read-back mismatch: " + name)
        print(name, digest)
    print("PASS_SD_CARD")


if __name__ == "__main__":
    main()
