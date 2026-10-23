"""Offline real-speech MFCC features using the hardware's exact integer math."""
import argparse,ctypes as ct,json,hashlib,time,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tmp/speaker_cnn/deps'))
import soundfile as sf
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--subset',choices=['train-clean-5','train-clean-100','dev-clean'],required=True);p.add_argument('--per-speaker',type=int,default=0);a=p.parse_args()
 root=ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech'/a.subset;assert root.is_dir()
 lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/mfcc_reference.dll'));fn=lib.speaker_mfcc_frames
 fn.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint32,ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)];fn.restype=ct.c_uint32
 files=sorted(root.rglob('*.flac'));counts={};selected=[]
 # Interleave chapters to avoid selecting a single recording for every speaker.
 speakers=sorted({int(f.parent.parent.name) for f in files})
 for speaker in speakers:
  chapters={}
  for f in files:
   if int(f.parent.parent.name)==speaker:chapters.setdefault(f.parent.name,[]).append(f)
  pending=list(chapters.values());ordered=[]
  while any(pending):
   for chapter in pending:
    if chapter:ordered.append(chapter.pop(0))
  selected.extend(ordered[:a.per_speaker] if a.per_speaker else ordered)
 features=[];energy=[];offsets=[0];labels=[];names=[];seconds=0;start=time.monotonic()
 for index,f in enumerate(selected):
  pcm,rate=sf.read(str(f),dtype='int16');assert rate==16000 and pcm.ndim==1
  pcm=np.ascontiguousarray(pcm);n=max(0,1+(len(pcm)-400)//160)
  if n<128:continue
  out=np.empty((n,16),np.int16);e=np.empty(n,np.uint32)
  assert fn(pcm.ctypes.data_as(ct.POINTER(ct.c_int16)),len(pcm),out.ctypes.data_as(ct.POINTER(ct.c_int16)),e.ctypes.data_as(ct.POINTER(ct.c_uint32)))==n
  features.append(out);energy.append(e);offsets.append(offsets[-1]+n);labels.append(int(f.parent.parent.name));names.append(f.name);seconds+=len(pcm)/16000
  if index%100==0:print(json.dumps({'subset':a.subset,'clips':index+1,'total':len(selected),'frames':offsets[-1],'elapsed_s':round(time.monotonic()-start,1)}),flush=True)
 output=ROOT/'tmp/speaker_cnn/features';output.mkdir(exist_ok=True)
 name=a.subset+('-cap'+str(a.per_speaker) if a.per_speaker else '')
 target=output/(name+'.npz')
 np.savez_compressed(target,mfcc=np.concatenate(features),energy=np.concatenate(energy),offsets=np.array(offsets,np.int64),speakers=np.array(labels,np.int32),names=np.array(names))
 record=dict(subset=a.subset,clips=len(labels),speakers=len(set(labels)),audio_seconds=seconds,frames=offsets[-1],per_speaker_cap=a.per_speaker,cache_sha256=sha(target),feature_source_sha256=sha(ROOT/'software/speaker_cnn/speaker_mfcc.cpp'),tables_sha256=sha(ROOT/'software/speaker_cnn/speaker_tables.h'),speaker_ids=sorted(set(labels)),elapsed_seconds=round(time.monotonic()-start,2))
 (ROOT/'output/speaker_cnn'/('features_'+name+'.json')).write_text(json.dumps(record,indent=2)+'\n')
 print('SPEAKER_FEATURES_READY '+json.dumps(record),flush=True)
if __name__=='__main__':main()
