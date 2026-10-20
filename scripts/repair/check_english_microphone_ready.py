"""Observe an English microphone build only after authorized board loading."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--mode',required=True,choices=('enzh','enen'))
p.add_argument('--directory',type=Path)
p.add_argument('--xsdb',type=Path,default=Path(r'C:\Xilinx\Vivado\2018.3\bin\xsdb.bat'))
a=p.parse_args();mode=a.mode
directory=a.directory or ROOT/'output'/mode/'microphone/ready'
directory.mkdir(parents=True,exist_ok=True);build=directory.parent
b=json.loads((build/'build.json').read_text(encoding='utf-8'))
sha=hashlib.sha256((build/'cloud_app.elf').read_bytes()).hexdigest()
assert b['Mode']==mode and not b['Selftest'] and b['ELFSHA256']==sha
points=[]
for i in range(4):
    f=directory/('sample_%03d.json'%i)
    subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('board_snapshot.py')),'--elf',str(build/'cloud_app.elf'),'--symbols',str(build/'symbols.log'),'--output',str(f),'--xsdb',str(a.xsdb)],check=True,stdout=subprocess.DEVNULL,timeout=45)
    d=json.loads(f.read_text(encoding='utf-8'));c=d['counters'];n,h,u,t=c['network_report'],c['cloud_report'],c['upload_diag'],c['arm_tls_diag']
    point={'utc':d['utc'],'elf_sha256':sha,'network_phase':n[2],'network_error':n[3],'cloud_phase':h[4],'cloud_error':h[5],'client_phase':c['client'][0],'asr_finals':h[7],'translations':h[8],'committed':h[9],'upload_pages':u[2],'upload_drops':u[1],'queue_peak':u[3],'capture_overflows':c['capture'][1],'tls_phase':t[1],'tls_error':t[2],'tls_sessions':t[9],'tls_failures':t[10],'tls_overflows':t[7],'uart_overflow':n[12],'uart_framing':n[13],'usb_power':(c['usb_power'][0]>>12)&1}
    if mode=='enzh':point.update(speech_overflows=c['speech_diag'][5],playback_underruns=c['speech_diag'][8])
    points.append(point);print(json.dumps(point),flush=True)
    if i<3:time.sleep(10)
faults=['network_error','cloud_error','upload_drops','capture_overflows','tls_error','tls_failures','tls_overflows','uart_overflow','uart_framing']+(['speech_overflows','playback_underruns'] if mode=='enzh' else [])
passed=all(x['network_phase']==4 and x['client_phase']==4 and x['cloud_phase']==1 and x['tls_phase']==2 and x['tls_sessions']==1 and x['usb_power']==1 and not any(x[k] for k in faults) for x in points) and points[-1]['upload_pages']>points[0]['upload_pages']
(directory/'summary.json').write_text(json.dumps({'status':'passed' if passed else 'failed','mode':mode,'scope':'actual microphone upload/readiness only; not quality, listening or long-run stability','samples':points},indent=2)+'\n',encoding='utf-8')
sys.exit(0 if passed else 1)
