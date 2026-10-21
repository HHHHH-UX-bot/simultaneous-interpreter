"""Offline actual C model/render/touch history checks; no hardware or cloud."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['tests/history_ui_test.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c']
HEADERS=['software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_layout.h','software/subtitle_ui/ui_render.h','software/subtitle_ui/ui_assets.h','software/subtitle_ui/ui_touch.h','software/z7_nano_usb/touch_link.h']
def main():
 p=argparse.ArgumentParser();p.add_argument('--gcc',type=Path,required=True);a=p.parse_args()
 out=ROOT/'output/history_ui_20261021';temp=ROOT/'tmp/history_ui_20261021';out.mkdir(exist_ok=True);temp.mkdir(exist_ok=True)
 exe=temp/'history_test.exe'
 cmd=[str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DZ7_ARM_TLS','-DZ7_SOURCE_EN','-DZ7_TRANSLATE_ZH',
      '-I',str(ROOT/'software/z7_nano_usb'),'-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/n) for n in SOURCES],'-o',str(exe)]
 r=subprocess.run(cmd,cwd=ROOT,capture_output=True,timeout=90);(out/'native_build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(exe)],cwd=ROOT,capture_output=True,env={**os.environ,'HISTORY_PREVIEW':str(out)},timeout=60)
 (out/'native.log').write_bytes(r.stdout+r.stderr)
 if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'));r.check_returncode()
 previews=[]
 for raw in out.glob('*.rgbx'):
  target=raw.with_suffix('.png');Image.frombytes('RGB',(1024,600),raw.read_bytes(),'raw','BGRX').save(target);previews.append(target.name)
 record=dict(status='passed',scope='offline synthetic finals, actual C model/render/touch; no microphone/cloud/hardware acceptance',result=r.stdout.decode().strip(),previews=previews,
             input_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in SOURCES+HEADERS})
 (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n');print(record['result'])
if __name__=='__main__':main()
