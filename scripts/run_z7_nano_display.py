"""Build and run the migrated PS/DDR UI without invoking old-board drivers."""
import argparse
import json
import shutil
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
import run_z7_nano as base

ROOT = base.ROOT
OUT = base.OUT/"display"
PLATFORM_ID = 0x5a374e32
TOP = "z7_nano_framebuffer_top"
BUILD_SCRIPT = "build_z7_nano_display.tcl"
PROGRAM_ACTION = "program-display"
PREPARES = ("prepare_z7_nano.py", "prepare_z7_nano_display.py")
PREFIX = "display"
LINKER = ROOT/"software/subtitle_ui/link.ld"
EXTRA_PLATFORM_INPUTS = ()
EXTRA_APP_INPUTS = ()
EXTRA_APP_FLAGS = ()
RTL = ("z7_nano_framebuffer_top.v", "framebuffer_video.v", "hdmi_clock.v", "async_rst_sync.v",
       "dvi_encoder.v", "dvi_transmitter_top.v", "serializer_10_to_1.v")
APP = ("software/ps_bringup/start.S", "software/subtitle_ui/main.c", "software/subtitle_ui/ui_model.c",
       "software/subtitle_ui/ui_render.c", "software/subtitle_ui/ui_demo.c", "software/subtitle_ui/runtime.c",
       "software/subtitle_ui/ui_touch.c", "output/subtitle_ui/ui_assets.c")


def save(path, record):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")


def hashes(paths):
    return {path: base.sha(ROOT/path) for path in paths}


def platform_inputs():
    paths = ["hardware/z7_nano/source.json", "scripts/prepare_z7_nano.py",
             "scripts/prepare_z7_nano_display.py", "scripts/build_z7_nano_display.tcl",
             "output/z7_nano/config/hdmi.xdc", "output/z7_nano/config/board.tcl",
             (OUT/"ps7_config.tcl").relative_to(ROOT).as_posix()]
    return hashes(paths+["rtl/"+name for name in RTL]+list(EXTRA_PLATFORM_INPUTS))


def app_inputs():
    paths = [*APP, LINKER.relative_to(ROOT).as_posix(), "software/subtitle_ui/ui_assets.h",
             "software/subtitle_ui/ui_model.h", "software/subtitle_ui/ui_render.h",
             "software/subtitle_ui/ui_demo.h", "software/subtitle_ui/ui_touch.h",
             "software/z7_nano_usb/touch_link.h", (OUT/"framebuffer_hw.h").relative_to(ROOT).as_posix()]
    return hashes(paths+list(EXTRA_APP_INPUTS))


def checked_build():
    record = json.loads((OUT/"build.json").read_text(encoding="utf-8"))
    if record["status"] != "passed" or record["input_hashes"] != platform_inputs():
        raise RuntimeError("Current display platform build required")
    for filename, digest in record["output_hashes"].items():
        if base.sha(OUT/filename) != digest:
            raise RuntimeError("Display build output changed: "+filename)
    return record


def checked_app():
    record = json.loads((OUT/"app_build.json").read_text(encoding="utf-8"))
    if record["status"] != "passed" or record["input_hashes"] != app_inputs() or \
       base.sha(OUT/"subtitle_ui.elf") != record["elf_sha256"]:
        raise RuntimeError("Current UI application build required")
    return record


def elf_layout(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise RuntimeError("Expected ELF32 little-endian")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    if header[0] != 2 or header[1] != 40 or header[3] != 0x10000:
        raise RuntimeError("Unexpected ARM executable or entry")
    ranges = ((0x10000, 0x20000), (0x01800000, 0x01a00000), (0x01b00000, 0x01b10000))
    segments = []
    for index in range(header[9]):
        kind, offset, virtual, physical, file_bytes, memory_bytes, flags, alignment = struct.unpack_from(
            "<IIIIIIII", data, header[4]+index*header[8])
        if kind != 1:
            continue
        if virtual != physical or file_bytes > memory_bytes or offset+file_bytes > len(data) or \
           not any(lo <= physical and physical+memory_bytes <= hi for lo, hi in ranges):
            raise RuntimeError("ELF load segment exceeds its reserved region")
        segments.append({"address": physical, "file_bytes": file_bytes, "memory_bytes": memory_bytes})
    if not any(s["address"] == 0x01800000 and s["file_bytes"] for s in segments):
        raise RuntimeError("Font payload missing")
    return segments


def build(args):
    before = platform_inputs()
    save(OUT/"build.json", {"status": "building", "input_hashes": before})
    base.run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
              ROOT/"scripts"/BUILD_SCRIPT, "-tclargs", "build"],
             base.TMP/(PREFIX+"_build"), "PASS_Z7_NANO_DISPLAY_BUILD", 1200)
    if before != platform_inputs():
        raise RuntimeError("Display platform inputs changed during compilation")
    outputs = (TOP+".bit", "ps7_init.tcl", "nano_display.xsa")
    save(OUT/"build.json", {"status": "passed", "input_hashes": before,
         "output_hashes": {name: base.sha(OUT/name) for name in outputs}})


def build_app(args):
    checked_build()
    # Reuse prior host/asset checks only while their actual source hashes still match.
    for name in ("model_tests.json", "demo_tests.json", "host_validation.json"):
        result = json.loads((ROOT/"output/subtitle_ui"/name).read_text(encoding="utf-8"))
        if result["Status"] != "passed" or any(base.sha(ROOT/p) != h for p, h in result["SourceHashes"].items()):
            raise RuntimeError("Shared UI validation is no longer current: "+name)
    assets = json.loads((ROOT/"output/subtitle_ui/assets_validation.json").read_text(encoding="utf-8"))
    for path, key in (("output/subtitle_ui/ui_assets.c", "source_sha256"),
                      ("software/subtitle_ui/ui_assets.h", "header_sha256"),
                      ("output/subtitle_ui/assets_manifest.json", "manifest_sha256")):
        if assets["status"] != "passed" or base.sha(ROOT/path) != assets[key]:
            raise RuntimeError("Shared font validation is no longer current")
    before = app_inputs()
    save(OUT/"app_build.json", {"status": "building", "input_hashes": before})
    flags = ["-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-O2", "-g", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-strict-aliasing", "-fno-short-enums",
             "-Wall", "-Wextra", "-Werror", "-std=c11", "-nostdlib", "-Wl,--build-id=none",
             "-DZ7_NANO_TOUCH", "-I", ROOT/"software/subtitle_ui", "-I", ROOT/"software/z7_nano_usb", "-I", OUT,
             "-T", LINKER, "-Wl,-Map="+str(OUT/"subtitle_ui.map")]
    base.run([args.gcc, *flags, *EXTRA_APP_FLAGS, *[ROOT/p for p in APP], "-lgcc", "-o", OUT/"subtitle_ui.elf"],
             base.TMP/(PREFIX+"_app_build"), "", 180)
    if before != app_inputs():
        raise RuntimeError("UI inputs changed during compilation")
    save(OUT/"app_build.json", {"status": "passed", "input_hashes": before,
         "elf_sha256": base.sha(OUT/"subtitle_ui.elf"), "segments": elf_layout(OUT/"subtitle_ui.elf"),
         "scope": "Simulated subtitle UI with USB tap consumer; no microphone or inference"})


def probe(args, folder):
    base.run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
              ROOT/"scripts/hardware_z7_nano.tcl", "-tclargs", "probe", folder/"probe.json"],
             folder/"probe", "PASS_Z7_NANO_HARDWARE", 90)
    result = json.loads((folder/"probe.json").read_text(encoding="utf-8"))
    if result["crc_error"] or (result["done"] and not result["eos"]):
        raise RuntimeError("Live FPGA configuration is not healthy")
    return result


def ps(args, action, folder, serial, elf="", init=""):
    base.run([args.xsdb, ROOT/"scripts/z7_nano_display.tcl", action, serial, elf, init, folder/"result.json"],
             folder, "PASS_Z7_NANO_DISPLAY_"+action.upper(), 90)


def program(args, folder):
    build = checked_build()
    bit = TOP+".bit"
    shutil.copyfile(OUT/bit, folder/bit)
    if base.sha(folder/bit) != build["output_hashes"][bit]:
        raise RuntimeError("Display bitstream snapshot changed")
    live = probe(args, folder)
    if live["done"] and live["usr_access"] in (0x5a374e32, 0x5a374e33):
        ps(args, "quiesce", folder/"quiesce", live["cable_serial"])
    else:
        if live["done"] and live["usr_access"] != 0x5a374e31:
            raise RuntimeError("Unrecognized live PL; stop and inspect before programming")
        base.run([args.xsdb, ROOT/"scripts/z7_nano_halt.tcl", live["cable_serial"]],
                 folder/"halt", "PASS_Z7_NANO_HALT", 45)
    save(OUT/"active_app.json", {"status": "invalidated_before_program", "attempt": folder.relative_to(ROOT).as_posix()})
    base.run([args.vivado, "-mode", "batch", "-nojournal", "-nolog", "-source",
              ROOT/"scripts/hardware_z7_nano.tcl", "-tclargs", PROGRAM_ACTION, folder/"result.json", folder/bit],
             folder/"program", "PASS_Z7_NANO_HARDWARE", 120)
    record = json.loads((folder/"result.json").read_text(encoding="utf-8"))
    record.update(attempt=folder.relative_to(ROOT).as_posix(), bitstream_sha256=build["output_hashes"][bit])
    save(OUT/"program.json", record)


def execute_app(args, folder):
    build, app = checked_build(), checked_app()
    programmed = json.loads((OUT/"program.json").read_text(encoding="utf-8"))
    if programmed["status"] != "passed" or programmed["bitstream_sha256"] != build["output_hashes"][TOP+".bit"]:
        raise RuntimeError("Current display programming required")
    live = probe(args, folder)
    if not live["done"] or live["usr_access"] != PLATFORM_ID:
        raise RuntimeError("Live migrated display platform required")
    if args.action == "run":
        consumed = OUT/"program_consumption.json"
        if consumed.exists() and json.loads(consumed.read_text(encoding="utf-8"))["program_attempt"] == programmed["attempt"]:
            raise RuntimeError("Program fresh PL before initializing PS again")
        for name, digest in (("subtitle_ui.elf", app["elf_sha256"]), ("ps7_init.tcl", build["output_hashes"]["ps7_init.tcl"])):
            shutil.copyfile(OUT/name, folder/name)
            if base.sha(folder/name) != digest:
                raise RuntimeError("Display input snapshot changed: "+name)
        save(consumed, {"program_attempt": programmed["attempt"], "run_attempt": folder.relative_to(ROOT).as_posix()})
        save(OUT/"active_app.json", {"status": "starting", "attempt": folder.relative_to(ROOT).as_posix()})
    else:
        active = json.loads((OUT/"active_app.json").read_text(encoding="utf-8"))
        if active["status"] != "ready" or active["elf_sha256"] != app["elf_sha256"] or active["program_attempt"] != programmed["attempt"]:
            raise RuntimeError("Current application start required before sampling")
    if args.action == "capture":
        from compare_z7_nano_capture import compare
        captured = folder/"capture"
        captured.mkdir()
        base.run([args.xsdb, ROOT/"scripts/z7_nano_capture.tcl", live["cable_serial"], captured],
                 captured, "PASS_Z7_NANO_CAPTURE", 120)
        record = compare(captured, app, args.native_gcc_directory)
        record.update(attempt=folder.relative_to(ROOT).as_posix(), program_attempt=programmed["attempt"],
                      bitstream_sha256=programmed["bitstream_sha256"])
        save(OUT/"capture.json", record)
        return
    ps(args, args.action, folder/"ps", live["cable_serial"], folder/"subtitle_ui.elf", folder/"ps7_init.tcl")
    record = json.loads((folder/"ps/result.json").read_text(encoding="utf-8"))
    record.update(attempt=folder.relative_to(ROOT).as_posix(), program_attempt=programmed["attempt"],
                  elf_sha256=app["elf_sha256"], bitstream_sha256=programmed["bitstream_sha256"])
    save(folder/"result.json", record)
    save(OUT/(args.action+".json"), record)
    if args.action == "run":
        save(OUT/"active_app.json", {**record, "status": "ready"})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("build", "build-app", "program", "run", "sample", "capture"))
    parser.add_argument("--vivado", type=Path, default=base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=base.XSDB)
    parser.add_argument("--gcc", type=Path, default=base.GCC)
    parser.add_argument("--native-gcc-directory", type=Path, default=Path("D:/cenvironment/mingw64/bin"))
    args = parser.parse_args()
    for script in PREPARES:
        base.run([sys.executable, "-B", ROOT/"scripts"/script], base.TMP/script[:-3], "PASS_", 30)
    if args.action == "build":
        build(args)
    elif args.action == "build-app":
        build_app(args)
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        folder = OUT/"attempts"/stamp
        folder.mkdir(parents=True)
        with base.hardware_lock():
            if args.action == "program":
                program(args, folder)
            else:
                execute_app(args, folder)
    print("PASS_Z7_NANO_DISPLAY_"+args.action.upper().replace("-", "_"), flush=True)


if __name__ == "__main__":
    main()
