"""Native bounded AT/IPD/ACK/credential fault checks; no Wi-Fi/device access."""
import argparse,re
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
import validate_network_protocol as common
INPUTS=(*common.INPUTS,'software/network_ui/network.h','software/network_ui/network.c',
        'software/ps_wifi/wifi_at_probe.h','tests/network_runtime_test.c','scripts/validate_network_runtime.py')
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc-directory',required=True,type=Path);a=p.parse_args()
    runner.OUT=common.OUT;runner.TEMP=common.TEMP;runner.TEMP.mkdir(parents=True,exist_ok=True)
    r={'Status':'pending','SourceHashes':fb.hashes(INPUTS),'StartedAtUTC':fb.now()};common.save('runtime_tests.json',r)
    try:
        exe=common.TEMP/'network_test.exe'
        runner.command([a.gcc_directory/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-O2',
            '-I',fb.ROOT/'software/network_ui','-I',fb.ROOT/'software/subtitle_ui',
            *[fb.ROOT/p for p in ('software/network_ui/network.c','software/network_ui/protocol.c',
                'software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c','tests/network_runtime_test.c')],'-o',exe],
            'runtime/build.log')
        text=runner.command([exe],'runtime/tests.log','PASS_NETWORK_RUNTIME')
        if fb.hashes(INPUTS)!=r['SourceHashes']:raise RuntimeError('Network runtime inputs changed')
        r.update(Status='passed',Checks=int(re.search(r'checks=(\d+)',text)[1]),VerifiedAtUTC=fb.now(),
            Scope='Volatile join/server, strict echo/private wipe, fragmented TCP, exact ACK, early next packet, binary tokens, unknown font, disconnect/errors and actual CIPSEND prompt')
        common.save('runtime_tests.json',r);print('PASS_NETWORK_RUNTIME',flush=True);return 0
    except (Exception,KeyboardInterrupt) as e:
        r.update(Status='failed',Error=str(e),FailedAtUTC=fb.now());common.save('runtime_tests.json',r);print(e);return 1
if __name__=='__main__':raise SystemExit(main())
