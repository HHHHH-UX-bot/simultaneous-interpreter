"""Build the new local speaker platform only after component RTL checks pass."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
import run_audio_loopback as audio
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8'))
def main():
 p=argparse.ArgumentParser();p.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/Vivado/2018.3/bin/vivado.bat'));a=p.parse_args()
 for name in ('integration_sim','capture_sim'):
  record=read(ROOT/'output/speaker_cnn'/(name+'.json'));assert record['passed']
  assert all(sha(ROOT/n)==h for n,h in record['source_sha256'].items()),'RTL test sources changed'
 hls=read(ROOT/'output/speaker_cnn/hls_validation.json')
 for name,r in hls.items():
  assert r['rtl_cosim']=='passed' and sha(ROOT/'software/speaker_cnn'/('speaker_'+name+'.cpp'))==r['source_sha256']
  assert all(sha(ROOT/n)==h for n,h in r['rtl_files'].items())
 # Reuse the unchanged audio component tests with explicit matching source hashes.
 previous=ROOT/'output/z7_nano/audio_loopback/simulation.json'
 component=read(previous);assert component['Status']=='passed'
 audio.check(component['LogicHashes'],audio.RTL_INPUTS);audio.check(component['TestHashes'],audio.SIM_INPUTS)
 base=ROOT/'output/z7_nano/speaker_cnn';attempt=base/'attempts'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ');attempt.mkdir(parents=True)
 material=attempt/'rtl';material.mkdir()
 added=[];canonical=[]
 for core in ('mfcc','cnn'):
  folder=ROOT/'rtl/speaker_cnn'/('hls_'+core)
  for file in folder.iterdir():
   if file.suffix not in ('.v','.dat'):continue
   dst=material/file.name
   if file.suffix=='.v':
    text=file.read_text()
    text=re.sub(r'\$readmemh\("(\./)?([^"]+\.dat)"',lambda m:'$readmemh("'+(folder/m[2]).as_posix()+'"',text)
    dst.write_text(text);added.append(dst)
   else:shutil.copy2(file,dst)
   canonical.append(file.relative_to(ROOT).as_posix())
 (attempt/'speaker_sources.tcl').write_text('\n'.join('add_files {'+v.as_posix()+'}' for v in added)+'\n')
 inputs=audio.BUILD_INPUTS+['scripts/pl_ethernet_bd.tcl','hardware/z7_nano/pl_ethernet.xdc','hardware/z7_nano/pl_ethernet_clock.xdc','scripts/build_speaker_platform.py','scripts/collect_speaker_hls.py','rtl/speaker_cnn/speaker_accel.v','rtl/speaker_cnn/speaker_pipeline.v','rtl/speaker_cnn/speaker_capture.v','rtl/speaker_cnn/speaker_fir_rom.vh']+canonical
 config=list((ROOT/'output/z7_nano/config').glob('*'))
 inputs+=['output/z7_nano/config/'+f.name for f in config if f.is_file()]
 before=audio.hashes(inputs)
 (attempt/'build_inputs.json').write_text(json.dumps(before,indent=2)+'\n')
 scratch=attempt/'tool_tmp';scratch.mkdir();env=os.environ.copy();env.update(TEMP=str(scratch),TMP=str(scratch))
 console=attempt/'console.log';print('SPEAKER_VIVADO_BUILD '+str(attempt.relative_to(ROOT)),flush=True)
 # Vivado 2018.3 still enforces MAX_PATH for generated PS7 IP. A task-owned
 # temporary drive maps only this attempt; canonical sources remain unchanged.
 drive=next((letter+':' for letter in 'SRQPVU' if not Path(letter+':/').exists()),None)
 if drive is None:raise RuntimeError('No free short build drive')
 subprocess.run(['subst',drive,str(attempt)],check=True)
 try:
  with console.open('w') as out:
   result=subprocess.run([str(a.vivado),'-mode','batch','-notrace','-source',str(ROOT/'scripts/build_z7_nano_audio.tcl'),'-log',drive+'/vivado.log','-journal',drive+'/vivado.jou','-tclargs','build',drive+'/','speaker-ui'],cwd=drive+'/',env=env,stdout=out,stderr=subprocess.STDOUT)
 finally:subprocess.run(['subst',drive,'/d'],check=True)
 if result.returncode or 'PASS_Z7_NANO_AUDIO_BUILD' not in console.read_text(encoding='utf-8',errors='replace'):raise RuntimeError('Speaker platform build failed; inspect '+str(console))
 audio.check(before,inputs);d=read(attempt/'implementation.json');assert d['usr_access']==0x5a374e35 and d['setup_slack_ns']>=0 and d['hold_slack_ns']>=0
 ps=list((attempt/'vivado').rglob('ps7_init.tcl'));assert ps and len({sha(f) for f in ps})==1
 shutil.copy2(ps[0],attempt/'ps7_init.tcl')
 d.update(Status='passed',LogicHashes=before,BitstreamSHA256=sha(attempt/'z7_nano_audio_top.bit'),PS7InitSHA256=sha(attempt/'ps7_init.tcl'),EthernetBase='0x40e00000',EthernetMbps=100,SpeakerBase='0x43c40000',SpeakerCNN=True,attempt=attempt.relative_to(ROOT).as_posix(),AudioSimulationProvenance={'record':'output/z7_nano/audio_loopback/simulation.json','source_hashes_match':True})
 (attempt/'build.json').write_text(json.dumps(d,indent=2)+'\n')
 for target in (base,ROOT/'output/z7_nano/remote_display'):
  target.mkdir(parents=True,exist_ok=True)
  for name in ('z7_nano_audio_top.bit','ps7_init.tcl','build.json'):shutil.copy2(attempt/name,target/name)
 print('PASS_SPEAKER_PLATFORM_BUILD',flush=True)
if __name__=='__main__':main()
