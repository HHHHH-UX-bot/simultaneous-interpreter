"""Native checks for public AT queries plus the unchanged LAN transaction contract."""
import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=("software/network_ui/network.c","software/network_ui/network.h",
        "software/network_ui/protocol.c","software/network_ui/protocol.h",
        "software/subtitle_ui/ui_model.c","software/subtitle_ui/ui_model.h",
        "tests/network_runtime_test.c","tests/network_capabilities_test.c",
        "scripts/run_z7_nano_network.py","scripts/prepare_z7_nano_network.py",
        "scripts/validate_z7_nano_capabilities.py","output/subtitle_ui/ui_assets.c")
def hashes():return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
def main():
    parser=argparse.ArgumentParser();parser.add_argument("--gcc-directory",type=Path,default=Path("D:/cenvironment/mingw64/bin"));a=parser.parse_args()
    temp=ROOT/"tmp/z7_nano/capabilities";out=ROOT/"output/z7_nano/capabilities"
    temp.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True);before=hashes()
    exe=temp/"capabilities_test.exe"
    sources=("software/network_ui/network.c","software/network_ui/protocol.c",
             "software/subtitle_ui/ui_model.c","output/subtitle_ui/ui_assets.c","tests/network_capabilities_test.c")
    command=[a.gcc_directory/"gcc.exe","-std=c11","-Wall","-Wextra","-Werror","-O2","-DZ7_NETWORK_CAPABILITIES",
             "-I",ROOT/"software/network_ui","-I",ROOT/"software/subtitle_ui",*[ROOT/p for p in sources],"-o",exe]
    built=subprocess.run([str(p) for p in command],cwd=ROOT,capture_output=True,text=True,timeout=30)
    (temp/"build.log").write_text(built.stdout+built.stderr,encoding="utf-8");built.check_returncode()
    result=subprocess.run([str(exe)],cwd=ROOT,capture_output=True,text=True,timeout=15)
    (out/"native_tests.log").write_text(result.stdout+result.stderr,encoding="utf-8");result.check_returncode()
    if "PASS_Z7_NETWORK_CAPABILITIES" not in result.stdout or hashes()!=before:raise RuntimeError("Capability checks incomplete or inputs changed")
    record={"status":"passed","checked_utc":datetime.now(timezone.utc).isoformat(),"source_hashes":before,
            "assertions":int(re.findall(r"checks=(\d+)",result.stdout)[-1]),
            "scope":"Native fixed query transaction/parser, errors/timeout/ownership plus existing LAN suite; no physical SSL or cloud acceptance"}
    (out/"host_validation.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print("PASS_Z7_NETWORK_CAPABILITIES_VALIDATION",record["assertions"])
if __name__=="__main__":main()
