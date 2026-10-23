"""Independent integer MFCC reference versus the native hardware C definition."""
import ctypes as ct,json,sys,hashlib,math,re
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tmp/speaker_cnn/deps'))
import soundfile as sf
spec=json.loads((ROOT/'output/speaker_cnn/feature_spec.json').read_text())
text=(ROOT/'software/speaker_cnn/speaker_tables.h').read_text()
def table(name):return np.array([int(x) for x in re.search(name+r'\[\d+\]=\{([^}]+)',text,re.S)[1].split(',') if x.strip()],dtype=np.int64)
window,cos,sin,offset,bins,weight,dct=[table(n) for n in ['sp_window','sp_cos','sp_sin','sp_mel_offsets','sp_mel_bins','sp_mel_weights','sp_dct']]
reverse=np.array([int(f'{i:09b}'[::-1],2) for i in range(512)])
def trunc(n,d):return np.where(n>=0,n//d,-((-n)//d))
def reference(pcm):
 x=pcm.astype(np.int64);x-=int(trunc(x.sum(),400))
 energy=int((x*x).sum()//400)
 r=np.zeros(512,np.int64);r[:400]=x*window>>15;r=r[reverse];im=np.zeros(512,np.int64)
 for stage in range(1,10):
  length=1<<stage;half=length//2;a=(np.arange(0,512,length)[:,None]+np.arange(half)).ravel();b=a+half;k=np.tile(np.arange(half)*(512//length),512//length)
  ar,ai=r[a].copy(),im[a].copy();br,bi=r[b].copy(),im[b].copy()
  tr=(cos[k]*br+sin[k]*bi)>>15;ti=(cos[k]*bi-sin[k]*br)>>15
  r[a]=(ar+tr)>>1;im[a]=(ai+ti)>>1;r[b]=(ar-tr)>>1;im[b]=(ai-ti)>>1
  assert np.max(np.abs(r))<2**17 and np.max(np.abs(im))<2**17
 power=r[:257]**2+im[:257]**2;bands=[]
 for band in range(32):
  terms=slice(offset[band],offset[band+1]);v=int(1+((power[bins[terms]]*weight[terms])>>15).sum())
  exponent=v.bit_length()-1;mantissa=(v>>(exponent-8)) if exponent>=8 else v<<(8-exponent)
  bands.append(exponent*256+mantissa-256)
 values=trunc(dct.reshape(16,32)@np.array(bands),131072)
 return np.clip(values,-32768,32767).astype(np.int16),energy
lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/mfcc_reference.dll'));fn=lib.speaker_mfcc_frames
fn.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint32,ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)];fn.restype=ct.c_uint32
def native(samples):
 data=np.ascontiguousarray(samples,np.int16);count=max(0,1+(len(data)-400)//160);out=np.zeros((count,16),np.int16);e=np.zeros(count,np.uint32)
 n=fn(data.ctypes.data_as(ct.POINTER(ct.c_int16)),len(data),out.ctypes.data_as(ct.POINTER(ct.c_int16)),e.ctypes.data_as(ct.POINTER(ct.c_uint32)));assert n==count
 return out,e
def main():
 rng=np.random.default_rng(73531);n=np.arange(400)
 cases=[np.zeros(400,np.int16),np.full(400,12345,np.int16),np.where(n%2,32767,-32768).astype(np.int16),np.rint(20000*np.sin(2*np.pi*n*523/16000)+5000).astype(np.int16)]
 file=next((ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech/train-clean-5').rglob('*.flac'))
 with sf.SoundFile(file) as audio:
  audio.seek(audio.frames//2);real=audio.read(400,dtype='int16')
 assert len(real)==400;cases.append(real)
 cases.extend(rng.integers(-32768,32768,400,dtype=np.int16) for _ in range(100))
 for pcm in cases:
  expected,energy=reference(pcm);got,e=native(pcm)
  assert np.array_equal(got[0],expected),(expected,got[0]);assert e[0]==energy
 assert np.all(native(cases[0])[0]==0) and np.all(native(cases[1])[0]==0)
 continuous=np.concatenate(cases[:5]);got,e=native(continuous)
 for i in range(len(got)):
  expected,energy=reference(continuous[i*160:i*160+400]);assert np.array_equal(expected,got[i]) and e[i]==energy
 vectors=cases[:5]+cases[5:7];lines=['#include <stdint.h>','static const int16_t input_vectors[7][400]={']
 lines+=['{'+','.join(map(str,x.tolist()))+'},' for x in vectors];lines+=['};','static const int16_t expected_mfcc[7][16]={']
 lines+=['{'+','.join(map(str,reference(x)[0].tolist()))+'},' for x in vectors];lines+=['};','static const uint32_t expected_energy[7]={'+','.join(str(reference(x)[1])+'u' for x in vectors)+'};']
 (ROOT/'tests/speaker_cnn/mfcc_vectors.h').write_text('\n'.join(lines)+'\n')
 record={'passed':True,'independent_frames':len(cases),'continuous_frames':len(got),'max_integer_difference':0,'checks':['silence','DC removal','full scale Nyquist','sinusoid','public real speech','100 random frames','overlap/hop ordering'],'source_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ['software/speaker_cnn/speaker_mfcc.cpp','software/speaker_cnn/speaker_tables.h']}}
 (ROOT/'output/speaker_cnn/mfcc_numeric.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
if __name__=='__main__':main()
