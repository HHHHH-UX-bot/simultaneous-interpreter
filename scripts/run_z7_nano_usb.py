"""Stage the bare-metal USB port while keeping the accepted UI on CPU0."""
import argparse
import json
import shutil
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
import run_z7_nano as base
import run_z7_nano_display as display

ROOT = base.ROOT
OUT = base.OUT/"usb"


def inputs():
    paths = ["hardware/z7_nano/source.json", "scripts/prepare_z7_nano_usb.py",
             "scripts/generate_z7_nano_usb_config.tcl", "scripts/run_z7_nano_usb.py",
             "scripts/z7_nano_usb_probe.tcl", "scripts/z7_nano_usb_stop.tcl", "software/z7_nano_usb/probe.c",
             "software/z7_nano_usb/link.ld", "software/ps_bringup/start.S",
             "output/z7_nano/usb/usb_hw.h", "output/z7_nano/usb/config/ps7_init.tcl"]
    return display.hashes(paths)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("build-probe", "run-probe"))
    parser.add_argument("--platform", choices=("display", "wifi", "network"), default="display")
    parser.add_argument("--gcc", type=Path, default=base.GCC)
    parser.add_argument("--vivado", type=Path, default=base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=base.XSDB)
    args = parser.parse_args()
    if args.platform == "wifi":
        from run_z7_nano_wifi import configure
        configure()
    elif args.platform == "network":
        from run_z7_nano_network import configure
        configure()
    base.run([sys.executable, "-B", ROOT/"scripts/prepare_z7_nano_usb.py"],
             base.TMP/"usb_prepare", "PASS_Z7_NANO_USB_PREPARE", 30)
    if args.action == "build-probe":
        before = inputs()
        display.save(OUT/"probe_build.json", {"status": "building", "input_hashes": before})
        flags = ["-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-O2", "-g", "-ffreestanding",
                 "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror", "-nostdlib",
                 "-Wl,--build-id=none", "-T", ROOT/"software/z7_nano_usb/link.ld", "-I", OUT,
                 "-Wl,-Map="+str(OUT/"probe.map")]
        base.run([args.gcc, *flags, ROOT/"software/ps_bringup/start.S", ROOT/"software/z7_nano_usb/probe.c",
                  "-lgcc", "-o", OUT/"probe.elf"], base.TMP/"usb_probe_build", "", 60)
        data = (OUT/"probe.elf").read_bytes()
        header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if data[:6] != b"\x7fELF\x01\x01" or header[0] != 2 or header[1] != 40 or header[3] != 0x1000:
            raise RuntimeError("Unexpected Core1 ARM ELF entry")
        for index in range(header[9]):
            kind, offset, virtual, physical, file_bytes, memory_bytes, flags, alignment = struct.unpack_from(
                "<IIIIIIII", data, header[4]+index*header[8])
            if kind == 1 and (physical != virtual or file_bytes > memory_bytes or offset+file_bytes > len(data) or
                             physical < 0x1000 or physical+memory_bytes > 0x10000):
                raise RuntimeError("USB probe overlaps display code or lies outside Core1 OCM")
        if before != inputs():
            raise RuntimeError("USB probe inputs changed during compilation")
        display.save(OUT/"probe_build.json", {"status": "passed", "input_hashes": before,
                     "elf_sha256": base.sha(OUT/"probe.elf")})
    else:
        checked = json.loads((OUT/"probe_build.json").read_text(encoding="utf-8"))
        if checked["status"] != "passed" or checked["input_hashes"] != inputs() or base.sha(OUT/"probe.elf") != checked["elf_sha256"]:
            raise RuntimeError("Current USB probe build required")
        app = display.checked_app()
        active = json.loads((display.OUT/"active_app.json").read_text(encoding="utf-8"))
        pixels = json.loads((display.OUT/"capture.json").read_text(encoding="utf-8"))
        # TOUCH USB is now the screen's only power source. PHY power must precede
        # physical screen acceptance; keep the current digital frame/ELF gate.
        if active["status"] != "ready" or pixels["status"] != "passed" or \
           any(record["elf_sha256"] != app["elf_sha256"] for record in (active, pixels)) or \
           pixels["program_attempt"] != active["program_attempt"]:
            raise RuntimeError("Running new-board UI with current exact captured pixels required")
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        folder = OUT/"attempts"/stamp
        folder.mkdir(parents=True)
        shutil.copyfile(OUT/"probe.elf", folder/"probe.elf")
        if base.sha(folder/"probe.elf") != checked["elf_sha256"]:
            raise RuntimeError("USB probe ELF snapshot changed")
        with base.hardware_lock():
            live = display.probe(args, folder)
            if not live["done"] or live["usr_access"] != display.PLATFORM_ID:
                raise RuntimeError("Live migrated display platform required")
            base.run([args.xsdb, ROOT/"scripts/z7_nano_usb_probe.tcl", live["cable_serial"],
                      folder/"probe.elf", folder/"result.json"], folder/"probe_run", "PASS_Z7_NANO_USB_PROBE", 45)
        result = json.loads((folder/"result.json").read_text(encoding="utf-8"))
        result.update(attempt=folder.relative_to(ROOT).as_posix(), elf_sha256=checked["elf_sha256"],
                      input_hashes=checked["input_hashes"], display_program_attempt=active["program_attempt"])
        display.save(OUT/"probe_run.json", result)
    print("PASS_Z7_NANO_USB_"+args.action.upper().replace("-", "_"))


if __name__ == "__main__":
    main()
