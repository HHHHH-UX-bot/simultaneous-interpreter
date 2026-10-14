"""Exercise real demo event ordering, controls and timer rollover on the host."""
import argparse,hashlib,json,re,subprocess,uuid
from datetime import datetime,timezone
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/subtitle_ui'
INPUTS=('software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_model.c',
        'software/subtitle_ui/ui_demo.h','software/subtitle_ui/ui_demo.c',
        'software/subtitle_ui/ui_assets.h','output/subtitle_ui/ui_assets.c',
        'tests/subtitle_demo_test.c','scripts/validate_subtitle_demo.py')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def hashes(): return {p:sha(ROOT/p) for p in INPUTS}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--gcc',type=Path,required=True)
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    temp=ROOT/'tmp/subtitle_ui'/('demo_'+uuid.uuid4().hex[:8]);temp.mkdir(parents=True)
    record={'Status':'pending','SourceHashes':hashes()}
    try:
        exe=temp/'demo_test.exe'
        sources=('software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_demo.c',
                 'output/subtitle_ui/ui_assets.c','tests/subtitle_demo_test.c')
        command=[str(args.gcc),'-O2','-Wall','-Wextra','-Werror','-std=c11',
                 '-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/p) for p in sources],'-o',str(exe)]
        built=subprocess.run(command,cwd=temp,capture_output=True,text=True,timeout=120)
        (OUT/'demo_build.log').write_text(built.stdout+built.stderr,encoding='utf-8')
        if built.returncode: raise RuntimeError('Demo test build failed')
        tested=subprocess.run([str(exe)],cwd=temp,capture_output=True,text=True,timeout=30)
        (OUT/'demo_tests.log').write_text(tested.stdout+tested.stderr,encoding='utf-8')
        match=re.search(r'PASS_SUBTITLE_DEMO checks=(\d+) simulated_ms=(\d+) finals=(\d+)',tested.stdout)
        if tested.returncode or not match: raise RuntimeError('Demo timing test failed')
        if hashes()!=record['SourceHashes']: raise RuntimeError('Demo inputs changed during verification')
        record.update(Status='passed',Checks=int(match[1]),SimulatedMilliseconds=int(match[2]),
            FinalCount=int(match[3]),ExecutableSHA256=sha(exe),
            VerifiedAtUTC=datetime.now(timezone.utc).isoformat(),
            Scope='Host event timing/control/rollover; simulated time does not qualify as board soak')
        print('PASS_SUBTITLE_DEMO_TIMING',flush=True)
    except Exception as error:
        record.update(Status='failed',Error=str(error));print(error,flush=True)
    (OUT/'demo_tests.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if record['Status']=='passed' else 1
if __name__=='__main__': raise SystemExit(main())
