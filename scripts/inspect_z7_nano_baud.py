"""Observe the accepted high-rate UART profile without sending module commands."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import inspect_z7_nano_network as observer
import run_z7_nano_network as network
import run_z7_nano_display as display


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vivado", type=Path, default=display.base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=display.base.XSDB)
    args = parser.parse_args()
    network.configure()
    folder = display.ROOT/"output/z7_nano/uart_921600/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    with display.base.hardware_lock():
        record = observer.sample(args, folder)
        wifi = record["wifi"]
        if not (wifi[0] == 0x57505331 and wifi[2:7] == [2, 0, 1, 1, 1] and
                wifi[9] >= 36 and wifi[10:12] == [0, 0] and wifi[14] == 54 and 912384 <= wifi[15] <= 930816):
            raise RuntimeError("High-rate AT/echo/firmware/module query acceptance failed")
        record.update(status="passed", target_baud=921600, module_baud=wifi[15],
                      divisor=wifi[14], actual_fpga_baud=50000000/wifi[14],
                      observer_hashes=display.hashes(["scripts/inspect_z7_nano_baud.py",
                          "scripts/inspect_z7_nano_network.py", "scripts/z7_nano_network_sample.tcl"]),
                      scope="Physical module at high-rate AT/echo/firmware/config query; no sustained throughput or cloud speech acceptance")
        display.save(folder/"rate.json", record)
        display.save(display.ROOT/"output/z7_nano/uart_921600/latest_rate.json", record)
    print("PASS_Z7_UART_921600", json.dumps({"module_baud": wifi[15], "divisor": wifi[14],
          "overflow": wifi[10], "framing": wifi[11]}))


if __name__ == "__main__":
    main()
