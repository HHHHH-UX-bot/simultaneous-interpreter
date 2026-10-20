"""Observe a known self-test image; records public counters, never credentials/audio."""
import argparse, json, subprocess, sys, time
from datetime import datetime, timezone
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--directory',type=Path,required=True)
p.add_argument('--seconds',type=int,default=600)
p.add_argument('--interval',type=int,default=30)
p.add_argument('--baseline-counters',action='store_true',help='compare new errors during a controlled interval; retain old counts')
a=p.parse_args()
a.directory.mkdir(parents=True,exist_ok=True)
started=time.monotonic(); points=[]; status='running'; progress_at=0; progress_events=None; baseline=None
while True:
    i=len(points)
    path=a.directory/('sample_%03d.json'%i)
    for attempt in range(3):
        result=subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('board_snapshot.py')),
            '--symbols',str(a.directory.parent/'symbols.log'),
            '--elf',str(a.directory.parent/'cloud_app_selftest.elf'),'--output',str(path)],
            capture_output=True,text=True,timeout=50)
        if not result.returncode:break
        if attempt<2:time.sleep(2)
    if result.returncode:
        print('SNAPSHOT_FAILED',result.stderr[-400:],flush=True);status='observation_failed';break
    record=json.loads(path.read_text(encoding='utf-8')); c=record['counters']
    n,h,d,u,s,cl=(c[k] for k in ('network_report','cloud_health','duplex_diag','upload_diag','speech_diag','cloud_report'))
    point={'elapsed_seconds':round(time.monotonic()-started,2),'utc':record['utc'],
        'elf_sha256':record['elf_sha256'],'network_phase':n[2],'network_error':n[3],
        'sessions':h[1],'events':h[2],'cloud_failures':h[6], 'network_faults':d[34],
        'cloud_phase':cl[4],'asr_final':cl[7],'translations':cl[8],
        'upload_drops':u[1],'upload_queue_peak':u[3],
        'speech_chunks':s[1],'speech_samples':s[2],'speech_overflows':s[5],
        'speech_invalid':s[6],'playback_underruns':s[8], 'evidence':path.name}
    if 'arm_tls_diag' in c:
        t=c['arm_tls_diag']
        point.update(tls_phase=t[1],tls_error=t[2],tls_verify_flags=t[3],tls_failures=t[10],tls_rx_overflows=t[7],tls_rx_peak=t[8],tls_handshake_ms=t[11])
    points.append(point);print(json.dumps(point),flush=True)
    keys=('cloud_failures','network_faults','upload_drops','speech_overflows','speech_invalid','playback_underruns')
    if 'arm_tls_diag' in c:keys+=('tls_failures','tls_rx_overflows')
    if baseline is None:
        baseline={k:point[k] if a.baseline_counters else 0 for k in keys}
        if a.baseline_counters and (n[2]!=4 or cl[4]!=1):status='invalid_initial_state';break
    if n[3] or any(point[k]>baseline[k] for k in keys): status='failed';break
    if progress_events!=h[2]: progress_events=h[2];progress_at=time.monotonic()-started
    if time.monotonic()-started-progress_at>90:
        status='failed_no_event_progress';break
    if time.monotonic()-started>=a.seconds:
        status='passed' if len(points)>1 and n[2]==4 and cl[4]==1 and h[2]>points[0]['events'] and s[2]>points[0]['speech_samples'] else 'inconclusive'
        break
    time.sleep(min(a.interval,max(0,a.seconds-(time.monotonic()-started))))
out={'status':status,'utc':datetime.now(timezone.utc).isoformat(),'seconds_requested':a.seconds,
     'scope':'fixed synthetic speech and silence on actual board; excludes microphone and subjective quality',
     'compare_new_errors_only':a.baseline_counters,'counter_baseline':baseline,'samples':points}
(a.directory/'summary.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
print('SOAK_RESULT='+status,flush=True)
sys.exit(0 if status=='passed' else 1)
