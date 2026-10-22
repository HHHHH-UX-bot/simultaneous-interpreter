"""Read only named public counters for a known, already loaded ELF."""
import argparse, hashlib, json, subprocess, tempfile
from datetime import datetime, timezone
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--symbols',type=Path,required=True)
p.add_argument('--elf',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--xsdb',type=Path,default=Path(r'C:\Xilinx\Vivado\2018.3\bin\xsdb.bat'))
a=p.parse_args()
symbols={parts[2]:int(parts[0],16) for line in a.symbols.read_text().splitlines() if len(parts:=line.split())==3}
counts={'network_report':16,'cloud_report':16,'client':24,'duplex_diag':38,'duplex_fault_state':25,'upload_diag':7,'speech_diag':18,'link_recovery_diag':7,'session_diag':9,'receive_priority_diag':5,'caption_queue_diag':36,'final_diag':15,'cloud_health':16,'network_heap':9}
counts['keepalive_diag']=5
counts['link_quality']=5
counts['first_wire_fault']=8
counts['first_uart_fault']=8
counts['arm_tls_diag']=24
counts['arm_tls_fault']=12
counts['arm_tls_tx_trace']=260
counts['arm_tls_tx_identity']=2
counts['network_tx_queue_trace']=260
counts['network_tx_uart_trace']=260
counts['network_tx_wire_trace']=262
counts['network_receive_probe_diag']=16
script=['connect -url TCP:127.0.0.1:3121','after 300',
 'set b [targets -target-properties -filter {name == "xc7z010"}]',
 'if {[llength $b]!=1 || ![dict exists [lindex $b 0] jtag_cable_serial] || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}',
 'targets -set -filter {name =~ "*Cortex-A9*#0"}']
for name,n in counts.items():
    if name in symbols: script.append(f'puts "COUNTER {name} [mrd -force -value 0x{symbols[name]:x} {n}]"')
for name,address,n in [('uart',0x43c30018,6),('uart_state',0x43c30004,2),('capture',0x43c20090,2),('usb_power',0xe0002184,1)]:
    addresses=' '.join(f'0x{address+4*i:x}' for i in range(n))
    script.append(f'set values {{}}; foreach a {{{addresses}}} {{lappend values [mrd -force -value $a]}}')
    script.append(f'puts "COUNTER {name} $values"')
script.append('disconnect')
temp = Path(__file__).resolve().parents[2] / 'tmp/board_observation'
temp.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='z7-public-', dir=temp) as td:
    tcl=Path(td)/'snapshot.tcl'
    tcl.write_text('\n'.join(script)+'\n',encoding='ascii')
    result=subprocess.run([str(a.xsdb),str(tcl)],capture_output=True,text=True,timeout=40)
result.check_returncode()
data={}
for line in result.stdout.splitlines():
    if line.startswith('COUNTER '):
        _,name,*values=line.split()
        data[name]=[int(v,0) for v in values]
if not {'network_report','cloud_report','uart'}<=data.keys(): raise RuntimeError('Incomplete counter snapshot: '+result.stdout[-800:])
for name,magic in {'network_report':0x3154454e,'cloud_report':0x31564c43,'duplex_diag':0x31505544}.items():
    if name in data and data[name][0]!=magic:raise RuntimeError('Public ABI mismatch: '+name)
record={'utc':datetime.now(timezone.utc).isoformat(),'elf_sha256':hashlib.sha256(a.elf.read_bytes()).hexdigest(),'counters':data}
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record))
