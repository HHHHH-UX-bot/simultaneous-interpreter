"""Replay public PCM arrival metadata through both real C playback profiles.

No recorded voice, cloud credentials or hardware are used. This compares local
buffer behavior under identical arrivals; it does not measure acoustic delay.
"""
import argparse
import hashlib
import json
import statistics
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc',type=Path,required=True)
    p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,default=ROOT/'output/tts/latency/timing_replay')
    args=p.parse_args();source=args.metadata.resolve();folder=args.output_dir.resolve();folder.mkdir(parents=True,exist_ok=True)
    data=json.loads(source.read_text());events=[x for x in data['speech_timeline'] if x[1] in (1,2)]
    if not events or events[-1][1]!=2:raise RuntimeError('Complete response metadata required')
    payload=''.join(f'{ms} {kind} {n}\n' for ms,kind,n,_ in events).encode()
    results={}
    for name,flags in [('old',[]),('optimized',['-DZ7_LOW_LATENCY_PLAYBACK'])]:
        exe=ROOT/'tmp'/f'speech_timing_{name}.exe';exe.parent.mkdir(exist_ok=True)
        subprocess.run([str(args.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DZ7_TRANSLATE_AUDIO','-DZ7_LATENCY_PROBE',*flags,str(ROOT/'tests/speech_timing_replay_test.c'),'-o',str(exe)],check=True,capture_output=True)
        r=subprocess.run([str(exe)],input=payload,capture_output=True);d=json.loads(r.stdout)
        (folder/(name+'.json')).write_text(json.dumps(d,indent=2)+'\n')
        first=[];new=True
        for ms,kind,n,_ in events:
            if kind==1 and new:first.append(ms);new=False
            if kind==2:new=True
        starts=[x[0] for x in d['timeline'] if x[1]==3]
        if len(starts)!=len(first):raise RuntimeError('Unexpected rebuffer or missing response')
        waits=[s-f for s,f in zip(starts,first)]
        results[name]={k:v for k,v in d.items() if k!='timeline'}
        results[name].update(median_start_wait_ms=statistics.median(waits),max_start_wait_ms=max(waits),start_wait_ms=waits)
        if r.returncode:raise RuntimeError(f'{name} replay failed: '+str(results[name]))
    inputs=['tests/speech_timing_replay_test.c','software/network_ui/speech_playback.c','scripts/validate_speech_timing.py']
    record=dict(scope=__doc__.strip(),source=source.relative_to(ROOT).as_posix(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),results=results,source_inputs={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in inputs})
    (folder/'validation.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(results))

if __name__=='__main__':main()
