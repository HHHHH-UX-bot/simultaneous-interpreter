"""Render and exercise the exact board C UI, without simulating voice identity."""
import hashlib,json,os,subprocess
import os
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
files=['tests/speaker_cnn/ui_test.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c']
work=ROOT/'tmp/speaker_cnn/ui_test';work.mkdir(parents=True,exist_ok=True);out=ROOT/'output/speaker_cnn/ui';out.mkdir(parents=True,exist_ok=True)
exe=work/'test.exe';subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DZ7_ARM_TLS','-DZ7_TRANSLATE_EN','-DZ7_LOCAL_AI','-DZ7_TINYQWEN','-DZ7_SPEAKER_CNN','-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'software/z7_nano_usb'),*[str(ROOT/n) for n in files],'-o',str(exe)],cwd=ROOT,check=True)
r=subprocess.run([str(exe)],cwd=ROOT,env={**os.environ,'SPEAKER_UI_PREVIEW':str(work)},text=True,capture_output=True);print(r.stdout+r.stderr);assert r.returncode==0
for raw in work.glob('*.rgbx'):Image.frombytes('RGB',(1024,600),raw.read_bytes(),'raw','BGRX').save(out/(raw.stem+'.png'))
(out/'validation.json').write_text(json.dumps(dict(passed=True,scope='Actual C renderer and pointer hit tests; not physical display/touch or voice identity',source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files}),indent=2)+'\n')
