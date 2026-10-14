"""Native bulk sender behavior; does not measure UART or network bandwidth."""
import hashlib
import json
import re
import subprocess
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=("software/network_ui/network.c","software/network_ui/network.h","software/network_ui/main.c",
        "software/subtitle_ui/main.c","software/network_ui/protocol.c","software/network_ui/protocol.h",
        "software/subtitle_ui/ui_model.c","software/subtitle_ui/ui_model.h","tests/network_runtime_test.c",
        "tests/network_transmit_test.c","tests/network_passthrough_test.c","scripts/prepare_z7_nano_network.py","scripts/validate_z7_nano_upload.py",
        "output/subtitle_ui/ui_assets.c")
def main():
    temp=ROOT/"tmp/z7_nano/upload";out=ROOT/"output/z7_nano/upload"
    temp.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
    exe=temp/"transmit_test.exe"
    sources=("software/network_ui/network.c","software/network_ui/protocol.c","software/subtitle_ui/ui_model.c",
             "output/subtitle_ui/ui_assets.c","tests/network_transmit_test.c")
    command=["D:/cenvironment/mingw64/bin/gcc.exe","-std=c11","-Wall","-Wextra","-Werror","-O2",
             "-I",str(ROOT/"software/network_ui"),"-I",str(ROOT/"software/subtitle_ui"),
             *[str(ROOT/p) for p in sources],"-o",str(exe)]
    built=subprocess.run(command,capture_output=True,text=True,timeout=30)
    (temp/"build.log").write_text(built.stdout+built.stderr,encoding="utf-8");built.check_returncode()
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
    (out/"native_tests.log").write_text(tested.stdout+tested.stderr,encoding="utf-8");tested.check_returncode()
    raw_exe=temp/"passthrough_test.exe";command[command.index(str(ROOT/"tests/network_transmit_test.c"))]=str(ROOT/"tests/network_passthrough_test.c")
    command[-1]=str(raw_exe)
    raw_built=subprocess.run(command,capture_output=True,text=True,timeout=30)
    (temp/"passthrough_build.log").write_text(raw_built.stdout+raw_built.stderr,encoding="utf-8");raw_built.check_returncode()
    raw_tested=subprocess.run([str(raw_exe)],capture_output=True,text=True,timeout=15)
    (out/"passthrough_native.log").write_text(raw_tested.stdout+raw_tested.stderr,encoding="utf-8");raw_tested.check_returncode()
    record={"status":"passed","input_hashes":hashes,"checked_utc":datetime.now(timezone.utc).isoformat(),
            "assertions":int(re.findall(r"checks=(\d+)",tested.stdout)[-1])+int(re.findall(r"checks=(\d+)",raw_tested.stdout)[-1]),
            "scope":"Native queued copy, FIFO/stop-bit ownership, LAN regression, raw prompt/RX/guarded escape/close/fault cleanup; not bandwidth"}
    (out/"host_validation.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print("PASS_NETWORK_TRANSMIT_VALIDATION")
if __name__=="__main__":main()
