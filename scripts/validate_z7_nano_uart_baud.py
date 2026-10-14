"""Run the two supported UART startup profiles against the native module model."""
import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("software/ps_wifi/wifi_at_probe.c", "software/ps_wifi/wifi_at_probe.h",
          "software/network_ui/main.c", "scripts/run_z7_nano_network.py",
          "tests/ps_wifi_baud_test.c", "tests/ps_wifi_probe_test.c", "scripts/validate_z7_nano_uart_baud.py")


def hashes():
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in INPUTS}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc-directory", type=Path, default=Path("D:/cenvironment/mingw64/bin"))
    parser.add_argument("--wire", action="store_true", help="Also simulate nominal 921600 serial peer timing")
    parser.add_argument("--vivado-bin", type=Path, default=Path("D:/AMDDesignTools/2026.1/Vivado/bin"))
    args = parser.parse_args()
    temp = ROOT/"tmp/z7_nano/uart_921600"
    out = ROOT/"output/z7_nano/uart_921600"
    temp.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    before = hashes()
    checks = {}
    for name, test, flags, marker in (
        ("high", "ps_wifi_baud_test.c", ["-DZ7_UART_921600"], "PASS_Z7_UART_921600_HOST"),
        ("legacy", "ps_wifi_probe_test.c", [], "PASS_PS_WIFI_PROBE_HOST"),
    ):
        exe = temp/(name+"_probe_test.exe")
        command = [str(args.gcc_directory/"gcc.exe"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                   *flags, "-I", str(ROOT/"software/ps_wifi"), str(ROOT/"software/ps_wifi/wifi_at_probe.c"),
                   str(ROOT/"tests"/test), "-o", str(exe)]
        compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        (temp/(name+"_build.log")).write_text(compiled.stdout+compiled.stderr, encoding="utf-8")
        compiled.check_returncode()
        tested = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True, timeout=10)
        (out/(name+"_test.log")).write_text(tested.stdout+tested.stderr, encoding="utf-8")
        tested.check_returncode()
        if marker not in tested.stdout:
            raise RuntimeError("Missing test completion marker")
        checks[name] = int(re.search(r"checks=(\d+)", tested.stdout)[1])
    if args.wire:
        wire_inputs = ("rtl/wifi_uart_phy.v", "sim/tb_z7_fast_uart.sv")
        wire_hashes = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in wire_inputs}
        commands = (
            [args.vivado_bin/"xvlog.bat", "--sv", ROOT/wire_inputs[0], ROOT/wire_inputs[1]],
            [args.vivado_bin/"xelab.bat", "work.tb_z7_fast_uart", "-s", "tb_z7_fast_uart", "--debug", "off"],
            [args.vivado_bin/"xsim.bat", "tb_z7_fast_uart", "-runall", "-log", out/"wire_xsim.log"],
        )
        for index, command in enumerate(commands):
            result = subprocess.run([str(part) for part in command], cwd=temp,
                                    capture_output=True, text=True, timeout=60)
            (temp/f"wire_{index}.log").write_text(result.stdout+result.stderr, encoding="utf-8")
            result.check_returncode()
        if "PASS_Z7_UART_921600_WIRE" not in result.stdout:
            raise RuntimeError("Wire simulation did not finish")
        if wire_hashes != {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in wire_inputs}:
            raise RuntimeError("Wire simulation inputs changed")
        wire_record = {"status": "passed", "source_hashes": wire_hashes, "rx_bytes": 64, "tx_bytes": 64,
                       "clock_hz": 50000000, "divisor": 54, "peer_baud": 921600,
                       "scope": "Simulated PHY full duplex serial timing; no physical wiring or sustained network throughput acceptance"}
        (out/"wire_validation.json").write_text(json.dumps(wire_record, indent=2)+"\n", encoding="utf-8")
    if hashes() != before:
        raise RuntimeError("UART inputs changed during validation")
    record = {"status": "passed", "checked_utc": datetime.now(timezone.utc).isoformat(),
              "source_hashes": before, "assertions": checks, "high_rate_scenarios": 10,
              "scope": "Native cold/warm negotiation, rate-change reply, rejection, idle-line guard, timer wrap and steady error checks; no physical module or sustained throughput acceptance"}
    (out/"host_validation.json").write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")
    print("PASS_Z7_UART_BAUD_VALIDATION", json.dumps(checks))


if __name__ == "__main__":
    main()
