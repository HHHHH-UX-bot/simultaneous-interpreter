"""Independent Z7-Nano stage runner; old-board runners and outputs remain intact."""
import argparse
import hashlib
import json
import msvcrt
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/z7_nano"
TMP = ROOT / "tmp/z7_nano"
VIVADO = Path("D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat")
XSDB = Path("D:/AMDDesignTools/2026.1/Vitis/bin/xsdb.bat")
GCC = Path("D:/AMDDesignTools/2026.1/Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe")
RTL = ["z7_nano_static_top.v", "static_ui_top.v", "static_ui_video.v", "hdmi_clock.v",
       "async_rst_sync.v", "dvi_encoder.v", "dvi_transmitter_top.v", "serializer_10_to_1.v"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = ["hardware/z7_nano/source.json", "scripts/prepare_z7_nano.py", "scripts/build_z7_nano_static.tcl"]
    paths += ["rtl/"+name for name in RTL]
    paths += ["output/static_ui/"+name for name in
              ["static_ui_config.vh", "static_ui_tilemap.mem", "static_ui_tiles.mem", "static_ui_palette.mem"]]
    paths += ["output/z7_nano/config/board.tcl", "output/z7_nano/config/hdmi.xdc"]
    return {name: sha(ROOT/name) for name in paths}


def ddr_inputs():
    paths = ["hardware/z7_nano/source.json", "hardware/z7_nano/vendor_ps7_init.tcl",
             "software/ps_bringup/start.S", "software/ps_bringup/main.c", "software/ps_bringup/link.ld",
             "scripts/z7_nano_ps_test.tcl", "scripts/z7_nano_halt.tcl"]
    return {name: sha(ROOT/name) for name in paths}


def run(command, folder, marker, timeout):
    folder.mkdir(parents=True, exist_ok=True)
    with (folder/"console.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(x) for x in command], cwd=folder, stdout=log,
                                   stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            if process.poll() is None:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
                process.wait(timeout=10)
            raise
    text = (folder/"console.log").read_text(encoding="utf-8", errors="replace")
    print("\n".join(text.splitlines()[-10:]), flush=True)
    if code or marker not in text:
        raise RuntimeError(f"Stage failed; see {folder/'console.log'}")


@contextmanager
def hardware_lock():
    path = ROOT/"tmp/ps/operation.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as handle:
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        yield


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["build-static", "probe", "program-static", "build-ddr", "test-ddr"])
    parser.add_argument("--vivado", type=Path, default=VIVADO)
    parser.add_argument("--xsdb", type=Path, default=XSDB)
    parser.add_argument("--gcc", type=Path, default=GCC)
    args = parser.parse_args()
    TMP.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-B", ROOT/"scripts/prepare_z7_nano.py"], check=True, cwd=ROOT)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if args.action == "build-static":
        record = {"status": "building", "started_utc": stamp, "input_hashes": inputs()}
        (OUT/"static").mkdir(parents=True, exist_ok=True)
        (OUT/"static/build.json").write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")
        run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
             ROOT/"scripts/build_z7_nano_static.tcl"], TMP/"static", "PASS_Z7_NANO_STATIC_BUILD", 1200)
        if inputs() != record["input_hashes"]:
            raise RuntimeError("Build inputs changed during compilation")
        record.update(status="passed", bitstream_sha256=sha(OUT/"static/z7_nano_static_top.bit"))
        (OUT/"static/build.json").write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")
    elif args.action == "build-ddr":
        build_ddr(args)
    else:
        with hardware_lock():
            hardware(args, stamp)
    print("PASS_Z7_NANO_"+args.action.upper().replace("-", "_"), flush=True)


def hardware(args, stamp):
    folder = OUT/"hardware"/stamp
    if args.action == "test-ddr":
        test_ddr(args, folder)
        return
    if args.action == "program-static":
        build = json.loads((OUT/"static/build.json").read_text(encoding="utf-8"))
        bit = OUT/"static/z7_nano_static_top.bit"
        if build["status"] != "passed" or build["input_hashes"] != inputs() or sha(bit) != build["bitstream_sha256"]:
            raise RuntimeError("Current passing build required before programming")
        folder.mkdir(parents=True, exist_ok=True)
        snapshot = folder/bit.name
        shutil.copyfile(bit, snapshot)
        if sha(snapshot) != build["bitstream_sha256"]:
            raise RuntimeError("Bitstream snapshot changed")
        run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
             ROOT/"scripts/hardware_z7_nano.tcl", "-tclargs", "probe", folder/"before_program.json"],
            folder/"probe", "PASS_Z7_NANO_HARDWARE", 90)
        live = json.loads((folder/"before_program.json").read_text(encoding="utf-8"))
        if live["crc_error"] or (live["done"] and not live["eos"]):
            raise RuntimeError("Live FPGA configuration is not healthy")
        serial = live["cable_serial"]
        if live["done"] and live["usr_access"] == 0x5a374e32:
            run([args.xsdb, ROOT/"scripts/z7_nano_display.tcl", "quiesce", serial, "", "", ""],
                folder/"quiesce", "PASS_Z7_NANO_DISPLAY_QUIESCE", 45)
        elif not live["done"] or live["usr_access"] == 0x5a374e31:
            run([args.xsdb, ROOT/"scripts/z7_nano_halt.tcl", serial], TMP/"halt", "PASS_Z7_NANO_HALT", 45)
        else:
            raise RuntimeError("Unrecognized live PL; inspect before restoring static display")
        extra = [snapshot]
    else:
        extra = []
    run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
         ROOT/"scripts/hardware_z7_nano.tcl", "-tclargs", args.action, folder/"result.json", *extra],
        folder, "PASS_Z7_NANO_HARDWARE", 120)
    if args.action == "program-static":
        record = json.loads((folder/"result.json").read_text(encoding="utf-8"))
        record["bitstream_sha256"] = sha(snapshot)
        record["attempt"] = folder.relative_to(ROOT).as_posix()
        (OUT/"program.json").write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")


def build_ddr(args):
    folder = OUT/"ddr"
    folder.mkdir(parents=True, exist_ok=True)
    before = ddr_inputs()
    flags = ["-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access", "-O2", "-g",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
             "-nostdlib", "-Wl,--build-id=none", "-T", ROOT/"software/ps_bringup/link.ld",
             "-Wl,-Map="+str(folder/"test.map")]
    run([args.gcc, *flags, ROOT/"software/ps_bringup/start.S", ROOT/"software/ps_bringup/main.c",
         "-o", folder/"test.elf"], TMP/"ddr_build", "", 60)
    if before != ddr_inputs():
        raise RuntimeError("DDR test inputs changed during compilation")
    (folder/"build.json").write_text(json.dumps({"status": "passed", "input_hashes": before,
        "elf_sha256": sha(folder/"test.elf")}, indent=2)+"\n", encoding="utf-8")


def test_ddr(args, folder):
    program = json.loads((OUT/"program.json").read_text(encoding="utf-8"))
    if program["screen_acceptance"] != "passed" or program["usr_access"] != 0x5a374e31:
        raise RuntimeError("New-board static display acceptance required before DDR stage")
    build = json.loads((OUT/"ddr/build.json").read_text(encoding="utf-8"))
    if build["status"] != "passed" or build["input_hashes"] != ddr_inputs() or sha(OUT/"ddr/test.elf") != build["elf_sha256"]:
        raise RuntimeError("Current passing DDR test build required")
    folder.mkdir(parents=True, exist_ok=True)
    for source, name, expected in [(OUT/"ddr/test.elf", "test.elf", build["elf_sha256"]),
             (ROOT/"hardware/z7_nano/vendor_ps7_init.tcl", "ps7_init.tcl",
              build["input_hashes"]["hardware/z7_nano/vendor_ps7_init.tcl"])]:
        shutil.copyfile(source, folder/name)
        if sha(folder/name) != expected:
            raise RuntimeError("DDR input snapshot changed")
    run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
         ROOT/"scripts/hardware_z7_nano.tcl", "-tclargs", "probe", folder/"probe.json"],
        folder/"probe", "PASS_Z7_NANO_HARDWARE", 90)
    probe = json.loads((folder/"probe.json").read_text(encoding="utf-8"))
    if probe["usr_access"] != 0x5a374e31:
        raise RuntimeError("DDR test requires the live static platform without PL DMA")
    serial = probe["cable_serial"]
    run([args.xsdb, ROOT/"scripts/z7_nano_ps_test.tcl", serial, folder/"test.elf",
         folder/"ps7_init.tcl", folder/"result.json"], folder/"test", "PASS_Z7_NANO_DDR_TEST", 120)
    result = json.loads((folder/"result.json").read_text(encoding="utf-8"))
    result.update(attempt=folder.relative_to(ROOT).as_posix(), input_hashes=build["input_hashes"],
                  elf_sha256=sha(folder/"test.elf"), ps_init_sha256=sha(folder/"ps7_init.tcl"))
    (OUT/"ddr/run.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
