"""Record offline pilot framing/input/snapshot/font checks."""
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
import validate_network_protocol as common
INPUTS=('scripts/network_demo_client.py','scripts/network_connect.py','scripts/validate_network_client.py',
        'tests/test_network_client.py','output/subtitle_ui/assets_manifest.json')
def main():
    import sys
    r={'Status':'pending','SourceHashes':fb.hashes(INPUTS),'StartedAtUTC':fb.now()}
    common.save('client_tests.json',r);runner.OUT=common.OUT;runner.TEMP=common.TEMP
    try:
        runner.command([sys.executable,'-B','-X','utf8',fb.ROOT/'tests/test_network_client.py'],'client/tests.log','OK')
        if fb.hashes(INPUTS)!=r['SourceHashes']:raise RuntimeError('Client inputs changed')
        r.update(Status='passed',Tests=12,VerifiedAtUTC=fb.now(),Scope='Offline fragments, ACK identity/codes/epoch, incremental journal, retained history content, Unicode bytes/limits, glyph subset, model offsets and private/runtime/hidden-console input validation')
        common.save('client_tests.json',r);print('PASS_NETWORK_CLIENT_TESTS',flush=True);return 0
    except (Exception,KeyboardInterrupt) as e:
        r.update(Status='failed',Error=str(e));common.save('client_tests.json',r);print(e);return 1
if __name__=='__main__':raise SystemExit(main())
