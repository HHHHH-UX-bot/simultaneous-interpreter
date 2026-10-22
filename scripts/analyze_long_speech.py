"""Validate complete synthetic speech and report source-end to output-tail time."""
import argparse,json,statistics
from pathlib import Path
def main():
    p=argparse.ArgumentParser();p.add_argument('--metadata',required=True,type=Path);p.add_argument('--history',required=True,type=Path);p.add_argument('--fixture',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    d=json.loads(a.metadata.read_text());h=json.loads(a.history.read_text(encoding='utf-8'));f=json.loads(a.fixture.read_text(encoding='utf-8'))
    c=d['counters'];m=d['metrics'];events=d['cloud_event_trace'];epoch=h['epoch'];events=[e for e in events if e[6]==epoch]
    start=c['final_diag'][6];end=start+f['speech_ms'];streams=m['streams'];waits=[s['start_wait_ms'] for s in streams if s['start_wait_ms'] is not None]
    def final_time(kind):
        times=[e[7] for e in events if e[0]&255==kind]
        return max(times)-end if times else None
    record=dict(scope='one known synthetic six-sentence input; times relative to final input sample, not acoustic microphone-to-speaker measurements',elf_sha256=d['elf_sha256'],epoch=epoch,input_ms=f['speech_ms'],asr_finals=sum(e[0]&255==1 for e in events),translation_finals=sum(e[0]&255==2 for e in events),source_end_to_asr_final_ms=final_time(1),source_end_to_translation_final_ms=final_time(2),source_end_to_dac_drained_ms=streams[-1]['drained_ms']-end if streams else None,audio_duration_ms=m['pcm_samples']/24,median_start_wait_ms=statistics.median(waits) if waits else None,max_start_wait_ms=max(waits) if waits else None,starts=m['starts'],samples=m['pcm_samples'],played=m['played'],upload_max_age_ms=m['uplink_max_packet_wait_ms'],upload_drops=m['drops'],underruns=m['underruns'],parse_errors=m['parse_errors'],cloud_failures=m['cloud_failures'],uart_overflow=m['uart_overflow'],uart_framing=m['uart_framing'],uart_divisor=c['uart_divisor'][0],tls_handshakes=c['arm_tls_diag'][9],network_faults=c.get('duplex_diag',[None]*35)[34],recoveries=c.get('link_recovery_diag',[None]*2)[1],asr_similarity=max([x['asr_similarity'] for x in h['complete_cases']],default=0))
    record.update(completed_audio_streams=c['speech_diag'][17],overflow_chunks=c['speech_diag'][5],invalid_chunks=c['speech_diag'][6],tls_failures=c['arm_tls_diag'][10])
    record['passed']=record['asr_similarity']>=.95 and record['asr_finals']>=6 and record['translation_finals']>=6 and record['samples']==record['played'] and record['starts']==len(streams)==record['completed_audio_streams'] and not any(record[k] for k in ('upload_drops','underruns','parse_errors','cloud_failures','uart_overflow','uart_framing','overflow_chunks','invalid_chunks','tls_failures'))
    if record['network_faults'] is not None:record['passed']=record['passed'] and record['network_faults']==0 and record['tls_handshakes']==1 and record['recoveries']==0
    if 'usb_report' in c:
        usb=c['usb_report'];record.update(touch_phase=usb[2],touch_error=usb[3],touch_heartbeat=usb[14])
        record['passed']=record['passed'] and usb[1]==1 and usb[2]==4 and usb[3]==0 and d['touch_status']=='running'
    a.output.parent.mkdir(exist_ok=True,parents=True);a.output.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
    if not record['passed']:raise SystemExit(1)
if __name__=='__main__':main()
