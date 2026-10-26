"""Controlled public PCM through the programmed FPGA; no microphone recording."""
import argparse,ctypes as ct,hashlib,json,re,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tmp/speaker_cnn/deps'))
import soundfile as sf
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--app-dir',type=Path,default=ROOT/'output/speaker_cnn/microphone');a=parser.parse_args();app=a.app_dir.resolve()
 build=json.loads((app/'build.json').read_text());load=json.loads((app/'load.json').read_text());pl=json.loads((ROOT/'output/z7_nano/speaker_cnn/build.json').read_text())
 assert build['Status']=='passed' and build['SpeakerCNN'] and pl['SpeakerCNN'] and pl['usr_access']==0x5a374e35
 assert load['elf_sha256']==build['ELFSHA256']==sha(app/'cloud_app.elf')
 assert load['bit_sha256']==pl['BitstreamSHA256']==sha(ROOT/'output/z7_nano/speaker_cnn/z7_nano_audio_top.bit')
 assert all(sha(ROOT/n)==h for n,h in build['InputSHA256'].items())
 assert all(sha(ROOT/v['File'])==v['SHA256'] for v in pl['LogicHashes'])
 symbols={v[2]:int(v[0],16) for line in (app/'symbols.log').read_text().splitlines() if len(v:=line.split())==3};diag=symbols['speaker_diag']
 work=ROOT/'tmp/speaker_cnn/board_numeric';work.mkdir(parents=True,exist_ok=True)
 file=ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech/train-clean-5/1088/134315/1088-134315-0000.flac';pcm,rate=sf.read(str(file),frames=20720,dtype='int16');assert rate==16000 and len(pcm)==20720
 pcm=np.ascontiguousarray(pcm);features=np.zeros((128,16),np.int16);energy=np.zeros(128,np.uint32)
 lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/mfcc_reference.dll'));fn=lib.speaker_mfcc_frames;fn.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint32,ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
 assert fn(pcm.ctypes.data_as(ct.POINTER(ct.c_int16)),len(pcm),features.ctypes.data_as(ct.POINTER(ct.c_int16)),energy.ctypes.data_as(ct.POINTER(ct.c_uint32)))==128
 model=ROOT/'output/speaker_cnn/full100_bn/export/model.bin';words=np.fromfile(model,dtype='<u4');expected=np.zeros(64,np.int16);status=ct.c_uint32()
 cnn=ct.CDLL(str(ROOT/'tmp/speaker_cnn/cnn_reference.dll'));core=cnn.speaker_cnn_core;core.argtypes=[ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
 core(features.ctypes.data_as(ct.POINTER(ct.c_int16)),words.ctypes.data_as(ct.POINTER(ct.c_uint32)),expected.ctypes.data_as(ct.POINTER(ct.c_int16)),ct.byref(status));assert not status.value
 head='''connect -url tcp:127.0.0.1:3121
after 300
for {set retry 0} {$retry<20} {incr retry} {
 set target [targets -target-properties -filter {name == "xc7z010"}]
 if {[llength $target]==1 && [dict exists [lindex $target 0] jtag_cable_serial]} {break}
 after 100
}
if {[llength $target]!=1 || [dict get [lindex $target 0] jtag_cable_serial] ne "210241277120"} {error "Wrong cable"}
targets -set -filter {name == "xc7z010"}
if {[fpga -state] ne "FPGA is configured"} {error "PL unconfigured; no MMIO reads allowed"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Running matching speaker application required"}
proc r {a} {return [lindex [mrd -force -value $a] 0]}
'''
 head+=f'if {{[r 0x1e140]!=0x3154454e || [r {diag}]!=0x314c5053 || [r {diag+4}]!=1 || [r {diag+12}]!=0xa3a5ae67}} {{error "Matching initialized speaker firmware required before PL access"}}\n'
 head+='''set q [expr {[r 0x1e0c4]+1}]
mwr -force 0x1e0c8 [r 0x1e0a8]
mwr -force 0x1e0cc 2
mwr -force 0x1e0c4 $q
for {set i 0} {$i<100 && [r 0x1e0d0]!=$q} {incr i} {after 20}
if {[r 0x1e0d0]!=$q} {error "Pause acknowledgement timeout"}
'''
 head+=f'for {{set i 0}} {{$i<500 && ([r 0x1e148]!=0 || [r {symbols["arm_tls_diag"]+4}]!=0 || [r {diag+124}]!=0)}} {{incr i}} {{after 20}}\n'
 head+=f'if {{[r 0x1e148]!=0 || [r {symbols["arm_tls_diag"]+4}]!=0 || [r {diag+124}]!=0}} {{error "Offline quiet state required"}}\n'
 head+='''if {[r 0x43c40000]!=0x314b5053 || [r 0x43c40018]!=0xa3a5ae67 || [r 0x43c40014]!=0xaab7fc2f || !([r 0x43c4000c]&1)} {error "Model identity/checksum mismatch"}
set outcome [catch {
 mwr -force 0x43c40008 7
 after 20
'''
 chunks=[]
 for at in range(0,len(pcm),128):
  values=' '.join(str(int(v)&65535) for v in pcm[at:at+128]);count=min(128,len(pcm)-at)
  chunks.append(' set until [expr {[clock milliseconds]+5000}]\n while {[r 0x43c40024]>'+str(512-count)+'} {if {[clock milliseconds]>$until} {error "Injection FIFO stalled"};after 1}\n foreach sample {'+values+'} {mwr -force 0x43c40020 $sample}\n')
 tail=''' for {set i 0} {$i<2000 && [r 0x43c40034]!=1} {incr i} {after 2}
 if {[r 0x43c40034]!=1 || [r 0x43c4002c]!=20720 || [r 0x43c40030]!=128 || [r 0x43c40060]!=0} {error "FPGA window did not complete"}
 if {([r 0x43c4000c]&12)!=0} {error "FPGA did not become idle"}
 puts "BOARD_EMBEDDING [mrd -force -value 0x43c40100 64]"
 puts "BOARD_FEATURES [mrd -force -value 0x43c41000 2048]"
 puts "BOARD_COUNTERS [mrd -force -value 0x43c4002c 14]"
 puts "BOARD_MODEL [mrd -force -value 0x43c40010 4]"
} failure]
# Return to the physical microphone even if a comparison/read fails.
mwr -force 0x43c40008 5
after 50
if {[r 0x43c40008]!=1} {error "Live microphone restoration failed"}
disconnect
if {$outcome} {puts stderr $failure;exit 1}
puts PASS_SPEAKER_BOARD_NUMERIC_CAPTURE
exit
'''
 script=work/'run.tcl';script.write_text(head+''.join(chunks)+tail,encoding='ascii')
 result=subprocess.run([str(ROOT/'scripts/xsct_path_compat.bat'),str(script)],cwd=ROOT,capture_output=True,text=True,timeout=600)
 (work/'console.log').write_text(result.stdout+result.stderr);assert not result.returncode and 'PASS_SPEAKER_BOARD_NUMERIC_CAPTURE' in result.stdout,'Board numeric check failed; see tmp/speaker_cnn/board_numeric/console.log'
 rows={line.split()[0]:[int(v,0) for v in line.split()[1:]] for line in result.stdout.splitlines() if line.startswith('BOARD_')}
 got=np.array(rows['BOARD_EMBEDDING'],np.uint32).astype(np.uint16).view(np.int16);mfcc=np.array(rows['BOARD_FEATURES'],np.uint32).astype(np.uint16).view(np.int16).reshape(128,16)
 assert np.array_equal(got,expected),'FPGA CNN mismatch';assert np.array_equal(mfcc,features),'FPGA MFCC mismatch'
 counters=rows['BOARD_COUNTERS'];assert counters[:3]==[20720,128,1] and counters[3:7]==[0,0,0,0],counters
 record=dict(passed=True,at_utc=datetime.now(timezone.utc).isoformat(),scope='Actual programmed FPGA, controlled public PCM injection, all 2048 MFCC and 64 embedding values bit-exact to integer C; not acoustic microphone/speaker accuracy',fixture=file.relative_to(ROOT).as_posix(),fixture_sha256=sha(file),pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),model_sha256=sha(model),elf_sha256=load['elf_sha256'],bit_sha256=load['bit_sha256'],counters=counters,embedding=got.tolist(),physical_microphone_restored=True)
 (ROOT/'output/speaker_cnn/board_numeric.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record),flush=True)
if __name__=='__main__':main()
