"""Exercise actual C config, English events, subtitles and pacing offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib

ROOT=Path(__file__).resolve().parents[1]
def main():
    profile=json.loads((ROOT/'PROFILE.json').read_text(encoding='utf-8')) if (ROOT/'PROFILE.json').exists() else {}
    p=argparse.ArgumentParser();p.add_argument('--mode',choices=('enzh','enen'),default=profile.get('mode','enzh'));p.add_argument('--gcc',type=Path,required=True);a=p.parse_args()
    out=ROOT/'output'/a.mode/'validation';out.mkdir(parents=True,exist_ok=True)
    temp=ROOT/'tmp'/a.mode;temp.mkdir(parents=True,exist_ok=True)
    defines=['-DZ7_ARM_TLS','-DZ7_TLS_PASSTHROUGH','-DZ7_SOURCE_EN']+(['-DZ7_TRANSLATE_ZH','-DZ7_TRANSLATE_AUDIO'] if a.mode=='enzh' else ['-DZ7_ASR_SUBTITLE','-DZ7_RECOGNIZE_EN'])
    sources=['tests/english_profile_test.c','software/network_ui/ogg_opus.c','software/network_ui/cloud_events.c','software/network_ui/cloud_wire.c','software/network_ui/cloud_frame.c','software/network_ui/speech_playback.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c']
    exe=temp/'profile.exe'
    cmd=[str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',*defines,'-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'third_party/wslay/lib/includes'),*[str(ROOT/s) for s in sources],'-o',str(exe)]
    c=subprocess.run(cmd,capture_output=True,timeout=90);(out/'build.log').write_bytes(c.stdout+c.stderr);c.check_returncode()
    rgb=temp/'preview.rgbx'
    r=subprocess.run([str(exe)],capture_output=True,env={**os.environ,'PROFILE_PREVIEW':str(rgb)},timeout=30);(out/'native.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    lines=r.stdout.decode().splitlines();session=json.loads(next(s[8:] for s in lines if s.startswith('SESSION ')))['session']
    assert session['input_audio_transcription']['language']=='en' and session['input_audio_format']=='opus' and session['turn_detection']['silence_duration_ms']==400
    if a.mode=='enzh':assert session['translation']['language']=='zh' and session['modalities']==['text','audio'] and session['turn_detection']['interrupt_response'] is False
    else:assert 'translation' not in session and 'modalities' not in session
    (out/'session.json').write_text(json.dumps(session,indent=2)+'\n',encoding='utf-8')
    raw=rgb.read_bytes();rows=bytearray()
    for y in range(600):
        rows.append(0)
        for x in range(1024):
            b,g,r,_=raw[(y*1024+x)*4:(y*1024+x+1)*4];rows.extend((r,g,b))
    def chunk(k,v):return struct.pack('>I',len(v))+k+v+struct.pack('>I',zlib.crc32(k+v)&0xffffffff)
    (out/'preview.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1024,600,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    inputs=sources+['software/network_ui/cloud_client.c','software/network_ui/cloud_live.c','software/network_ui/language_profile.h']
    record={'status':'passed','mode':a.mode,'native_result':lines[-1],'session':session,'scope':'real C config/event/UI/client with mock codec and TLS boundary; no board or recognition quality test','input_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in inputs}}
    (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(lines[-1],a.mode)
if __name__=='__main__':main()
