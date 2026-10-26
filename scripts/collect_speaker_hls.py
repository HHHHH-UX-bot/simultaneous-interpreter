"""Collect only RTL that passed C/RTL co-simulation; preserve ROM file hashes."""
import hashlib,json,shutil,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
 record={}
 for core in ('mfcc','cnn'):
  log=ROOT/'tmp/speaker_cnn'/(core+'_cosim_console.log')
  assert 'C/RTL co-simulation finished: PASS' in log.read_text(encoding='utf-8')
  report=ROOT/'output/speaker_cnn'/('hls_'+core)/'solution/sim/report'/('speaker_'+core+'_core_cosim.rpt')
  values=re.search(r'\|\s*Verilog\|\s*Pass\|\s*(\d+)\|\s*(\d+)\|\s*(\d+)',report.read_text())
  assert values
  source=ROOT/'output/speaker_cnn'/('hls_'+core)/'solution/syn/verilog'
  target=ROOT/'rtl/speaker_cnn'/('hls_'+core);target.mkdir(exist_ok=True)
  files={}
  for f in source.iterdir():
   if f.suffix in ('.v','.dat'):
    shutil.copy2(f,target/f.name);files[(target/f.name).relative_to(ROOT).as_posix()]=sha(target/f.name)
  record[core]=dict(rtl_cosim='passed',min_cycles=int(values[1]),max_cycles=int(values[3]),clock_period_ns=20,latency_ms=int(values[3])/50000,source_sha256=sha(ROOT/'software/speaker_cnn'/('speaker_'+core+'.cpp')),rtl_files=files)
 (ROOT/'output/speaker_cnn/hls_validation.json').write_text(json.dumps(record,indent=2)+'\n')
 print(json.dumps({k:{n:v for n,v in x.items() if n!='rtl_files'} for k,x in record.items()}))
if __name__=='__main__':main()
