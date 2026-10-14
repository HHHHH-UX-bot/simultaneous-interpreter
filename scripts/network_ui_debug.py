"""Bound current-app observations to the live network task without reading secrets."""
import argparse,json,msvcrt
from pathlib import Path
import run_network_ui as app
import ui_debug as debug
INPUTS=(*app.HW_INPUTS,'scripts/ui_debug.py','scripts/ps_subtitle_ui.tcl',
        'scripts/ps_network_ui_debug.tcl','scripts/network_ui_debug.py')
def prepare():
    app.configure('sample');app.require_owner()
    debug.OUT=app.OUT;debug.app.checked_ui_build=app.checked_app;debug.app.write=app.save;debug.INPUTS=INPUTS
    command=app.net.runner.command
    def route(args,*pos,**kwargs):
        args=[app.ROOT/'scripts/ps_network_ui_debug.tcl' if str(v)==str(app.ROOT/'scripts/ps_subtitle_ui.tcl') else v for v in args]
        return command(args,*pos,**kwargs)
    app.net.runner.command=route
def execute(a):
    app.require_owner()
    code=debug.execute(a)
    if code:return code
    r=json.loads((app.OUT/('latest_'+a.action+'.json')).read_text(encoding='utf-8'))
    text=(app.OUT/'logs/hardware'/r['Attempt']/'operation.log').read_text(encoding='utf-8')
    keys=('Magic','Sequence','Phase','Error','ConfigAck','StationIP','Client','Accepted',
          'Rejected','LastRequest','LastCode','ReceivedBytes','Overflow','Framing','EchoVerified','Epoch')
    public=dict(zip(keys,debug.values(text,'NETWORK_RESULT',16)))
    if public['Magic']!=0x3154454e or public['Sequence']&1 or public['Epoch']!=r['UIResult'][10]:
        raise RuntimeError('Public network/UI ABI mismatch')
    r['NetworkResult']=public;r['PSWiFiResult']=debug.values(text,'PS_WIFI_RESULT',16)
    app.save('latest_'+a.action+'.json',r)
    (app.ROOT/r['ResultFile']).write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS_NETWORK_PUBLIC_SAMPLE '+json.dumps(public),flush=True);return 0
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=('sample','capture','command'))
    p.add_argument('--vivado',type=Path);p.add_argument('--xsdb',required=True,type=Path)
    p.add_argument('--command',choices=tuple(debug.COMMANDS));p.add_argument('--expected-epoch',type=int);a=p.parse_args()
    if a.action=='command' and a.command is None:p.error('--command required')
    with (app.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        try:
            prepare();return execute(a)
        except (Exception,KeyboardInterrupt) as e:print('Network observation stopped: '+str(e),flush=True);return 1
        finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__':raise SystemExit(main())
