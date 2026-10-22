"""Capture completed synthetic-only history after a known long fixture test.

Pauses upload, waits for the socket to close, then briefly halts CPU0 to read a
coherent snapshot. Leaves CPU0 running and UI paused. Not for microphone data.
"""
import argparse,json,subprocess,sys,struct,re,difflib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--app',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
build=json.loads((a.app/'build.json').read_text());assert build['Selftest'],'Synthetic fixture only'
(root/'tmp').mkdir(exist_ok=True)
tcl_control=root/'tmp/synthetic_pause.tcl'
tcl_control.write_text('''connect -url TCP:127.0.0.1:3121
set b [targets -target-properties -filter {name == "xc7z010"}]
if {[llength $b]!=1 || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
set epoch [mrd -force -value 0x1e0a8]
set seq [expr {[mrd -force -value 0x1e0c4]+1}]
mwr -force 0x1e0c8 $epoch
mwr -force 0x1e0cc 2
mwr -force 0x1e0c4 $seq
set end [expr {[clock milliseconds]+5000}]
while {[mrd -force -value 0x1e0d0]!=$seq} {
 if {[clock milliseconds]>$end} {error "UI pause not acknowledged"}
 after 20
}
puts PASS_SYNTHETIC_PAUSE
disconnect
''')
pause=subprocess.run(['C:/Xilinx/SDK/2018.3/bin/xsct.bat',str(tcl_control)],capture_output=True)
if pause.returncode or b'PASS_SYNTHETIC_PAUSE' not in pause.stdout:raise RuntimeError('Synthetic pause failed')
a.output.parent.mkdir(parents=True,exist_ok=True);binary=a.output.with_suffix('.bin').resolve()
tcl=root/'tmp/synthetic_history.tcl';tcl.write_text('''connect -url TCP:127.0.0.1:3121
set b [targets -target-properties -filter {name == "xc7z010"}]
if {[llength $b]!=1 || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
set end [expr {[clock milliseconds]+15000}]
while {[mrd -force -value 0x1e148]!=0} {
 if {[clock milliseconds]>$end} {error "Cloud did not become idle"}
 after 50
}
stop
set failed [catch {
 set ui [mrd -force -value 0x1e080 6]
 if {[lindex $ui 4]!=135804 || [lindex $ui 3]!=135868} {error "UI ABI mismatch"}
 set addr [expr {[lindex $ui 2]+[lindex $ui 5]*[lindex $ui 3]}]
 mrd -force -bin -file {FILE} $addr 33967
} detail]
con
if {$failed} {error $detail}
puts PASS_SYNTHETIC_HISTORY
disconnect
'''.replace('FILE',binary.as_posix()))
r=subprocess.run(['C:/Xilinx/SDK/2018.3/bin/xsct.bat',str(tcl)],capture_output=True)
if r.returncode or b'PASS_SYNTHETIC_HISTORY' not in r.stdout:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
w=struct.unpack('<33967I',binary.read_bytes());state=w[8:];epoch=state[0];count=state[134676//4]
def text(v):return ''.join(chr(c) for c in v[:v.index(0)] if c<=0x10ffff) if 0 in v else ''.join(map(chr,v))
entries=[]
for i in range(count):
    row=state[2068//4+i*518:2068//4+(i+1)*518]
    if row[2]==epoch:entries.append(dict(source=text(row[6:262]),translation=text(row[262:518])))
fixture=json.loads((root/'output/tts/long_speech_20261022/input/fixture.json').read_text(encoding='utf-8'))
expected=''.join(re.findall('[\u4e00-\u9fff]',''.join(x['text'] for x in fixture['sentences'])))
cases=[];current=[];source=''
for entry in entries:
    current.append(entry);source+=''.join(re.findall('[\u4e00-\u9fff]',entry['source']))
    if len(source)>=len(expected):
        cases.append(dict(entries=current,asr_similarity=difflib.SequenceMatcher(None,expected,source,autojunk=False).ratio()));current=[];source=''
record=dict(scope='Paused synthetic-only board snapshot, completed history from current epoch; no human microphone data',epoch=epoch,history_count=count,complete_cases=cases,remaining_entries=current)
a.output.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in record.items() if k not in ('complete_cases','remaining_entries')}));print('SIMILARITY',[c['asr_similarity'] for c in cases])
