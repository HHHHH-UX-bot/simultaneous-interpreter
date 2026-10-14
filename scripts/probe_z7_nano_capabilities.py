"""Trigger five AT queries; no association, secrets or direct host UART register access."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import run_z7_nano_network as network
import run_z7_nano_display as display

QUERIES=("ssl_buffer","ssl_auth_arguments","ssl_sni","ssl_common_name","sntp_configuration")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--vivado",type=Path,default=display.base.VIVADO)
    parser.add_argument("--xsdb",type=Path,default=display.base.XSDB)
    args=parser.parse_args();args.action="sample";network.configure()
    folder=display.ROOT/"output/z7_nano/capabilities/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    inputs=display.hashes(["scripts/probe_z7_nano_capabilities.py","scripts/z7_nano_capabilities.tcl"])
    with display.base.hardware_lock():
        display.execute_app(args,folder)
        program=json.loads((display.OUT/"program.json").read_text(encoding="utf-8"))
        display.base.run([args.xsdb,display.ROOT/"scripts/z7_nano_capabilities.tcl",program["cable_serial"],folder/"queries.json"],
                         folder/"queries","OBSERVED_Z7_CAPABILITIES",60)
        record=json.loads((folder/"queries.json").read_text(encoding="utf-8"));caps=record["capabilities"]
        if caps[4]!=31 or (caps[5]|caps[6]|caps[7])!=31 or caps[5]&caps[6] or caps[5]&caps[7] or caps[6]&caps[7]:
            raise RuntimeError("Capability query masks are inconsistent")
        if caps[9]>4:raise RuntimeError("Capability numeric field count exceeds public ABI")
        record.update(queries={name:"data_observed" if caps[5]&(1<<i) else "query_rejected" if caps[6]&(1<<i) else "invalid_response"
                               for i,name in enumerate(QUERIES)},
                      ssl_buffer=caps[8],auth_numeric_arguments=caps[10:10+caps[9]],
                      firmware_hash=caps[15],input_hashes=inputs,program_attempt=program["attempt"],
                      elf_sha256=display.checked_app()["elf_sha256"],attempt=folder.relative_to(display.ROOT).as_posix(),
                      scope="Read-only AT capability query observations; rejection does not prove TLS unsupported; no certificate, TLS connection, bandwidth or cloud acceptance")
        if inputs!=display.hashes(inputs):raise RuntimeError("Capability observer changed during execution")
        display.save(folder/"queries.json",record)
        display.save(display.ROOT/"output/z7_nano/capabilities/latest.json",record)
    print("OBSERVED_Z7_CAPABILITIES",json.dumps(record["queries"]))


if __name__=="__main__":main()
