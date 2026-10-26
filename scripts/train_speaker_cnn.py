"""Train only on public audio, validate on disjoint speakers, no cloud calls."""
import argparse,json,math,time,hashlib,random
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F
from speaker_cnn_model import SpeakerCNN
ROOT=Path(__file__).resolve().parents[1]
torch.set_num_threads(4);torch.set_num_interop_threads(1)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
class Features:
 def __init__(self,path):
  d=np.load(path);self.x=d['mfcc'];self.e=d['energy'];self.offset=d['offsets'];self.speakers=d['speakers'];self.names=d['names']
  self.ids=sorted(set(map(int,self.speakers)));self.by_id={k:np.flatnonzero(self.speakers==k) for k in self.ids}
 def crop(self,i,rng):
  a,b=self.offset[i:i+2]
  for _ in range(20):
   start=int(rng.integers(a,b-128+1))
   if np.count_nonzero(self.e[start:start+128]>2000)>=64:break
  return self.x[start:start+128].astype(np.int32)
def gains(train):
 square=np.zeros(16,np.float64);n=0
 for i in range(len(train.speakers)):
  a,b=train.offset[i:i+2];x=train.x[a:b:8].astype(np.float64);x-=x.mean(0);square+=(x*x).sum(0);n+=len(x)
 std=np.maximum(np.sqrt(square/n),1)
 return np.rint(32/std*65536).astype(np.int64)
def input_batch(crops,gain):
 a=np.stack(crops).astype(np.int64);a-=a.sum(1,keepdims=True)>>7
 a=np.clip((a*gain)>>16,-127,127).astype(np.float32)/32
 return torch.from_numpy(a[:,None])
def eval_set(data,ids,gain):
 rng=np.random.default_rng(928173);crops=[];labels=[]
 for speaker in ids:
  choices=data.by_id[speaker][:8]
  assert len(choices)>=8
  for i in choices:crops.append(data.crop(i,rng));labels.append(speaker)
 return input_batch(crops,gain),np.array(labels)
@torch.no_grad()
def evaluate(net,x,labels,ids):
 net.eval();emb=np.concatenate([net.embed(v).numpy() for v in x.split(64)])
 prototypes=[];queries=[];targets=[]
 for speaker in ids:
  rows=emb[labels==speaker];p=rows[:3].mean(0);p/=np.linalg.norm(p)
  prototypes.append(p);queries.extend(rows[3:]);targets.extend([speaker]*len(rows[3:]))
 refs=np.array(prototypes);q=np.array(queries);targets=np.array(targets);scores=q@refs.T;truth=np.array([ids.index(int(k)) for k in targets])
 genuine=scores[np.arange(len(q)),truth];negative=scores[np.arange(len(q))[:,None],np.array([[i for i in range(len(ids)) if i!=t] for t in truth])]
 neg=negative.ravel();best_eer=1.;tau=0.
 for threshold in np.linspace(-.25,1.,501):
  frr=float(np.mean(genuine<threshold));far=float(np.mean(neg>=threshold))
  if max(frr,far)<best_eer:best_eer=max(frr,far);tau=float(threshold)
 # Four enrolled identities versus other, unseen identities, without test access.
 groups=[]
 for base in range(0,len(ids),4):
  selected=ids[base:base+4]
  if len(selected)<4:continue
  cols=[ids.index(s) for s in selected];v=scores[:,cols];order=np.argsort(v,axis=1);top=v[np.arange(len(v)),order[:,-1]];margin=top-v[np.arange(len(v)),order[:,-2]]
  known=np.isin(targets,selected);correct=np.array([selected[int(i)]==int(t) for i,t in zip(order[:,-1],targets)])
  groups.append((top,margin,known,correct))
 operating=None
 for threshold in np.linspace(.0,.99,100):
  for gap in (0.,.02,.04,.06,.08,.10):
   frr=[];far=[];wrong=[]
   for top,margin,known,correct in groups:
    accepted=(top>=threshold)&(margin>=gap)
    frr.append(np.mean(~(accepted&correct)[known]));far.append(np.mean(accepted[~known]));wrong.append(np.mean((accepted&~correct)[known]))
   error=(float(np.mean(frr))+float(np.mean(far)))/2
   if operating is None or error<operating['balanced_error']:
    operating=dict(threshold=float(threshold),margin=gap,balanced_error=error,false_reject_known=float(np.mean(frr)),false_accept_unknown=float(np.mean(far)),wrong_identity_accept=float(np.mean(wrong)))
 return dict(speakers=len(ids),queries=len(q),identification_top1=float(np.mean(np.argmax(scores,axis=1)==truth)),verification_eer=best_eer,verification_threshold=tau,open_set_four_profiles=operating)
def main():
 p=argparse.ArgumentParser();p.add_argument('--train',default='train-clean-5');p.add_argument('--epochs',type=int,default=40);p.add_argument('--windows',type=int,default=4096);p.add_argument('--run',default='pilot');p.add_argument('--resume',type=Path);a=p.parse_args()
 random.seed(47182);np.random.seed(47182);torch.manual_seed(47182);rng=np.random.default_rng(47182)
 base=ROOT/'tmp/speaker_cnn/features';train_file=base/(a.train+'.npz');dev_file=base/'dev-clean.npz';train=Features(train_file);dev=Features(dev_file)
 assert not set(train.ids)&set(dev.ids)
 # Split by identity. The final test half is never evaluated by this training command.
 ordered=sorted(dev.ids,key=lambda s:hashlib.sha256(('speaker-test-split-v1:'+str(s)).encode()).hexdigest())
 val_ids=ordered[:20];test_ids=ordered[20:];gain=gains(train);xval,yval=eval_set(dev,val_ids,gain)
 net=SpeakerCNN(len(train.ids))
 if a.resume:
  checkpoint=torch.load(a.resume,map_location='cpu',weights_only=False)
  encoder={k:v for k,v in checkpoint['state_dict'].items() if k!='classifier'};net.load_state_dict(encoder,strict=False)
 optimizer=torch.optim.AdamW(net.parameters(),lr=.002,weight_decay=.0001)
 output=ROOT/'output/speaker_cnn'/a.run;output.mkdir(parents=True,exist_ok=True)
 metadata=dict(train_cache_sha256=sha(train_file),dev_cache_sha256=sha(dev_file),train_speakers=train.ids,validation_speakers=val_ids,test_speakers_unseen_and_unmeasured=test_ids,gain_q16=gain.tolist(),architecture='128x16, Conv3x3 stride2 channels16/32/64, mean32, FC64',parameters=sum(v.numel() for k,v in net.named_parameters() if k!='classifier'),runtime_cloud_calls=0)
 (output/'training_manifest.json').write_text(json.dumps(metadata,indent=2)+'\n');best=2.;history=[];start=time.monotonic()
 for epoch in range(a.epochs):
  net.train();net.quantized=epoch>=max(2,a.epochs//3);loss_sum=0.;right=0;count=0
  for at in range(0,a.windows,64):
   batch=min(64,a.windows-at);classes=rng.integers(0,len(train.ids),batch);crops=[train.crop(int(rng.choice(train.by_id[train.ids[int(c)]])),rng) for c in classes]
   x=input_batch(crops,gain);y=torch.from_numpy(classes.astype(np.int64));optimizer.zero_grad(set_to_none=True)
   prediction=net(x,y);loss=F.cross_entropy(prediction,y);loss.backward();torch.nn.utils.clip_grad_norm_(net.parameters(),5);optimizer.step()
   loss_sum+=float(loss.detach())*batch;right+=int((prediction.argmax(1)==y).sum());count+=batch
  metrics=evaluate(net,xval,yval,val_ids);row=dict(epoch=epoch+1,qat=net.quantized,loss=loss_sum/count,training_accuracy=right/count,elapsed_s=round(time.monotonic()-start,1),validation=metrics)
  history.append(row);(output/'training_history.json').write_text(json.dumps(history,indent=2)+'\n')
  # Only an actual quantized encoder is eligible for hardware export.
  score=metrics['open_set_four_profiles']['balanced_error']
  if net.quantized and score<best:
   best=score;torch.save(dict(state_dict=net.state_dict(),manifest=metadata,validation=metrics,epoch=epoch+1,quantized=True),output/'best.pt')
  torch.save(dict(state_dict=net.state_dict(),manifest=metadata,validation=metrics,epoch=epoch+1,quantized=net.quantized),output/'last.pt')
  print(json.dumps(row),flush=True)
 print('TRAINING_COMPLETED '+a.run,flush=True)
if __name__=='__main__':main()
