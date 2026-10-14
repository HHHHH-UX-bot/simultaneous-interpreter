"""Read public CPU0 AT status on the Z7N3 profile; never send UART bytes."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import run_z7_nano_wifi as wifi
import run_z7_nano_display as display


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vivado", type=Path, default=display.base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=display.base.XSDB)
    args = parser.parse_args()
    args.action = "sample"
    wifi.configure()
    folder = display.OUT/"attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    inputs = display.hashes(["scripts/inspect_z7_nano_at.py", "scripts/z7_nano_at.tcl"])
    with display.base.hardware_lock():
        display.execute_app(args, folder)
        current = json.loads((display.OUT/"program.json").read_text(encoding="utf-8"))
        display.base.run([args.xsdb, display.ROOT/"scripts/z7_nano_at.tcl", current["cable_serial"],
                          folder/"at.json"], folder/"at", "PASS_Z7_NANO_AT", 30)
        record = json.loads((folder/"at.json").read_text(encoding="utf-8"))
        if inputs != display.hashes(inputs):
            raise RuntimeError("AT observer changed during execution")
        record.update(attempt=folder.relative_to(display.ROOT).as_posix(),
                      input_hashes=inputs, program_attempt=current["attempt"],
                      elf_sha256=display.checked_app()["elf_sha256"],
                      scope="New-board PS autonomous AT/echo/firmware status; no Wi-Fi association or model data")
        display.save(folder/"at.json", record)
        display.save(display.OUT/"at_probe.json", record)
    print("PASS_Z7_NANO_AT")


if __name__ == "__main__":
    main()
