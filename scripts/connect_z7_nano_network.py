"""New-board runtime association; reuse the existing private format and AT driver."""
import argparse
import ipaddress
import json
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from network_connect import payload, read_credentials_line
import inspect_z7_nano_network as observer
import run_z7_nano_network as network
import run_z7_nano_display as display


def private_submit(args, data, folder, serial):
    process = subprocess.Popen([str(args.xsdb), str(display.ROOT/"scripts/z7_nano_network_config.tcl"), serial],
        cwd=folder, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace")
    try:
        output, _ = process.communicate(input=data, timeout=25)
    except (subprocess.TimeoutExpired, KeyboardInterrupt):
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=10)
            process.wait(timeout=10)
        raise RuntimeError("Private submission interrupted; inspect public network state") from None
    matches = re.findall(r"^NETWORK_CONFIG_RESULT ([0-9a-fA-FxX ]+)$", output, re.M)
    if process.returncode or len(matches)!=1 or "PASS_PRIVATE_NETWORK_CONFIG" not in output:
        raise RuntimeError("Private configuration has no verified acknowledgement")
    try:
        words = [int(word, 0) for word in matches[0].split()]
    except ValueError:
        raise RuntimeError("Invalid public configuration acknowledgement") from None
    if len(words)!=16 or any(word<0 or word>0xffffffff for word in words) or words[0]!=0x3154454e or words[1]&1 or not words[4]:
        raise RuntimeError("Invalid public configuration acknowledgement")
    return words


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--credentials-stdin", action="store_true", required=True)
    parser.add_argument("--lan-cidr", type=ipaddress.IPv4Network, default=ipaddress.IPv4Network("192.168.31.0/24"))
    parser.add_argument("--vivado", type=Path, default=display.base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=display.base.XSDB)
    args = parser.parse_args()
    try:
        data = payload(json.loads(read_credentials_line()))
    except (Exception, KeyboardInterrupt):
        raise SystemExit("Wi-Fi input rejected; no device action") from None
    network.configure()
    folder = display.OUT/"connections"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record = {"status": "pending", "attempt": folder.relative_to(display.ROOT).as_posix(),
              "scope": "New-board volatile association/TCP server only; no audio, ASR or translation"}
    with display.base.hardware_lock():
        try:
            before = observer.sample(args, folder/"before")
            if before["NetworkResult"]["Phase"]!=0 or before["wifi"][2:7]!=[2,0,1,1,1]:
                raise RuntimeError("Fresh network app and independent AT response required")
            current = json.loads((display.OUT/"program.json").read_text(encoding="utf-8"))
            record.update(program_attempt=current["attempt"], elf_sha256=before["elf_sha256"])
            record["config_ack"] = private_submit(args, data, folder, current["cable_serial"])
            data = None
            # Observe the ongoing setup; the firmware alone owns AT sequencing.
            deadline = time.monotonic()+60
            samples = []
            while time.monotonic()<deadline:
                after = observer.sample(args, folder/("after_"+str(len(samples))))
                samples.append(after["attempt"]+"/network.json")
                if time.monotonic()>=deadline:
                    raise RuntimeError("Autonomous association observation deadline expired")
                public = after["NetworkResult"]
                if public["Phase"] in (3,6):
                    break
            else:
                raise RuntimeError("Autonomous association observation deadline expired")
            if public["Phase"]!=3 or public["Error"] or public["EchoVerified"]!=1 or public["Overflow"] or public["Framing"]:
                raise RuntimeError("Autonomous association/TCP server did not reach ready state")
            address = ipaddress.IPv4Address(public["StationIP"])
            if address not in args.lan_cidr or address in (args.lan_cidr.network_address,args.lan_cidr.broadcast_address):
                raise RuntimeError("Station address is outside the confirmed LAN")
            record.update(status="passed", station_address=str(address), lan_cidr=str(args.lan_cidr), public=public,
                evidence=after["attempt"]+"/network.json", samples=samples,
                private_storage="stdin/volatile RAM only; firmware wipes credentials")
        except (Exception, KeyboardInterrupt) as error:
            record.update(status="failed", error=str(error))
        finally:
            data = None
            display.save(folder/"result.json", record)
            display.save(display.OUT/"connection.json", record)
    if record["status"]!="passed":
        raise SystemExit("Network setup stopped: "+record["error"])
    print("PASS_Z7_NANO_NETWORK_CONNECTED "+record["station_address"])


if __name__ == "__main__":
    main()
