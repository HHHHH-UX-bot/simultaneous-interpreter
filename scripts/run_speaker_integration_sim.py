"""Actual AXI -> PCM -> MFCC -> CNN RTL against independent native numeric results."""
import argparse,ctypes as ct,hashlib,json,shutil,subprocess,sys,os
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tmp/speaker_cnn/deps'))
import soundfile as sf
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--model',type=Path,default=ROOT/'output/speaker_cnn/pilot/export/model.bin')
 p.add_argument('--xsim-bin',type=Path,default=Path('C:/Xilinx/Vivado/2018.3/bin'),help='directory holding xvlog/xelab/xsim .bat');a=p.parse_args()
 work=ROOT/'tmp/speaker_cnn/integration_sim';work.mkdir(exist_ok=True)
 words=np.frombuffer(a.model.read_bytes(),'<u4').copy();assert len(words)==8192 and words[2]==7076
 lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/mfcc_reference.dll'));fn=lib.speaker_mfcc_frames
 fn.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint32,ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)];fn.restype=ct.c_uint32
 for file in sorted((ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech/train-clean-5').rglob('*.flac')):
  pcm,rate=sf.read(str(file),frames=20720,dtype='int16')
  if rate!=16000 or len(pcm)!=20720:continue
  pcm=np.ascontiguousarray(pcm);mfcc=np.zeros((128,16),np.int16);energy=np.zeros(128,np.uint32)
  assert fn(pcm.ctypes.data_as(ct.POINTER(ct.c_int16)),len(pcm),mfcc.ctypes.data_as(ct.POINTER(ct.c_int16)),energy.ctypes.data_as(ct.POINTER(ct.c_uint32)))==128
  if np.count_nonzero(energy)>120:break
 else:raise RuntimeError('Public voiced fixture unavailable')
 cnn=ct.CDLL(str(ROOT/'tmp/speaker_cnn/cnn_reference.dll'));core=cnn.speaker_cnn_core
 core.argtypes=[ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
 embedding=np.zeros(64,np.int16);status=ct.c_uint32(99)
 core(mfcc.ctypes.data_as(ct.POINTER(ct.c_int16)),words.ctypes.data_as(ct.POINTER(ct.c_uint32)),embedding.ctypes.data_as(ct.POINTER(ct.c_int16)),ct.byref(status));assert status.value==0
 def mem(name,data,width): (work/name).write_text(''.join(f'{int(x)&((1<<width)-1):0{width//4}x}\n' for x in np.asarray(data).ravel()),encoding='ascii')
 mem('model.mem',words,32);mem('pcm.mem',pcm,16);mem('features.mem',mfcc,16);mem('embedding.mem',embedding,16)
 value=2166136261
 for word in words[:7076]:value=((value^int(word))*16777619)&0xffffffff
 (work/'fixture_config.vh').write_text("localparam [31:0] EXPECTED_HASH=32'h%08x;\n"%value)
 generated=[]
 for folder in ('hls_mfcc','hls_cnn'):
  for f in (ROOT/'rtl/speaker_cnn'/folder).iterdir():
   if f.suffix=='.v':generated.append(f)
   if f.suffix=='.dat':shutil.copy2(f,work/f.name)
 sources=generated+[ROOT/'rtl/speaker_cnn'/n for n in ('speaker_capture.v','speaker_pipeline.v','speaker_accel.v')]+[ROOT/'tests/speaker_cnn/speaker_accel_tb.v']
 tools=a.xsim_bin
 tool_temp=work/'tool_tmp';tool_temp.mkdir(exist_ok=True)
 tool_env=os.environ.copy();tool_env['TEMP']=str(tool_temp);tool_env['TMP']=str(tool_temp)
 def run(name,cmd,marker=None):
  # Tool-native logs use the executable name; do not redirect onto that file.
  console=work/(name+'_console.log')
  with console.open('w') as out:subprocess.run([str(x) for x in cmd],cwd=work,env=tool_env,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=600)
  output=console.read_text(encoding='utf-8',errors='replace')
  if marker and marker not in output:raise RuntimeError(name+' lacked '+marker)
 run('xvlog',[tools/'xvlog.bat','-i',ROOT/'rtl/speaker_cnn','-i',work,*sources])
 run('xelab',[tools/'xelab.bat','work.speaker_accel_tb','-s','speaker_accel_tb','--debug','off','--mt','off','--verbose','2'])
 run('xsim',[tools/'xsim.bat','speaker_accel_tb','-runall'],'PASS_SPEAKER_FULL_AXI_MFCC_CNN')
 record=dict(passed=True,scope='actual RTL, AXI model programming and readback, local PCM injection, 128 overlapping MFCC frames, trained CNN and 64 exact embedding words; not microphone acoustics or identity accuracy',fixture=file.relative_to(ROOT).as_posix(),model_sha256=sha(a.model),pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),model_fnv32=value,source_sha256={f.relative_to(ROOT).as_posix():sha(f) for f in sources})
 (ROOT/'output/speaker_cnn/integration_sim.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:v for k,v in record.items() if k!='source_sha256'}),flush=True)
if __name__=='__main__':main()
