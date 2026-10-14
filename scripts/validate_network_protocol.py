"""Pure protocol/font/model checks without network or device access."""
import argparse,re
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
OUT=fb.ROOT/'output/network_ui';TEMP=fb.ROOT/'tmp/network_ui'
INPUTS=('software/network_ui/protocol.h','software/network_ui/protocol.c','tests/network_protocol_test.c',
    'software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_assets.h',
    'output/subtitle_ui/ui_assets.c','scripts/validate_network_protocol.py')
def save(name,r):
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True)
    import json;p.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc-directory',type=Path,required=True);a=p.parse_args()
    runner.OUT=OUT;runner.TEMP=TEMP;TEMP.mkdir(parents=True,exist_ok=True)
    r={'Status':'pending','SourceHashes':fb.hashes(INPUTS),'StartedAtUTC':fb.now()};save('protocol_tests.json',r)
    try:
        exe=TEMP/'protocol_test.exe'
        runner.command([a.gcc_directory/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-O2',
            '-I',fb.ROOT/'software/network_ui','-I',fb.ROOT/'software/subtitle_ui',
            fb.ROOT/'software/network_ui/protocol.c',fb.ROOT/'software/subtitle_ui/ui_model.c',
            fb.ROOT/'output/subtitle_ui/ui_assets.c',fb.ROOT/'tests/network_protocol_test.c','-o',exe],'protocol/build.log')
        text=runner.command([exe],'protocol/tests.log','PASS_NETWORK_PROTOCOL')
        if fb.hashes(INPUTS)!=r['SourceHashes']:raise RuntimeError('Protocol inputs changed')
        r.update(Status='passed',Checks=int(re.search(r'checks=(\d+)',text)[1]),VerifiedAtUTC=fb.now(),
            Scope='Pilot frame/ACK schema, Unicode/limits/font rejection, model ordering/epoch/controls and bounded history')
        save('protocol_tests.json',r);print('PASS_NETWORK_PROTOCOL',flush=True);return 0
    except (Exception,KeyboardInterrupt) as e:
        r.update(Status='failed',Error=str(e),FailedAtUTC=fb.now());save('protocol_tests.json',r);print(e);return 1
if __name__=='__main__':raise SystemExit(main())
