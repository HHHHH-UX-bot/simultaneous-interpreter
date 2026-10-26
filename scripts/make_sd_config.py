"""Write z7cfg.bin, the runtime configuration an SD-booted board applies itself.

Prompts privately (as configure_cloud_runtime.py does) and writes the file
straight to the given card path. Nothing secret is printed or kept elsewhere.
The file holds the Wi-Fi password and Bailian key in plain bytes: anyone with
the card can read them. The TLS seed in it is mixed with per-boot microphone
noise by the firmware; the time stamp is the board's clock baseline.
Usage: python scripts/make_sd_config.py <card_root_or_file>
"""
import getpass
import secrets
import struct
import sys
import time
import zlib
from pathlib import Path

from configure_cloud_runtime import DEFAULT_PATH, MAGIC_CLOUD, network_words, words_of
import re

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x4643375A  # "Z7CF", software/sd_boot/sd_boot.h


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    target = Path(sys.argv[1])
    if target.is_dir() or str(target).endswith(("\\", "/")):
        target = target / "z7cfg.bin"
    ssid = input("Wi-Fi SSID: ").strip().encode()
    password = getpass.getpass("Wi-Fi password: ").encode()
    key = getpass.getpass("Bailian API key: ").strip().encode()
    host = re.search(r'#define CLOUD_HOST "(ws-[^.]+)\.', (ROOT / "software/network_ui/network.c").read_text(encoding="utf-8"))
    workspace_default = host.group(1) if host else ""
    workspace = (input(f"Bailian workspace id [{workspace_default}]: ").strip() or workspace_default).encode()
    path = (input(f"Model path [{DEFAULT_PATH}]: ").strip() or DEFAULT_PATH).encode()
    if not 1 <= len(ssid) <= 32:
        raise SystemExit("SSID must be 1..32 bytes")
    if not 8 <= len(password) <= 64:
        raise SystemExit("Wi-Fi password must be 8..64 bytes")
    if not workspace.startswith(b"ws-") or len(workspace) >= 48:
        raise SystemExit("workspace must start with ws- (just press Enter for the default)")
    if not path.startswith(b"/api-ws/") or len(path) >= 96:
        raise SystemExit("model path must be the /api-ws/... realtime path (just press Enter for the default)")
    if not key or len(key) >= 192:
        raise SystemExit("API key required")
    net = network_words(0, ssid, password)
    cloud = [MAGIC_CLOUD] + words_of(path, 96) + words_of(workspace, 48) + words_of(key, 192)
    body = struct.pack("<32I", *net) + struct.pack("<85I", *cloud) + struct.pack("<I", int(time.time())) + secrets.token_bytes(64)
    blob = struct.pack("<4I", MAGIC, 1, 16 + len(body), zlib.crc32(body) & 0xFFFFFFFF) + body
    target.write_bytes(blob)
    print(f"Wrote {target.name}: {len(blob)} bytes (SSID {len(ssid)} B, password {len(password)} B, key {len(key)} B)")


if __name__ == "__main__":
    main()
