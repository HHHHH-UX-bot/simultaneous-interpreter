"""Staged UART/Wi-Fi build/program gates. No Wi-Fi credentials are read here."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/wifi"
TEMP = ROOT / "tmp/wifi"
RTL = ["static_ui_top.v", "static_ui_video.v", "hdmi_clock.v", "async_rst_sync.v",
       "dvi_encoder.v", "dvi_transmitter_top.v", "serializer_10_to_1.v",
       "wifi_byte_fifo.v", "wifi_uart_phy.v", "wifi_uart_axi.v", "wifi_ui_top.v"]
ASSET_NAMES = ["static_ui_config.vh", "static_ui_tilemap.mem", "static_ui_tiles.mem", "static_ui_palette.mem"]
BUILD_INPUTS = [f"rtl/{name}" for name in RTL] + [
    "constraints/hdmi.xdc", "constraints/wifi_uart.xdc", "scripts/build_wifi.tcl"
] + [f"output/static_ui/{name}" for name in ASSET_NAMES]
TEST_INPUTS = ["sim/tb_wifi_uart_axi.sv","sim/tb_wifi_control.sv"]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def hashes(names):
    return {name: sha(ROOT / name) for name in names}

def save(name, value):
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")

def read(name):
    return json.loads((OUT / name).read_text(encoding="utf-8-sig"))

def check(saved, names):
    if saved != hashes(names):
        raise RuntimeError("Input hashes changed; rerun the required stage.")

def run(tool, args, log_name, marker=None, cwd=None):
    path = OUT / "logs" / log_name
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Running {Path(tool).name}: {path.name}", flush=True)
    with path.open("w", encoding="utf-8") as output:
        result = subprocess.run([str(tool), *map(str,args)], cwd=cwd or TEMP,
                                stdout=output, stderr=subprocess.STDOUT)
    text = path.read_text(encoding="utf-8", errors="replace")
    if result.returncode or "CRITICAL WARNING:" in text or "\nERROR:" in text or (marker and marker not in text):
        print("\n".join(text.splitlines()[-20:]))
        raise RuntimeError(f"{Path(tool).name} failed; see {path.name}")
    if marker:
        print(marker, flush=True)

def check_display_baseline():
    # Reuse the accepted display proof only while every recorded input matches.
    old = json.loads((ROOT/"output/fpga_static/simulation_result.json").read_text(encoding="utf-8-sig"))
    if old["Status"] != "passed":
        raise RuntimeError("Accepted display simulation evidence is missing.")
    for item in old["SourceHashes"]:
        if sha(ROOT/item["File"]).lower() != item["SHA256"].lower():
            raise RuntimeError(f"Display baseline changed: {item['File']}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("simulate","build","probe","program"))
    parser.add_argument("--vivado", required=True, type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    TEMP.mkdir(parents=True,exist_ok=True)
    attempt = str(uuid.uuid4())
    started = datetime.now(timezone.utc).isoformat()
    status_file = {"simulate":"simulation.json", "build":"build.json",
                   "probe":"hardware/probe_result.json", "program":"hardware/program_result.json"}[args.action]
    save(status_file, {"Status":"pending","AttemptId":attempt,"StartedAtUTC":started})
    try:
        if not args.vivado.is_file():
            raise RuntimeError("Vivado launcher missing")
        bin_dir = args.vivado.parent
        check_display_baseline()
        logic_hashes = hashes(BUILD_INPUTS)
        if args.action == "simulate":
            test_hashes = hashes(TEST_INPUTS)
            work = TEMP/"sim"
            work.mkdir(exist_ok=True)
            inputs = ["wifi_byte_fifo.v","wifi_uart_phy.v","wifi_uart_axi.v"]
            run(bin_dir/"xvlog.bat", ["--sv",*[ROOT/"rtl"/name for name in inputs],*[ROOT/name for name in TEST_INPUTS]], "uart_xvlog.log",cwd=work)
            for top,marker in (("tb_wifi_uart_axi","PASS_WIFI_UART"),("tb_wifi_control","PASS_WIFI_CONTROL")):
                run(bin_dir/"xelab.bat", [f"work.{top}","-s",top,"--debug","off"],f"{top}_xelab.log",cwd=work)
                run(bin_dir/"xsim.bat", [top,"-runall","-log",OUT/f"logs/{top}_xsim.log"],
                    f"{top}_simulate.log",marker,work)
            check(logic_hashes,BUILD_INPUTS)
            check(test_hashes,TEST_INPUTS)
            save(status_file, {"Status":"passed","LogicHashes":logic_hashes,"TestHashes":test_hashes,
                "VerifiedAtUTC":datetime.now(timezone.utc).isoformat()})
        elif args.action == "build":
            sim = read("simulation.json")
            if sim["Status"] != "passed":
                raise RuntimeError("UART simulation must pass before build.")
            check(sim["LogicHashes"], BUILD_INPUTS)
            check(sim["TestHashes"], TEST_INPUTS)
            work = TEMP/"build"
            work.mkdir(exist_ok=True)
            for name in ASSET_NAMES:
                shutil.copyfile(ROOT/"output/static_ui"/name,work/name)
            run(args.vivado,["-mode","batch","-notrace","-source",ROOT/"scripts/build_wifi.tcl",
                "-log",OUT/"logs/build.log","-journal",work/"build.jou"],"build_console.log","PASS_WIFI_BUILD",work)
            check(logic_hashes,BUILD_INPUTS)
            result = read("implementation.json")
            result.update(Status="passed", LogicHashes=logic_hashes,
                BitstreamSHA256=sha(OUT/"wifi_ui_top.bit"), VerifiedAtUTC=datetime.now(timezone.utc).isoformat())
            save(status_file,result)
        else:
            build = read("build.json")
            if build["Status"] != "passed":
                raise RuntimeError("Build must pass before hardware access.")
            check(build["LogicHashes"],BUILD_INPUTS)
            if sha(OUT/"wifi_ui_top.bit") != build["BitstreamSHA256"]:
                raise RuntimeError("Bitstream differs from verified build.")
            # Existing user hw_server is reused; caller can start it explicitly if absent.
            run(args.vivado,["-mode","batch","-notrace","-source",ROOT/"scripts/hardware_wifi.tcl",
                "-log",OUT/f"logs/{args.action}.log","-journal",TEMP/f"{args.action}.jou",
                "-tclargs",args.action,attempt,build["BitstreamSHA256"]],
                f"{args.action}_console.log",f"PASS_WIFI_{args.action.upper()}")
            check(logic_hashes,BUILD_INPUTS)
            if sha(OUT/"wifi_ui_top.bit") != build["BitstreamSHA256"]:
                raise RuntimeError("Bitstream changed during hardware operation.")
            result=read(status_file)
            if result.get("AttemptId")!=attempt or result.get("Status")!="verified":
                raise RuntimeError("Hardware result does not match the current attempt.")
            if args.action=="program" and (result.get("DONE"),result.get("EOS"),result.get("CRC_ERROR"))!=(1,1,0):
                raise RuntimeError("Configuration verification failed.")
            result.update(Status="passed",VerifiedAtUTC=datetime.now(timezone.utc).isoformat())
            save(status_file,result)
    except Exception as error:
        save(status_file,{"Status":"failed","AttemptId":attempt,"StartedAtUTC":started,
            "FailedAtUTC":datetime.now(timezone.utc).isoformat(),"Error":str(error)})
        raise

if __name__=="__main__":
    main()
