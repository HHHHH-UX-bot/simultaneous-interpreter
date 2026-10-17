"""Build the ARM cloud application (output/cloud_app/cloud_app.elf).

Same freestanding flags as scripts/run_network_ui.py plus the cloud modules
(cloud_events/cloud_wire/cloud_live/cloud_client/cloud_frame, bundled wslay
frame codec). The byte-for-byte ELF used for the 2026-10-17 board session is
recorded in output/cloud_app/build.json; the API key is never compiled in (it
is written to the .cloud_config OCM window by the host).

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
    "-DZ7_NETWORK_CAPABILITIES",
    "-I", str(ROOT / "software/subtitle_ui"),
    "-I", str(ROOT / "output/display"),
    "-I", str(ROOT / "third_party/wslay/lib/includes"),
    "-T", str(ROOT / "software/network_ui/link.ld"),
    "-Wl,-Map=" + str(OUT / "cloud_app.map"),
]

SOURCES = [
    "software/ps_bringup/start.S",
    "software/network_ui/main.c",
    "software/network_ui/network.c",
    "software/network_ui/protocol.c",
    "software/network_ui/cloud_events.c",
    "software/network_ui/cloud_wire.c",
    "software/network_ui/cloud_live.c",
    "software/network_ui/cloud_client.c",
    "software/network_ui/cloud_frame.c",
    "software/network_ui/stream_pilot.c",
    "software/ps_wifi/wifi_at_probe.c",
    "software/subtitle_ui/ui_model.c",
    "software/subtitle_ui/ui_render.c",
    "software/subtitle_ui/ui_demo.c",
    "software/subtitle_ui/runtime.c",
    "output/subtitle_ui/ui_assets.c",
]

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
    cmd = [str(gcc), *FLAGS, *[str(ROOT / s) for s in SOURCES], "-lgcc",
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
