"""Generate deterministic integer MFCC ROMs for host and FPGA."""
import math,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'software/speaker_cnn'
def q15(x):return max(-32768,min(32767,round(x*32768)))
def array(name,values,ctype='int16_t'):
 return 'static const '+ctype+' '+name+'['+str(len(values))+']={\n'+''.join(' '+','.join(str(x) for x in values[i:i+16])+',\n' for i in range(0,len(values),16))+'};\n'
def main():
 window=[q15(.54-.46*math.cos(2*math.pi*n/399)) for n in range(400)]
 cos=[q15(math.cos(2*math.pi*k/512)) for k in range(256)]
 sin=[q15(math.sin(2*math.pi*k/512)) for k in range(256)]
 hz=lambda m:700*(10**(m/2595)-1)
 edges=[int(512*hz(i*(2595*math.log10(1+8000/700))/33)/16000) for i in range(34)]
 edges[0]=0;edges[-1]=256
 assert all(a<b for a,b in zip(edges,edges[1:]))
 bins=[];weights=[];offset=[0]
 for l,c,r in zip(edges,edges[1:],edges[2:]):
  for k in range(l,r+1):
   value=(k-l)/(c-l) if k<=c else (r-k)/(r-c)
   if value>0:bins.append(k);weights.append(q15(value))
  offset.append(len(bins))
 dct=[q15(math.cos(math.pi*d*(b+.5)/32)*math.sqrt((1 if d==0 else 2)/32)) for d in range(16) for b in range(32)]
 data='#ifndef SPEAKER_TABLES_H\n#define SPEAKER_TABLES_H\n#include <stdint.h>\n'
 for name,values,ctype in [('sp_window',window,'int16_t'),('sp_cos',cos,'int16_t'),('sp_sin',sin,'int16_t'),('sp_mel_offsets',offset,'uint16_t'),('sp_mel_bins',bins,'uint16_t'),('sp_mel_weights',weights,'int16_t'),('sp_dct',dct,'int16_t')]:
  data+=array(name,values,ctype)+'\n'
 data+='#endif\n';(OUT/'speaker_tables.h').write_text(data,encoding='ascii')
 record=dict(sample_rate=16000,frame=400,hop=160,fft=512,mel_bands=32,mfcc=16,mel_edges=edges,sparse_mel_terms=len(bins),window='Hamming',log='Q8 base-2 integer mantissa approximation',dct='orthonormal type II, Q15, final /4',sha256=hashlib.sha256(data.encode()).hexdigest())
 (ROOT/'output/speaker_cnn/feature_spec.json').write_text(json.dumps(record,indent=2)+'\n')
 print('GENERATED_SPEAKER_MFCC_TABLES',len(bins))
if __name__=='__main__':main()
