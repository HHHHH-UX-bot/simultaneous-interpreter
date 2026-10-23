"""Load this desktop project's verified Z7-Nano bit/ELF and resident touch host.

Runtime secrets are accepted by configure_cloud_runtime only in process memory.
No boot media, flash or persistent Wi-Fi/API credentials are written.
"""
import argparse,hashlib,json,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--app-dir',type=Path,default=ROOT/'output/remote_display/microphone')
    p.add_argument('--xsct',type=Path,default=Path('C:/Xilinx/SDK/2018.3/bin/xsct.bat'))
    p.add_argument('--configure',action='store_true');p.add_argument('--env',action='store_true')
    p.add_argument('--skip-touch-host',action='store_true',help='automatic diagnostics: leave CPU1 stopped after full system reset')
    a=p.parse_args();app=a.app_dir.resolve();build=json.loads((app/'build.json').read_text())
    elf=app/Path(build['ELF']).name
    if build['Status']!='passed' or build['ELFSHA256']!=sha(elf):raise RuntimeError('Verified current application required')
    if any(sha(ROOT/f)!=h for f,h in build['InputSHA256'].items()):raise RuntimeError('Sources changed: rebuild the application first')
    platform=ROOT/('output/z7_nano/remote_display' if build.get('RemoteDisplay') else 'output/z7_nano/audio')
    proof=platform/'build.json' if build.get('RemoteDisplay') else ROOT/'output/z7_nano/audio_loopback/build.json'
    pl=json.loads(proof.read_text());bit=platform/'z7_nano_audio_top.bit'
    if pl['Status']!='passed' or pl['BitstreamSHA256']!=sha(bit):raise RuntimeError('Verified bitstream required')
    if bool(build.get('SpeakerCNN'))!=bool(pl.get('SpeakerCNN')):raise RuntimeError('Speaker firmware and PL platform must match')
    if build.get('SpeakerCNN') and pl.get('usr_access')!=0x5a374e35:raise RuntimeError('Verified speaker platform identity required')
    if any(sha(ROOT/item['File'])!=item['SHA256'] for item in pl['LogicHashes']):raise RuntimeError('PL inputs changed: rebuild first')
    init=next(platform.rglob('ps7_init.tcl'))
    if pl.get('PS7InitSHA256') and sha(init)!=pl['PS7InitSHA256']:raise RuntimeError('PS initialization hash mismatch')
    attempt=app/'loads'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ');attempt.mkdir(parents=True)
    result=attempt/'result.json'
    run=subprocess.run([str(a.xsct),str(ROOT/'scripts/z7_nano_audio.tcl'),'run','210241277120',str(bit),str(result),str(init),str(elf)],capture_output=True,cwd=ROOT)
    console=(run.stdout+run.stderr).decode('utf-8','replace');(attempt/'console.log').write_text(console,encoding='utf-8')
    print('\n'.join(console.splitlines()[-12:]),flush=True)
    if run.returncode or 'PASS_Z7_NANO_AUDIO_RUN' not in console:raise RuntimeError('Board load did not pass; see '+str(attempt/'console.log'))
    actual=json.loads(result.read_text())
    if actual['display_ok']!=1 or actual['cable_serial']!='210241277120':raise RuntimeError('Board display startup did not pass')
    (app/'load.json').write_text(json.dumps(dict(cable_serial='210241277120',result=result.relative_to(ROOT).as_posix(),result_sha256=sha(result),elf_sha256=sha(elf),bit_sha256=sha(bit),ps7_init_sha256=sha(init)),indent=2)+'\n')
    if not a.skip_touch_host:
        subprocess.run([sys.executable,str(ROOT/'scripts/run_cloud_touch.py'),'start','--app-dir',str(app),'--xsdb',str(a.xsct)],check=True,cwd=ROOT)
    if a.configure:
        cmd=[sys.executable,str(ROOT/'scripts/configure_cloud_runtime.py'),'--yes','--mode',build['Mode'],'--symbols',str(app/'symbols.log'),'--xsdb',str(a.xsct)]
        if a.env:cmd.append('--env')
        subprocess.run(cmd,check=True,cwd=ROOT)
    print('PASS_TTS_LOADED real microphone' if not build['Selftest'] else 'PASS_TTS_LOADED synthetic self-test')
if __name__=='__main__':main()
