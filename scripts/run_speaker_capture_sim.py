"""Validate real RTL resampling versus independent integer convolution."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--xsim-bin',type=Path,default=Path('C:/Xilinx/Vivado/2018.3/bin'),help='directory holding xvlog/xelab/xsim .bat');a=p.parse_args()
 work=ROOT/'tmp/speaker_cnn/capture_sim';work.mkdir(parents=True,exist_ok=True)
 taps=np.array(json.loads((ROOT/'output/speaker_cnn/resampler.json').read_text())['taps'],np.int64)
 n=np.arange(2049);rng=np.random.default_rng(73318)
 pcm=np.clip(np.rint(8000+12000*np.sin(2*np.pi*n*523/48000)+5000*np.sin(2*np.pi*n*10000/48000))+rng.integers(-500,501,len(n)),-32768,32767).astype(np.int16)
 expected=np.clip(np.convolve(pcm.astype(np.int64),taps)[:len(pcm)]>>15,-32768,32767)[2::3]
 for name,data in [('input.mem',pcm),('expected.mem',expected)]:
  (work/name).write_text(''.join(f'{int(x)&65535:04x}\n' for x in data))
 gain=lambda hz:abs(np.sum(taps*np.exp(-2j*np.pi*hz*np.arange(len(taps))/48000))/32768)
 assert .98<gain(1000)<1.02 and gain(10000)<.01
 tool=a.xsim_bin;scratch=work/'tool_tmp';scratch.mkdir(exist_ok=True)
 env=os.environ.copy();env.update(TEMP=str(scratch),TMP=str(scratch))
 def run(name,args,marker=None):
  console=work/(name+'_console.log')
  with console.open('w') as f:subprocess.run([str(x) for x in args],cwd=work,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
  if marker:assert marker in console.read_text(encoding='utf-8',errors='replace')
 source=[ROOT/'rtl/speaker_cnn/speaker_capture.v',ROOT/'tests/speaker_cnn/speaker_capture_tb.v']
 run('xvlog',[tool/'xvlog.bat','-i',ROOT/'rtl/speaker_cnn',*source])
 run('xelab',[tool/'xelab.bat','work.speaker_capture_tb','-s','speaker_capture_tb','--debug','off','--mt','off'])
 run('xsim',[tool/'xsim.bat','speaker_capture_tb','-runall'],'PASS_SPEAKER_FIR_CAPTURE')
 record=dict(passed=True,input_samples=len(pcm),output_samples=len(expected),integer_difference=0,gain_1khz=gain(1000),gain_10khz=gain(10000),source_sha256={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in [*source,ROOT/'rtl/speaker_cnn/speaker_fir_rom.vh']})
 (ROOT/'output/speaker_cnn/capture_sim.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
if __name__=='__main__':main()
