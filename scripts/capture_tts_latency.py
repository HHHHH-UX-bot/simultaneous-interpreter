"""Read public latency metadata AFTER synthetic speech and playback are quiet.

Large debugger reads can interrupt UART service. Never use this during live
speech. Requires a matching loaded LatencyProbe firmware; no secrets or PCM.
"""
import argparse,json,re,struct,subprocess,hashlib
from pathlib import Path
from datetime import datetime,timezone
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--app',type=Path,required=True);p.add_argument('--name',default='measurement');a=p.parse_args()
app=a.app.resolve();folder=app/a.name;folder.mkdir(exist_ok=True)
build=json.loads((app/'build.json').read_text());loaded=json.loads((app/'load.json').read_text())
if not build.get('LatencyProbe') or loaded['elf_sha256']!=build['ELFSHA256']:raise RuntimeError('Matching loaded latency probe required')
(root/'tmp').mkdir(exist_ok=True)
symbols={v[2]:int(v[0],16) for l in (app/'symbols.log').read_text().splitlines() if len(v:=l.split())==3}
script=['connect -url TCP:127.0.0.1:3121','after 300','set b [targets -target-properties -filter {name == "xc7z010"}]',
'if {[llength $b]!=1 || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}',
'targets -set -filter {name =~ "*Cortex-A9*#0"}']
points={'first_uart_fault':8,'link_recovery_diag':7,'duplex_diag':38,'final_diag':15,'link_quality':5,'service_perf':16,'pipeline_diag':12,'transport_perf':16,'upload_diag':7,'speech_diag':18,'arm_tls_diag':24,'cloud_health':16,'session_diag':9,'caption_queue_diag':36,'client':24}
for name,n in points.items():
    if name in symbols:script.append(f'puts "META {name} [mrd -force -value 0x{symbols[name]:x} {n}]"')
script.append('puts "META usb_report [mrd -force -value 0xe000 16]"')
for name,count in [('speech_timeline',8193),('cloud_id_trace',16385)]:
    if name in symbols:script.append(f'mrd -force -bin -file {{{(folder/(name+".bin")).as_posix()}}} 0x{symbols[name]:x} {count}')
script+=['set c {}; foreach a {0x43c20090 0x43c20094} {lappend c [mrd -force -value $a]}','puts "META capture $c"','set u {}; foreach a {0x43c30018 0x43c3001c 0x43c30020 0x43c30024 0x43c30028 0x43c3002c} {lappend u [mrd -force -value $a]}','puts "META uart $u"','puts "META uart_divisor [mrd -force -value 0x43c30008]"','puts "META power [mrd -force -value 0xe0002184]"','disconnect']
tcl=root/'tmp/capture_latency.tcl';tcl.write_text('\n'.join(script)+'\n')
r=subprocess.run(['C:/Xilinx/SDK/2018.3/bin/xsct.bat',str(tcl)],capture_output=True);r.check_returncode()
txt=r.stdout.decode(errors='replace');values={m[0]:[int(v,0) for v in m[1].split()] for m in re.findall(r'^META (\w+) (.*)$',txt,re.M)}
if not {'pipeline_diag','speech_diag','transport_perf'}<=values.keys():raise RuntimeError('Missing latency metadata')
speech=[];events=[]
for name,width,count in [('speech_timeline',4,8193),('cloud_id_trace',8,16385)]:
    f=folder/(name+'.bin')
    if f.exists():
        words=struct.unpack('<%dI'%count,f.read_bytes());n=words[0]
        rows=[list(words[1+(i%2048)*width:1+(i%2048)*width+width]) for i in range(max(0,n-2048),n)]
        if name=='speech_timeline':speech=rows
        else:events=rows
metrics={};streams=[];chunks=[];total=0;previous_end=0;previous_drain=0
for ms,kind,value,depth in speech:
    if kind==1:
        chunks.append((total,total+value,ms));total+=value
    elif kind==5:
        incoming=[c for c in chunks if c[0]>=previous_end and c[0]<value]
        starts=[r[0] for r in speech if r[1]==3 and previous_drain<r[0]<=ms]
        first_pcm=incoming[0][2] if incoming else None;start=starts[0] if starts else None
        streams.append(dict(first_pcm_ms=first_pcm,last_pcm_ms=incoming[-1][2] if incoming else None,start_ms=start,drained_ms=ms,
                            samples=value-previous_end,chunks=len(incoming),
                            start_wait_ms=start-first_pcm if start is not None and first_pcm is not None else None))
        previous_end=value;previous_drain=ms
metrics['streams']=streams
rx=values['transport_perf'];up=values['pipeline_diag'];sp=values['speech_diag']
metrics.update(uplink_max_packet_wait_ms=up[4],uplink_max_send_wait_ms=up[5],uplink_queue_ms=up[2]*20,
               rx_transactions=rx[1],rx_bytes=rx[2],rx_mean_ms=round(rx[3]/rx[1],2) if rx[1] else None,rx_max_ms=rx[4],read_limit=rx[6],
               pcm_samples=sp[2],played=sp[9],starts=sp[7],underruns=sp[8],drops=values['upload_diag'][1],parse_errors=values['cloud_health'][4],cloud_failures=values['cloud_health'][6],uart_overflow=values['uart'][2],uart_framing=values['uart'][3])
elf=app/('cloud_app_selftest.elf' if (app/'cloud_app_selftest.elf').exists() else 'cloud_app.elf')
touch_file=app/'touch_live.json'
touch=json.loads(touch_file.read_text()) if touch_file.exists() else {}
record=dict(utc=datetime.now(timezone.utc).isoformat(),elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),cpu1_elf_sha256=touch.get('cpu1_elf_sha256'),touch_status=touch.get('status'),metrics=metrics,counters=values,speech_timeline=speech,cloud_event_trace=events)
(folder/'latency.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(metrics))
