"""Native state-machine test of the actual ARM driver with a bounded MMIO mock."""
import hashlib,json,subprocess
import os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
files=['software/network_ui/speaker_local.c','software/network_ui/speaker_local.h','software/network_ui/speaker_weights.h','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_model.h','tests/speaker_cnn/driver_mmio.h','tests/speaker_cnn/driver_test.c','output/speaker_cnn/model_weights.c']
work=ROOT/'tmp/speaker_cnn/driver_test';work.mkdir(parents=True,exist_ok=True)
exe=work/'test.exe'
subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2','-DZ7_LOCAL_AI','-DZ7_SPEAKER_CNN','-DZ7_HISTORY_UI','-include',str(ROOT/files[5]),*[str(ROOT/n) for n in [files[0],files[3],files[6],files[7]]],'-lm','-o',str(exe)],cwd=ROOT,check=True)
r=subprocess.run([str(exe)],cwd=ROOT,text=True,capture_output=True);print(r.stdout+r.stderr);assert r.returncode==0
record=dict(passed=True,scope='Actual driver C with MMIO and transport mocks: complete weight readback/checksum, no enrollment until TCP/TLS/queue idle, enrollment/profiles/duplicate/unknown, cancel/timeout, DAC gating; not FPGA inference or acoustic accuracy',source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files})
(ROOT/'output/speaker_cnn/driver_test.json').write_text(json.dumps(record,indent=2)+'\n')
