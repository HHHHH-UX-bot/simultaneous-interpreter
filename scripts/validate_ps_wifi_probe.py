"""Native fault cases for the exact freestanding AT task; no module access."""
import argparse,json,re
from pathlib import Path
import build_ps_wifi as platform
import run_framebuffer as fb
import run_ps_bringup as runner
ROOT=fb.ROOT
INPUTS=('software/ps_wifi/wifi_at_probe.c','software/ps_wifi/wifi_at_probe.h',
        'tests/ps_wifi_probe_test.c','scripts/validate_ps_wifi_probe.py')
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc-directory',type=Path,required=True);a=p.parse_args()
    runner.OUT=platform.OUT;runner.TEMP=platform.TEMP;runner.TEMP.mkdir(parents=True,exist_ok=True)
    r={'Status':'pending','SourceHashes':fb.hashes(INPUTS),'StartedAtUTC':fb.now()}
    platform.save('probe_host_tests.json',r)
    try:
        exe=platform.TEMP/'probe_test.exe'
        runner.command([a.gcc_directory/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-O2',
            '-I',ROOT/'software/ps_wifi',ROOT/'software/ps_wifi/wifi_at_probe.c',
            ROOT/'tests/ps_wifi_probe_test.c','-o',exe],'host/build.log')
        text=runner.command([exe],'host/probe_test.log','PASS_PS_WIFI_PROBE_HOST')
        if fb.hashes(INPUTS)!=r['SourceHashes']:raise RuntimeError('Host test inputs changed')
        r.update(Status='passed',Checks=int(re.search(r'checks=(\d+)',text)[1]),VerifiedAtUTC=fb.now(),
            Scope='Successful AT/echo/GMR plus wrong identity/divider, no-response, blocked TX, echo still on, missing version, overflow/framing, oversize response and timer wrap')
        platform.save('probe_host_tests.json',r);print('PASS_PS_WIFI_PROBE_HOST',flush=True);return 0
    except (Exception,KeyboardInterrupt) as e:
        r.update(Status='failed',Error=str(e),FailedAtUTC=fb.now());platform.save('probe_host_tests.json',r);print(e);return 1
if __name__=='__main__':raise SystemExit(main())
