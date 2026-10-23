"""48 -> 16 kHz anti-alias FIR, 63 Q15 taps, independent of cloud audio path."""
import math,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 values=[]
 for n in range(63):
  t=n-31;v=2*7000/48000 if t==0 else math.sin(2*math.pi*7000/48000*t)/(math.pi*t)
  values.append(v*(.54-.46*math.cos(2*math.pi*n/62)))
 scale=sum(values);q=[round(v/scale*32768) for v in values];q[31]+=32768-sum(q)
 lines=['function signed [15:0] fir_coeff;',' input [5:0] index;',' begin',' case(index)']
 lines += [" 6'd%d: fir_coeff=%s16'sd%d;"%(i,'-' if v<0 else '',abs(v)) for i,v in enumerate(q)]
 lines+=[" default: fir_coeff=16'sd0;"," endcase"," end","endfunction"]
 (ROOT/'rtl/speaker_cnn/speaker_fir_rom.vh').write_text('\n'.join(lines)+'\n')
 (ROOT/'output/speaker_cnn/resampler.json').write_text(json.dumps(dict(input_hz=48000,output_hz=16000,taps=q,q_fraction_bits=15,cutoff_hz=7000,decimation=3),indent=2)+'\n')
 print('GENERATED_SPEAKER_FIR_Q15',sum(q))
if __name__=='__main__':main()
