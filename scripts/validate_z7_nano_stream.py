"""Native validation of the reused AT engine and timed binary stream pilot."""
import hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=("software/network_ui/network.c","software/network_ui/network.h","software/network_ui/stream_pilot.c",
        "software/network_ui/stream_pilot.h","tests/network_stream_test.c","software/network_ui/main.c",
        "scripts/validate_z7_nano_stream.py")
def main():
    temp=ROOT/"tmp/z7_nano/stream";temp.mkdir(parents=True,exist_ok=True)
    output=ROOT/"output/z7_nano/stream";output.mkdir(parents=True,exist_ok=True)
    exe=temp/"stream_test.exe"
    command=["D:/cenvironment/mingw64/bin/gcc.exe","-std=c11","-Wall","-Wextra","-Werror","-O2",
             "-I",str(ROOT/"software/network_ui"),"-I",str(ROOT/"software/subtitle_ui"),
             *[str(ROOT/p) for p in ("software/network_ui/network.c","software/network_ui/stream_pilot.c",
               "software/network_ui/protocol.c","software/subtitle_ui/ui_model.c","output/subtitle_ui/ui_assets.c",
               "tests/network_stream_test.c")],"-o",str(exe)]
    built=subprocess.run(command,capture_output=True,text=True,timeout=30)
    (temp/"build.log").write_text(built.stdout+built.stderr,encoding="utf-8");built.check_returncode()
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
    (output/"native_tests.log").write_text(tested.stdout+tested.stderr,encoding="utf-8");tested.check_returncode()
    record={"status":"passed","checks":int(re.findall(r"checks=(\d+)",tested.stdout)[-1]),
            "input_hashes":{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS},
            "scope":"Native exact timed sends, IPD/control separation, handoff, invalid input, timeout and RX capacity; not hardware or TLS throughput"}
    (output/"host_validation.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print(tested.stdout.strip())
if __name__=="__main__":main()
