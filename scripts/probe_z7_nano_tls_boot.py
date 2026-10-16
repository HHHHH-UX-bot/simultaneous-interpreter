"""Key-free ARM-owned TLS identity test; PC supplies volatile Wi-Fi mailbox only."""
import argparse,json,re,subprocess
from datetime import datetime,timezone
from pathlib import Path
from network_connect import read_credentials_line,payload
import run_z7_nano_network as network
import run_z7_nano_display as display
INPUTS=('scripts/probe_z7_nano_tls_boot.py','scripts/z7_nano_tls_boot.tcl')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--credentials-stdin',required=True,action='store_true')
    parser.add_argument('--vivado',type=Path,default=display.base.VIVADO)
    parser.add_argument('--xsdb',type=Path,default=display.base.XSDB)
    args=parser.parse_args();private=payload(json.loads(read_credentials_line()))
    owner=display.ROOT/'output/z7_nano/tls/uart_uncertain.json'
    if owner.exists():raise RuntimeError('Unconfirmed UART state requires public recovery')
    network.configure();args.action='sample'
    folder=display.ROOT/'output/z7_nano/tls_boot/attempts'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    folder.mkdir(parents=True)
    record={'status':'pending','attempt':folder.relative_to(display.ROOT).as_posix(),'input_hashes':display.hashes(INPUTS),
        'scope':'ARM autonomous trusted TLS positive/wrong-CN/positive and close/MUX0; no API key, WSS, audio or certificate writes'}
    with display.base.hardware_lock():
        display.execute_app(args,folder)
        program=json.loads((display.OUT/'program.json').read_text(encoding='utf-8'))
        record.update(program_attempt=program['attempt'],elf_sha256=display.checked_app()['elf_sha256'])
        process=subprocess.Popen([str(args.xsdb),str(display.ROOT/'scripts/z7_nano_tls_boot.tcl'),program['cable_serial']],
            cwd=folder,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        reports={};released=False;tree_stopped=True
        try:
            output,_=process.communicate(input=private,timeout=240);private=None
            for name,values in re.findall(r'^ARM_TLS_RESULT (positive|wrong_identity|positive_again|positive_close|wrong_identity_close|positive_again_close) ([0-9a-fA-FxX ]+)$',output,re.M):
                words=[int(v,0) for v in values.split()]
                if len(words)!=16 or words[0]!=0x3154454e or words[1]&1:raise RuntimeError('Invalid public TLS report')
                reports[name]=words
            releases=re.findall(r'^ARM_TLS_RELEASED ([01])$',output,re.M)
            released=releases==['1']
            record['reports']=reports
            passed=process.returncode==0 and 'PASS_ARM_TLS_BOOT' in output and len(reports)==6
            record['status']='passed' if passed and all(not v[12] and not v[13] for v in reports.values()) else 'failed'
            if record['input_hashes']!=display.hashes(INPUTS):raise RuntimeError('Observer inputs changed')
        except (Exception,KeyboardInterrupt) as error:
            record.update(status='failed',failure_kind=type(error).__name__)
        finally:
            private=None
            if process.poll() is None:
                tree_stopped=False
                try:
                    stopped=subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,timeout=10)
                    process.communicate(timeout=10);tree_stopped=stopped.returncode==0
                except (Exception,KeyboardInterrupt):pass
            record.update(released=released,tool_tree_stopped=tree_stopped)
            if not released or not tree_stopped:
                record['status']='failed'
                display.save(owner,{'cable_serial':program['cable_serial'],'reason':'ARM_TLS_boot_not_released',
                    'attempt':record['attempt'],'tool_tree_stopped':tree_stopped})
            display.save(folder/'result.json',record)
            display.save(display.ROOT/'output/z7_nano/tls_boot/latest.json',record)
    print('ARM_TLS_BOOT',record['status'],flush=True)
    return 0 if record['status']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
