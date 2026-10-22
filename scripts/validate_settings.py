"""Actual C settings/render/touch and translated-PCM controls, without hardware."""
import argparse,json,os,subprocess,hashlib
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc',type=Path,required=True);a=p.parse_args()
    out=ROOT/'output/tts/native';out.mkdir(parents=True,exist_ok=True)
    results={}
    for name,sources,flags in [
        ('settings',['tests/settings_ui_test.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c'],['-DZ7_ARM_TLS','-DZ7_SOURCE_EN','-DZ7_TRANSLATE_ZH']),
        ('tts_controls',['tests/tts_controls_test.c'],['-DZ7_TRANSLATE_AUDIO']),
        ('late_preview',['tests/late_preview_test.c','software/network_ui/ogg_opus.c','software/network_ui/cloud_events.c','software/network_ui/cloud_wire.c','software/network_ui/cloud_frame.c','software/network_ui/speech_playback.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c'],['-DZ7_ARM_TLS','-DZ7_TLS_PASSTHROUGH','-DZ7_TRANSLATE_EN','-DZ7_TRANSLATE_AUDIO'])]:
        exe=out/(name+'.exe')
        r=subprocess.run([str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',*flags,'-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'software/z7_nano_usb'),'-I',str(ROOT/'third_party/wslay/lib/includes'),*[str(ROOT/f) for f in sources],'-o',str(exe)],capture_output=True)
        (out/(name+'_build.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
        r=subprocess.run([str(exe)],env={**os.environ,'SETTINGS_PREVIEW':str(out)},capture_output=True)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'))
        r.check_returncode();results[name]=r.stdout.decode().strip()
    for raw in out.glob('*.rgbx'):Image.frombytes('RGB',(1024,600),raw.read_bytes(),'raw','BGRX').save(raw.with_suffix('.png'))
    inputs=['software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_render.c','software/network_ui/speech_playback.c','software/network_ui/speech_playback.h','software/network_ui/cloud_live.c','software/network_ui/cloud_events.c','tests/settings_ui_test.c','tests/tts_controls_test.c','tests/late_preview_test.c']
    (out/'validation.json').write_text(json.dumps(dict(status='passed',tests=results,input_sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in inputs}),indent=2)+'\n')
    print(json.dumps(results))
if __name__=='__main__':main()
