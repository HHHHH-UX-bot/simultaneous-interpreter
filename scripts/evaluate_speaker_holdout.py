"""One final, identity-disjoint test of exported integer weights, fixed thresholds."""
import ctypes as ct,hashlib,json
from pathlib import Path
import numpy as np
from train_speaker_cnn import Features
ROOT=Path(__file__).resolve().parents[1]
def main():
 folder=ROOT/'output/speaker_cnn/full100_bn';manifest=json.loads((folder/'training_manifest.json').read_text())
 ids=manifest['test_speakers_unseen_and_unmeasured'];assert len(ids)==20
 assert not set(ids)&set(manifest['train_speakers']+manifest['validation_speakers'])
 data=Features(ROOT/'tmp/speaker_cnn/features/dev-clean.npz');rng=np.random.default_rng(928173)
 model=np.fromfile(folder/'export/model.bin',dtype='<u4');lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/cnn_reference.dll'));fn=lib.speaker_cnn_core
 fn.argtypes=[ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
 embeddings=[];labels=[]
 for speaker in ids:
  for clip in data.by_id[speaker][:8]:
   features=np.ascontiguousarray(data.crop(int(clip),rng),np.int16);out=np.zeros(64,np.int16);status=ct.c_uint32()
   fn(features.ctypes.data_as(ct.POINTER(ct.c_int16)),model.ctypes.data_as(ct.POINTER(ct.c_uint32)),out.ctypes.data_as(ct.POINTER(ct.c_int16)),ct.byref(status));assert status.value==0
   v=out.astype(np.float64);v/=max(np.linalg.norm(v),1e-12);embeddings.append(v);labels.append(speaker)
 embeddings=np.array(embeddings);labels=np.array(labels);refs=[];queries=[];targets=[]
 for speaker in ids:
  rows=embeddings[labels==speaker];v=rows[:3].mean(0);v/=np.linalg.norm(v);refs.append(v);queries.extend(rows[3:]);targets.extend([speaker]*5)
 scores=np.array(queries)@np.array(refs).T;targets=np.array(targets);truth=np.array([ids.index(int(k)) for k in targets])
 threshold=.44;gap=.06;known_ok=unknown_ok=known_total=unknown_total=wrong=0
 for base in range(0,20,4):
  selected=ids[base:base+4];v=scores[:,base:base+4];order=np.argsort(v,axis=1);top=v[np.arange(100),order[:,-1]];margin=top-v[np.arange(100),order[:,-2]]
  known=np.isin(targets,selected);correct=np.array([selected[int(i)]==int(t) for i,t in zip(order[:,-1],targets)]);accepted=(top>=threshold)&(margin>=gap)
  known_ok+=int(np.sum((accepted&correct)[known]));unknown_ok+=int(np.sum((~accepted)[~known]));known_total+=int(known.sum());unknown_total+=int((~known).sum());wrong+=int(np.sum((accepted&~correct)[known]))
 record=dict(scope='Public LibriSpeech held-out speakers, exact integer CNN, three separate enrollment utterances, single 1.28 s query; no microphone/Chinese/person-authentication claim',model_sha256=hashlib.sha256((folder/'export/model.bin').read_bytes()).hexdigest(),test_speakers=ids,threshold=threshold,margin=gap,thresholds_fixed_from_validation=True,identification_top1=float(np.mean(scores.argmax(1)==truth)),known_queries=known_total,unknown_queries=unknown_total,false_reject_known=1-known_ok/known_total,false_accept_unknown=1-unknown_ok/unknown_total,wrong_identity_accept=wrong/known_total,source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
 (ROOT/'output/speaker_cnn/holdout.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record),flush=True)
if __name__=='__main__':main()
