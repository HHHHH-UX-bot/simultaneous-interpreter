"""Simulate and build the Z7-Nano audio loopback.

Stages, in order:
  simulate  compile the audio RTL and run the five self-checking testbenches,
            recording the measured loopback delay and L1 latency
  build     run the Vivado batch build on top of a passing simulation

Evidence goes to output/z7_nano/audio_loopback/ (simulation.json, build.json)
with logs under logs/. Nothing touches the board; loading the bitstream follows
the existing display flow in run_z7_nano_display.py.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/z7_nano/audio_loopback"
LOGS = OUT / "logs"
TEMP = ROOT / "tmp/audio_loopback"

RTL_INPUTS = [
    "rtl/i2c_master.v",
    "rtl/wm8731_init.v",
    "rtl/i2s_loopback.v",
    "rtl/audio_latency.v",
    "rtl/audio_regs.v",
    "rtl/audio_overlay.v",
]
SIM_INPUTS = [
    "sim/tb_i2c_wm8731.v",
    "sim/tb_i2s_loopback.v",
    "sim/tb_audio_latency.v",
    "sim/tb_audio_regs.v",
    "sim/tb_audio_overlay.v",
]
TESTS = [
    ("tb_i2c_wm8731", "PASS_I2C_WM8731"),
    ("tb_i2s_loopback", "PASS_I2S_LOOPBACK"),
    ("tb_audio_latency", "PASS_AUDIO_LATENCY"),
    ("tb_audio_regs", "PASS_AUDIO_REGS"),
    ("tb_audio_overlay", "PASS_AUDIO_OVERLAY"),
]
BUILD_INPUTS = RTL_INPUTS + [
    "rtl/z7_nano_audio_top.v",
    "rtl/framebuffer_video.v",
    "rtl/hdmi_clock.v",
    "rtl/async_rst_sync.v",
    "rtl/dvi_encoder.v",
    "rtl/dvi_transmitter_top.v",
    "rtl/serializer_10_to_1.v",
    "scripts/build_z7_nano_audio.tcl",
    "scripts/prepare_z7_nano.py",
    "hardware/z7_nano/source.json",
    "hardware/z7_nano/audio_module.json",
]


def sha(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def hashes(paths):
    return [{"File": p, "SHA256": sha(p)} for p in paths]


def check(record, paths):
    for item in record:
        if sha(item["File"]) != item["SHA256"]:
            raise RuntimeError(f"Input changed: {item['File']}")


def save(name, payload):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
                            encoding="utf-8")


def read(name):
    return json.loads((OUT / name).read_text(encoding="utf-8"))


def run(exe, args, log_path, marker=None, cwd=None):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("LANG", "C.UTF-8")
    proc = subprocess.run([str(exe), *map(str, args)], cwd=str(cwd) if cwd else str(ROOT),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    text = proc.stdout.decode("utf-8", "replace")
    log_path.write_text(text, encoding="utf-8")
    if proc.returncode != 0:
        raise RuntimeError(f"{Path(exe).name} failed ({proc.returncode}); see {log_path}")
    if marker and marker not in text:
        raise RuntimeError(f"{Path(exe).name} did not print {marker}; see {log_path}")
    return text


def find_xsim_bin(explicit):
    if explicit:
        return Path(explicit)
    for base in (Path("C:/Xilinx/Vivado/2018.3/bin"), Path("D:/AMDDesignTools/2026.1/Vivado/bin")):
        if (base / "xvlog.bat").is_file():
            return base
    found = shutil.which("xvlog") or shutil.which("xvlog.bat")
    if found:
        return Path(found).parent
    raise RuntimeError("xsim not found; pass --xsim-bin <Vivado bin>")


def simulate(xsim_bin):
    OUT.mkdir(parents=True, exist_ok=True)
    work = TEMP / "sim"
    work.mkdir(parents=True, exist_ok=True)
    logic = hashes(RTL_INPUTS)
    tests = hashes(SIM_INPUTS)
    attempt = str(uuid.uuid4())

    run(xsim_bin / "xvlog.bat",
        [*[ROOT / p for p in RTL_INPUTS + SIM_INPUTS]],
        LOGS / "xvlog.log", None, work)
    results = []
    for top, marker in TESTS:
        run(xsim_bin / "xelab.bat", ["work." + top, "-s", top, "--debug", "off"],
            LOGS / f"{top}_xelab.log", None, work)
        text = run(xsim_bin / "xsim.bat", [top, "-runall"],
                   LOGS / f"{top}_xsim.log", marker, work)
        info = [line.strip() for line in text.splitlines()
                if line.strip().startswith("INFO") and "loopback" in line]
        results.append({"Top": top, "Marker": marker, "Info": info})

    check(logic, RTL_INPUTS)
    check(tests, SIM_INPUTS)
    save("simulation.json", {
        "Status": "passed",
        "AttemptId": attempt,
        "VerifiedAtUTC": datetime.now(timezone.utc).isoformat(),
        "Tool": str(xsim_bin),
        "Tests": results,
        "LogicHashes": logic,
        "TestHashes": tests,
    })
    print("PASS_AUDIO_LOOPBACK_SIMULATE")


def build(vivado, xsim_bin):
    sim = read("simulation.json")
    if sim["Status"] != "passed":
        raise RuntimeError("Simulation must pass before build")
    check(sim["LogicHashes"], RTL_INPUTS)
    check(sim["TestHashes"], SIM_INPUTS)
    logic = hashes(BUILD_INPUTS)
    work = TEMP / "build"
    work.mkdir(parents=True, exist_ok=True)

    # Regenerate the board inputs so the constraints match source.json.
    run(Path(sys.executable), ["-B", ROOT / "scripts/prepare_z7_nano.py"],
        LOGS / "prepare.log", "PASS_Z7_NANO_PREPARE")

    run(vivado, ["-mode", "batch", "-notrace", "-source", ROOT / "scripts/build_z7_nano_audio.tcl",
                 "-log", LOGS / "build.log", "-journal", work / "build.jou", "-tclargs", "build"],
        LOGS / "build_console.log", "PASS_Z7_NANO_AUDIO_BUILD", work)
    check(logic, BUILD_INPUTS)
    result = json.loads((ROOT / "output/z7_nano/audio/implementation.json")
                        .read_text(encoding="utf-8"))
    bitstream = ROOT / "output/z7_nano/audio/z7_nano_audio_top.bit"
    result.update(Status="passed", LogicHashes=logic,
                  BitstreamSHA256=sha(bitstream.relative_to(ROOT).as_posix()),
                  VerifiedAtUTC=datetime.now(timezone.utc).isoformat())
    save("build.json", result)
    print("PASS_AUDIO_LOOPBACK_BUILD")


def synth(vivado):
    """Per-module out-of-context synthesis: proves the audio RTL builds for the
    part without needing the block design that only the project Vivado can make."""
    logic = hashes(BUILD_INPUTS)
    work = TEMP / "ooc"
    work.mkdir(parents=True, exist_ok=True)
    ooc = ROOT / "output/z7_nano/audio_ooc"
    ooc.mkdir(parents=True, exist_ok=True)
    run(vivado, ["-mode", "batch", "-notrace", "-source", ROOT / "scripts/audio_synth_ooc.tcl",
                 "-log", ooc / "synth.log", "-journal", work / "ooc.jou"],
        LOGS / "ooc_console.log", "PASS_AUDIO_SYNTH_OOC", work)
    check(logic, BUILD_INPUTS)
    reports = sorted(p.name for p in (ooc / "reports").glob("*_utilization.rpt"))
    if len(reports) != 6:
        raise RuntimeError(f"expected 6 utilization reports, found {len(reports)}")
    save("synthesis.json", {
        "Status": "passed",
        "Tool": str(vivado),
        "Reports": reports,
        "LogicHashes": logic,
        "VerifiedAtUTC": datetime.now(timezone.utc).isoformat(),
    })
    print("PASS_AUDIO_LOOPBACK_SYNTH")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("simulate", "synth", "build"))
    parser.add_argument("--xsim-bin", help="directory holding xvlog.bat/xelab.bat/xsim.bat")
    parser.add_argument("--vivado", type=Path,
                        default=Path("D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat"))
    args = parser.parse_args()
    bin_dir = find_xsim_bin(args.xsim_bin)
    if args.action == "simulate":
        simulate(bin_dir)
    elif args.action == "synth":
        if not args.vivado.is_file():
            raise RuntimeError(f"Vivado launcher missing: {args.vivado}")
        synth(args.vivado)
    else:
        if not args.vivado.is_file():
            raise RuntimeError(f"Vivado launcher missing: {args.vivado}")
        build(args.vivado, bin_dir)


if __name__ == "__main__":
    main()
