"""Bounded local engineering helpers; JTAG explicitly arms test-only PCM."""
import hashlib,json,subprocess,time,urllib.request
from pathlib import Path
from board_llm_client import Client
ROOT=Path(__file__).resolve().parents[1]
class Board:
 def __init__(self,app='output/speaker_cnn/microphone_udp_test'):
  self.app=ROOT/app;self.build=json.loads((self.app/'build.json').read_text());self.load=json.loads((self.app/'load.json').read_text())
  self.symbols={v[2]:int(v[0],16) for line in (self.app/'symbols.log').read_text().splitlines() if len(v:=line.split())==3}
  assert self.build['SpeakerCNN'] and self.load['elf_sha256']==self.build['ELFSHA256']==hashlib.sha256((self.app/'cloud_app.elf').read_bytes()).hexdigest()
  assert all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in self.build['InputSHA256'].items())
  pl=json.loads((ROOT/'output/z7_nano/speaker_cnn/build.json').read_text());assert pl['usr_access']==0x5a374e35 and self.load['bit_sha256']==pl['BitstreamSHA256']
  self.work=ROOT/'tmp/speaker_cnn/board_udp';self.work.mkdir(parents=True,exist_ok=True);self.serial=0;self.client=Client('192.168.1.50','192.168.1.102',0)
 def run(self,body):
  self.serial+=1;diag=self.symbols['speaker_diag']
  text='''connect -url tcp:127.0.0.1:3121
after 300
for {set retry 0} {$retry<20} {incr retry} {
 set t [targets -target-properties -filter {name == "xc7z010"}]
 if {[llength $t]==1 && [dict exists [lindex $t 0] jtag_cable_serial]} {break}
 after 100
}
if {[llength $t]!=1 || ![dict exists [lindex $t 0] jtag_cable_serial] || [dict get [lindex $t 0] jtag_cable_serial] ne "210241277120"} {error "Wrong cable"}
targets -set -filter {name == "xc7z010"}
if {[fpga -state] ne "FPGA is configured"} {error "PL unconfigured"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Running CPU0 required"}
proc r {a} {return [lindex [mrd -force -value $a] 0]}
'''+f'if {{[r {diag}]!=0x314c5053 || [r {diag+4}]!=1 || [r {diag+12}]!=0xa3a5ae67}} {{error "Matching initialized speaker firmware required"}}\n'+body+'\ndisconnect\nexit\n'
  path=self.work/f'{self.serial:03}.tcl';path.write_text(text,encoding='ascii');r=subprocess.run([str(ROOT/'scripts/xsct_path_compat.bat'),str(path)],cwd=ROOT,capture_output=True,text=True,timeout=40)
  path.with_suffix('.log').write_text(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr)[-1000:]
  return {line.split()[0]:[int(v,0) for v in line.split()[1:]] for line in r.stdout.splitlines() if line.startswith('OBS_')}
 def observe(self):
  return self.run(f'puts "OBS_DIAG [mrd -force -value {self.symbols["speaker_diag"]} 32]"\nputs "OBS_COUNTERS [mrd -force -value 0x43c4002c 14]"\nputs "OBS_CONTROL [r 0x43c40008]"')
 def control(self,command,arm_pcm=False):
  body=f'''set q [expr {{[r 0x1e0c4]+1}}]
mwr -force 0x1e0c8 [r 0x1e0a8]
mwr -force 0x1e0cc {command}
mwr -force 0x1e0c4 $q
for {{set i 0}} {{$i<100 && [r 0x1e0d0]!=$q}} {{incr i}} {{after 20}}
if {{[r 0x1e0d0]!=$q}} {{error "UI acknowledgement timeout"}}
puts "OBS_COMMAND [r 0x1e0d4]"
after 250'''
  if arm_pcm:body+='''
set until [expr {[clock milliseconds]+12000}]
while {[r 0x43c40008]!=1} {if {[clock milliseconds]>$until} {error "Enrollment capture not ready"};after 1}
mwr -force 0x43c40008 7
after 20
puts "OBS_CONTROL [r 0x43c40008]"'''
  return self.run(body)
 def mode(self,value):return self.run(f'mwr -force 0x43c40008 {value}\nafter 30\nputs "OBS_CONTROL [r 0x43c40008]"')
 def feed(self,pcm,stop_on_reset=False):
  at=0;start=time.monotonic();busy=0
  while at<len(pcm):
   block=pcm[at:at+256].astype('<i2').tobytes()
   try:self.client.request(9,offset=at,data=block,retries=3)
   except RuntimeError as error:
    if 'board error 4:' in str(error):
     busy+=1
     if time.monotonic()-start>25:raise TimeoutError('Test stream stalled')
     time.sleep(.002);continue
    if stop_on_reset and 'board error 7:' in str(error):break
    raise
   at+=len(block)//2
  return dict(accepted_samples=at,elapsed_s=time.monotonic()-start,busy_responses=busy)
 def click(self,x,y):
  for attempt in range(2):
   state=json.load(urllib.request.urlopen('http://127.0.0.1:8787/state',timeout=5));body=json.dumps(dict(press_x=x,press_y=y,x=x,y=y,epoch=state['epoch'])).encode()
   request=urllib.request.Request('http://127.0.0.1:8787/pointer',data=body,headers={'Content-Type':'application/json'})
   result=json.load(urllib.request.urlopen(request,timeout=5))
   end=time.monotonic()+5
   while time.monotonic()<end:
    current=json.load(urllib.request.urlopen('http://127.0.0.1:8787/state',timeout=5))
    if current['epoch']==result['epoch']:break
    time.sleep(.1)
   if result['accepted']:return result
   if result['epoch']==state['epoch']:break
  raise AssertionError(result)
 def picture(self,name):
  with urllib.request.urlopen('http://127.0.0.1:8787/frame.png',timeout=5) as reply:data=reply.read()
  (ROOT/'output/speaker_cnn'/name).write_bytes(data)
 def close(self):self.client.close()
