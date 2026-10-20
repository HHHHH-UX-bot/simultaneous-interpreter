import argparse,json,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parent;build=a.directory.parent;points=[]
for i in range(4):
 f=a.directory/f'sample_{i:03d}.json'
 subprocess.run([sys.executable,'-B',str(r/'board_snapshot.py'),'--symbols',str(build/'symbols.log'),'--elf',str(build/'cloud_app.elf'),'--output',str(f)],check=True,stdout=subprocess.DEVNULL,timeout=45)
 d=json.loads(f.read_text());c=d['counters'];n,h,u,s,t=c['network_report'],c['cloud_health'],c['upload_diag'],c['speech_diag'],c['arm_tls_diag']
 point={'utc':d['utc'],'elf_sha256':d['elf_sha256'],'network_phase':n[2],'network_error':n[3],'cloud_phase':c['cloud_report'][4],'sessions':h[1],'events':h[2],'cloud_failures':h[6],'network_faults':c['duplex_diag'][34],'upload_pages':u[2],'upload_drops':u[1],'upload_queue_peak':u[3],'capture_overflows':c['capture'][1],'tls_phase':t[1],'tls_error':t[2],'tls_failures':t[10],'tls_overflows':t[7],'uart_overflow':n[12],'uart_framing':n[13],'usb_power':(c['usb_power'][0]>>12)&1,'speech_overflows':s[5],'playback_underruns':s[8]}
 points.append(point);print(json.dumps(point),flush=True)
 if i<3:time.sleep(10)
status='passed' if all(x['network_phase']==4 and x['cloud_phase']==1 and x['tls_phase']==2 and x['usb_power']==1 and all(x[k]==0 for k in ('network_error','cloud_failures','network_faults','upload_drops','capture_overflows','tls_error','tls_failures','tls_overflows','uart_overflow','uart_framing','speech_overflows','playback_underruns')) for x in points) and points[-1]['upload_pages']>points[0]['upload_pages'] else 'failed'
(a.directory/'summary.json').write_text(json.dumps({'status':status,'scope':'real microphone transport/readiness and capture polling; excludes recognition quality and subjective listening','samples':points},indent=2)+'\n');print('MICROPHONE_READY_CHECK='+status);sys.exit(0 if status=='passed' else 1)
