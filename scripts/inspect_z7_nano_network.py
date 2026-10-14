"""Selected-app public network observations; private credentials are never read."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import run_z7_nano_network as network
import run_z7_nano_display as display

FIELDS = ("Magic", "Sequence", "Phase", "Error", "ConfigAck", "StationIP", "Client", "Accepted",
          "Rejected", "LastRequest", "LastCode", "ReceivedBytes", "Overflow", "Framing", "EchoVerified", "Epoch")


def sample(args, folder):
    args.action = "sample"
    display.execute_app(args, folder)
    program = json.loads((display.OUT/"program.json").read_text(encoding="utf-8"))
    display.base.run([args.xsdb, display.ROOT/"scripts/z7_nano_network_sample.tcl", program["cable_serial"],
                      folder/"network.json"], folder/"network", "PASS_Z7_NANO_NETWORK_SAMPLE", 30)
    record = json.loads((folder/"network.json").read_text(encoding="utf-8"))
    record.update(NetworkResult=dict(zip(FIELDS, record["network"])),
                  attempt=folder.relative_to(display.ROOT).as_posix(), program_attempt=program["attempt"],
                  elf_sha256=display.checked_app()["elf_sha256"],
                  input_hashes=display.hashes(["scripts/inspect_z7_nano_network.py", "scripts/z7_nano_network_sample.tcl"]))
    display.save(folder/"network.json", record)
    display.save(display.OUT/"network_sample.json", record)
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vivado", type=Path, default=display.base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=display.base.XSDB)
    args = parser.parse_args()
    network.configure()
    folder = display.OUT/"attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    with display.base.hardware_lock():
        sample(args, folder)
    print("PASS_Z7_NANO_NETWORK_SAMPLE")


if __name__ == "__main__":
    main()
