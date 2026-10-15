"""Persistent USB Host on CPU1; start/sample/stop preserve CPU0 HDMI."""
import argparse
import json
import shutil
import subprocess
from datetime import datetime,timezone
from pathlib import Path

import run_z7_nano as base
import run_z7_nano_display as display
import run_z7_nano_usb as probe
import run_z7_nano_usb_host as host

ROOT=base.ROOT
OUT=host.OUT
MANIFEST=OUT/"active_host.json"

def save(path,value):
    display.save(path,value)

def live_inputs():
    return display.hashes(["scripts/run_z7_nano_usb_live.py","scripts/z7_nano_usb_live.tcl",
                           "scripts/z7_nano_usb_stop.tcl"])

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=("start","sample","stop"))
    parser.add_argument("--platform",choices=("display","wifi","network"),default="display")
    parser.add_argument("--vivado",type=Path,default=base.VIVADO)
    parser.add_argument("--xsdb",type=Path,default=base.XSDB)
    args=parser.parse_args()
    if args.platform=="wifi":
        from run_z7_nano_wifi import configure
        configure()
    elif args.platform=="network":
        from run_z7_nano_network import configure
        configure()
    previous=json.loads(MANIFEST.read_text()) if MANIFEST.exists() else None
    program=json.loads((display.OUT/"program.json").read_text())
    active=json.loads((display.OUT/"active_app.json").read_text())
    if program["status"]!="passed" or active["status"]!="ready" or active["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Current display program required")
    if args.action=="start":
        if previous and previous["status"] in ("starting","running","unconfirmed","start_unconfirmed") and (
            previous.get("display_program_attempt")==program["attempt"] or
            previous.get("cable_serial")!=program["cable_serial"]):
            raise RuntimeError("Sample or stop the managed Host before restarting")
        app=display.checked_app()
        build=json.loads((OUT/"host_build.json").read_text())
        phy=json.loads((OUT/"probe_run.json").read_text())
        if build["status"]!="passed" or build["input_hashes"]!=host.inputs() or \
            base.sha(OUT/"host.elf")!=build["elf_sha256"]:
            raise RuntimeError("Current Host build required")
        if phy["status"]!="passed" or phy["input_hashes"]!=probe.inputs() or \
            phy["display_program_attempt"]!=program["attempt"] or app["elf_sha256"]!=active["elf_sha256"]:
            raise RuntimeError("Current PHY proof and display app required")
        expected_generation=0
    else:
        if not previous or previous["status"] not in (("running","unconfirmed") if args.action=="stop" else ("running",)) or \
            previous["display_program_attempt"]!=program["attempt"] or previous["cable_serial"]!=program["cable_serial"]:
            raise RuntimeError("Matching managed Host required")
        expected_generation=previous["generation"]
    folder=OUT/"live_attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    inputs=live_inputs()
    shutil.copyfile(OUT/"connection.json",folder/"connection.json")
    if args.action=="start":
        shutil.copyfile(OUT/"host.elf",folder/"host.elf")
        if base.sha(folder/"host.elf")!=build["elf_sha256"]:
            raise RuntimeError("Host ELF snapshot changed")
    with base.hardware_lock():
        live=display.probe(args,folder)
        if live["usr_access"]!=display.PLATFORM_ID or not live["done"] or live["cable_serial"]!=program["cable_serial"]:
            raise RuntimeError("Live selected new-board platform identity required")
        if args.action=="start":
            save(MANIFEST,{"status":"starting","attempt":folder.relative_to(ROOT).as_posix()})
        try:
            base.run([args.xsdb,ROOT/"scripts/z7_nano_usb_live.tcl",args.action,live["cable_serial"],
                      folder/"host.elf",folder,expected_generation],folder/"run",
                     "PASS_Z7_NANO_USB_LIVE_"+args.action.upper(),30)
        except (subprocess.TimeoutExpired,KeyboardInterrupt,RuntimeError) as error:
            # Use ownership evidence, not the exception type: killed children may also raise RuntimeError.
            owner=None
            if (folder/"owned.json").exists() and not (folder/"cleanup.json").exists():
                try:
                    owner=json.loads((folder/"owned.json").read_text())
                    if "generation" not in owner and args.action=="start":
                        base.run([args.xsdb,ROOT/"scripts/z7_nano_usb_cleanup.tcl",live["cable_serial"],
                                  folder/"cleanup.json"],folder/"cleanup","PASS_Z7_NANO_USB_CLEANUP",15)
                    else:
                        recovery=folder/"cleanup"
                        recovery.mkdir()
                        base.run([args.xsdb,ROOT/"scripts/z7_nano_usb_live.tcl","stop",live["cable_serial"],
                                  "",recovery,owner["generation"]],recovery/"run","PASS_Z7_NANO_USB_LIVE_STOP",15)
                        stopped=json.loads((recovery/"result.json").read_text())
                        if stopped["action"]!="stop" or not stopped["host_cpu"].startswith("Stopped:"):
                            raise RuntimeError("Live Host cleanup not confirmed")
                        save(folder/"cleanup.json",{"status":"passed","host_cpu":"Stopped","usb_dma":"Halted",
                                                     "display_cpu":stopped["display_cpu"]})
                except Exception as cleanup_error:
                    print("USB live cleanup not confirmed: "+str(cleanup_error))
            if (folder/"cleanup.json").exists():
                save(MANIFEST,{**(previous or {}),"status":"stopped_after_failure",
                               "attempt":folder.relative_to(ROOT).as_posix(),
                               "cleanup_sha256":base.sha(folder/"cleanup.json")})
            elif args.action=="start":
                if owner and "generation" in owner:
                    save(MANIFEST,{"status":"unconfirmed","attempt":folder.relative_to(ROOT).as_posix(),
                                   "generation":owner["generation"],"cable_serial":live["cable_serial"],
                                   "display_program_attempt":program["attempt"],
                                   "display_elf_sha256":active["elf_sha256"],"elf_sha256":build["elf_sha256"]})
                else:
                    save(MANIFEST,{"status":"start_unconfirmed","attempt":folder.relative_to(ROOT).as_posix()})
            else:
                save(MANIFEST,{**previous,"status":"unconfirmed","attempt":folder.relative_to(ROOT).as_posix()})
            raise
        result=json.loads((folder/"result.json").read_text())
        result.update(attempt=folder.relative_to(ROOT).as_posix(),input_hashes=inputs,
                      cable_serial=live["cable_serial"],display_program_attempt=program["attempt"],
                      display_elf_sha256=active["elf_sha256"],
                      elf_sha256=build["elf_sha256"] if args.action=="start" else previous["elf_sha256"],
                      connection_sha256=base.sha(folder/"connection.json"))
        save(folder/"result.json",result)
        save(OUT/("live_"+args.action+".json"),result)
        save(MANIFEST,{**result,"status":"stopped" if args.action=="stop" else "running"})
    print("PASS_Z7_NANO_USB_LIVE_"+args.action.upper())

if __name__=="__main__":
    main()
