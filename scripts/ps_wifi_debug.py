"""Observe the public PS-owned AT status using the shared bounded UI debugger."""
import argparse,json,msvcrt
from pathlib import Path
import run_ps_wifi as net
import ui_debug as debug
ROOT=net.ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=('sample','capture','command'))
    p.add_argument('--xsdb',required=True,type=Path);p.add_argument('--vivado',type=Path)
    p.add_argument('--command',choices=tuple(debug.COMMANDS));p.add_argument('--expected-epoch',type=int)
    a=p.parse_args()
    if a.action=='command' and a.command is None:p.error('--command required')
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        try:
            net.configure('sample')
            debug.OUT=net.OUT
            debug.app.checked_ui_build=net.checked_app
            debug.app.write=net.platform.save
            debug.INPUTS=(*net.HW_INPUTS,'scripts/ui_debug.py','scripts/ps_subtitle_ui.tcl',
                'scripts/ps_wifi_ui_debug.tcl','scripts/ps_wifi_debug.py')
            command=net.runner.command
            def route(args,*pos,**kwargs):
                args=[ROOT/'scripts/ps_wifi_ui_debug.tcl' if str(v)==str(ROOT/'scripts/ps_subtitle_ui.tcl') else v for v in args]
                return command(args,*pos,**kwargs)
            net.runner.command=route
            result=debug.execute(a)
            if result:return result
            record=json.loads((net.OUT/('latest_'+a.action+'.json')).read_text(encoding='utf-8'))
            log=net.OUT/'logs/hardware'/record['Attempt']/'operation.log'
            values=debug.values(log.read_text(encoding='utf-8'),'PS_WIFI_RESULT',16)
            keys=('Magic','Sequence','Phase','Error','ATOK','EchoOff','FirmwareOK','FirmwareHash',
                  'RXReceived','TXStarted','RXOverflow','FramingError','Ticks','Step','Reserved0','Reserved1')
            wifi=dict(zip(keys,values))
            passed=wifi['Magic']==0x57505331 and not (wifi['Sequence']&1) and wifi['Phase']==2 and \
                wifi['Error']==0 and wifi['ATOK']==wifi['EchoOff']==wifi['FirmwareOK']==1 and \
                wifi['FirmwareHash']!=0 and wifi['TXStarted']==22 and wifi['RXReceived']>=6 and \
                wifi['RXOverflow']==wifi['FramingError']==0
            record.update(PSWiFiResult=wifi,ATProbeStatus='passed' if passed else 'failed',
                ATProbeScope='CPU-owned AT/verified echo-off/GMR; JTAG reads status only, no Wi-Fi association or TCP')
            net.platform.save('latest_'+a.action+'.json',record)
            (ROOT/record['ResultFile']).write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
            net.platform.save('at_probe.json',{'Status':record['ATProbeStatus'],'VerifiedAtUTC':net.fb.now(),
                'Evidence':record['ResultFile'],'WiFi':wifi,'ApplicationELFSHA256':record['ApplicationELFSHA256'],
                'BitstreamSHA256':record['BitstreamSHA256'],'RunAttempt':record['RunAttempt'],
                'Scope':record['ATProbeScope']})
            print('PASS_PS_OWNED_AT' if passed else 'PS AT probe did not pass: '+str(wifi),flush=True)
            return 0 if passed else 1
        except (Exception,KeyboardInterrupt) as e:
            print('PS Wi-Fi observation stopped: '+str(e),flush=True);return 1
        finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__':raise SystemExit(main())
