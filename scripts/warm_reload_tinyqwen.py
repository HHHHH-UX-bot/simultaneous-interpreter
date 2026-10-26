"""Reload CPU0 on the recorded, unchanged PL/PS platform; never reinitialize DDR.

Requires the matching owned touch manifest and public guarded AT closure.
DDR assets are usable only after the new firmware rehashes their entire contents.
No credentials are read from the old firmware.
"""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8'))
def run(c):subprocess.run([str(x) for x in c],cwd=ROOT,check=True)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--app-dir',type=Path,required=True);p.add_argument('--current-app-dir',type=Path,required=True);a=p.parse_args()
 app=a.app_dir.resolve();current=a.current_app_dir.resolve();build=read(app/'build.json');old=read(current/'load.json')
 elf=app/Path(build['ELF']).name;xsct=ROOT/'scripts/xsct_path_compat.bat'
 if build.get('Status')!='passed' or not all(build.get(k) for k in ('TinyQwen','Touch','RemoteDisplay','ArmTLS')) or sha(elf)!=build['ELFSHA256']:raise RuntimeError('Verified tiny Qwen firmware required')
 if any(sha(ROOT/n)!=h for n,h in build['InputSHA256'].items()):raise RuntimeError('Application sources changed; rebuild first')
 pl=read(ROOT/'output/z7_nano/remote_display/build.json');bit=ROOT/'output/z7_nano/remote_display/z7_nano_audio_top.bit'
 if bool(build.get('SpeakerCNN'))!=bool(pl.get('SpeakerCNN')):raise RuntimeError('Speaker firmware and PL platform must match')
 if build.get('SpeakerCNN') and pl.get('usr_access')!=0x5a374e35:raise RuntimeError('Verified speaker platform identity required')
 if old['cable_serial']!='210241277120' or old['bit_sha256']!=sha(bit) or pl['BitstreamSHA256']!=sha(bit):raise RuntimeError('Unchanged recorded PL required')
 if any(sha(ROOT/x['File'])!=x['SHA256'] for x in pl['LogicHashes']):raise RuntimeError('PL inputs changed')
 if sha(ROOT/old['result'])!=old['result_sha256'] or read(ROOT/old['result']).get('display_ok')!=1:raise RuntimeError('Successful current load required')
 touch=read(current/'touch_live.json')
 if touch.get('status') not in ('running','stopped') or touch.get('load_id')!=sha(current/'load.json') or touch.get('cpu0_elf_sha256')!=old['elf_sha256']:raise RuntimeError('Matching owned CPU1 manifest required')
 attempt=app/'loads'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ');attempt.mkdir(parents=True)
 if touch['status']=='running':run([sys.executable,ROOT/'scripts/run_cloud_touch.py','stop','--app-dir',current,'--xsdb',xsct])
 run([xsct,ROOT/'scripts/close_cloud_for_tinyqwen.tcl','210241277120',attempt/'close.json'])
 close=read(attempt/'close.json')
 if close.get('status')!='passed' or not close.get('cpu0_stopped') or close.get('active_tcp') is not False:raise RuntimeError('Confirmed CPU0/AT closure required')
 script=attempt/'warm_reload.tcl'
 script.write_text('''connect -url tcp:127.0.0.1:3121
after 300
for {set i 0} {$i<20} {incr i} {
 set found [targets -target-properties -filter {name == "xc7z010"}]
 if {[llength $found]==1 && [dict exists [lindex $found 0] jtag_cable_serial]} {break}
 after 100
}
if {[llength $found]!=1 || ![dict exists [lindex $found 0] jtag_cable_serial] || [dict get [lindex $found 0] jtag_cable_serial] ne "210241277120"} {error "Wrong or unavailable cable"}
foreach core {1 0} {
 targets -set -filter "name =~ \\"*Cortex-A9*#$core\\""
 for {set i 0} {$i<20 && ![string match "Stopped:*" [state]]} {incr i} {after 100}
 if {![string match "Stopped:*" [state]]} {error "Both cores must be stopped"}
}
targets -set -filter {name =~ "*Cortex-A9*#0"}
rst -processor
dow [lindex $argv 0]
mwr -force 0x1e000 0 16
con
set end [expr {[clock milliseconds]+20000}]
while {[clock milliseconds]<$end} {
 set b [lindex [mrd -force -value 0x1e008] 0]
 set w [mrd -force -value 0x1e000 16]
 set z [lindex [mrd -force -value 0x1e008] 0]
 if {$b==$z && !($z&1) && [lindex $w 0]==0x46504231 && [lindex $w 1]==0x600d600d} {
  set f [open [lindex $argv 1] w]
  puts $f "{\\"display_ok\\":1,\\"cable_serial\\":\\"210241277120\\",\\"kind\\":\\"warm_cpu0_only\\",\\"ddr_reinitialized\\":false,\\"flips\\":[lindex $w 4]}"
  close $f
  puts PASS_WARM_CPU0_DISPLAY
  disconnect
  exit
 }
 if {[lindex $w 1]==0xbad0bad0} {error "Application startup fault: $w"}
 after 100
}
error "Warm application display startup timeout"
''',encoding='utf-8')
 result=attempt/'result.json'
 r=subprocess.run([str(xsct),str(script),str(elf),str(result)],cwd=ROOT,capture_output=True,text=True)
 (attempt/'console.log').write_text(r.stdout+r.stderr,encoding='utf-8')
 print('\n'.join(r.stdout.splitlines()[-5:]),flush=True)
 if r.returncode or 'PASS_WARM_CPU0_DISPLAY' not in r.stdout:raise RuntimeError('Warm download failed; inspect '+str(attempt/'console.log'))
 loaded=dict(cable_serial='210241277120',result=result.relative_to(ROOT).as_posix(),result_sha256=sha(result),elf_sha256=sha(elf),bit_sha256=old['bit_sha256'],ps7_init_sha256=old['ps7_init_sha256'])
 (app/'load.json').write_text(json.dumps(loaded,indent=2)+'\n',encoding='utf-8')
 run([sys.executable,ROOT/'scripts/run_cloud_touch.py','start','--app-dir',app,'--xsdb',xsct])
 print('WARM_CPU0_AND_TOUCH_LOADED; assets require full on-board SHA verification',flush=True)
if __name__=='__main__':main()
