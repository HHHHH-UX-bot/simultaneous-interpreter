"""Real fixed-point Opus DTX encode/decode regression; synthetic audio only."""
import argparse,hashlib,json,runpy,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--gcc',type=Path,required=True);args=parser.parse_args()
    temp=ROOT/'tmp/speech_playback';temp.mkdir(parents=True,exist_ok=True)
    out=ROOT/'output/zhen';out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-O2','-DHAVE_CONFIG_H','-DZ7_TRANSLATE_AUDIO']
    for suffix in ['','include','config','celt','silk','silk/fixed']:flags+=['-I',str(ROOT/'third_party/opus'/suffix)]
    flags+=['-I',str(ROOT/'software/network_ui')]
    sources=runpy.run_path(str(ROOT/'scripts/build_cloud_app.py'))['opus_sources']()
    sources=[ROOT/'tests/opus_dtx_test.c',ROOT/'software/network_ui/opus_codec.c',*sources]
    exe=temp/'opus_dtx_test.exe'
    command=[str(args.gcc),*flags,*map(str,sources),'-o',str(exe)]
    compiled=subprocess.run(command,capture_output=True,timeout=180)
    (out/'opus_dtx_build.log').write_bytes(compiled.stdout+compiled.stderr);compiled.check_returncode()
    tested=subprocess.run([str(exe)],capture_output=True,timeout=30)
    (out/'opus_dtx_native.log').write_bytes(tested.stdout+tested.stderr);tested.check_returncode()
    record={'status':'passed','result':tested.stdout.decode().strip(),'scope':'500 synthetic 20 ms frames through actual fixed-point encoder and decoder; not cloud recognition accuracy','sha256':{str(p.relative_to(ROOT)).replace(chr(92),'/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (out/'opus_dtx_validation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(record['result'])
if __name__=='__main__':main()
