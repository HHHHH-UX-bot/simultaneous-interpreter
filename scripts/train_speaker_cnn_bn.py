"""Full-corpus speaker-disjoint training, with folded BN and margin warmup."""
import argparse,hashlib,json,math,random,time
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F
from train_speaker_cnn import Features,gains,input_batch,eval_set,evaluate,ROOT,sha
from speaker_cnn_bn_model import SpeakerCNNBN
def main():
 p=argparse.ArgumentParser();p.add_argument('--epochs',type=int,default=120);p.add_argument('--windows',type=int,default=16384);p.add_argument('--run',default='full100_bn');a=p.parse_args()
 random.seed(17402);np.random.seed(17402);torch.manual_seed(17402);rng=np.random.default_rng(17402)
 base=ROOT/'tmp/speaker_cnn/features';train_file=base/'train-clean-100.npz';dev_file=base/'dev-clean.npz'
 train=Features(train_file);dev=Features(dev_file);assert not set(train.ids)&set(dev.ids)
 ordered=sorted(dev.ids,key=lambda s:hashlib.sha256(('speaker-test-split-v1:'+str(s)).encode()).hexdigest())
 val_ids=ordered[:20];test_ids=ordered[20:];gain=gains(train);xval,yval=eval_set(dev,val_ids,gain)
 net=SpeakerCNNBN(len(train.ids));optimizer=torch.optim.AdamW(net.parameters(),lr=.001,weight_decay=.0001)
 out=ROOT/'output/speaker_cnn'/a.run;out.mkdir(parents=True,exist_ok=True)
 metadata=dict(model_variant='bn_folded_v1',train_cache_sha256=sha(train_file),dev_cache_sha256=sha(dev_file),train_speakers=train.ids,validation_speakers=val_ids,test_speakers_unseen_and_unmeasured=test_ids,gain_q16=gain.tolist(),architecture='128x16, Conv3x3 stride2 channels16/32/64, folded BN, mean32, FC64',runtime_cloud_calls=0,source_sha256={n:sha(ROOT/n) for n in ['scripts/train_speaker_cnn_bn.py','scripts/speaker_cnn_bn_model.py','scripts/speaker_cnn_model.py']})
 (out/'training_manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
 best=2.;history=[];start=time.monotonic()
 for epoch in range(a.epochs):
  net.train();net.quantized=epoch>=a.epochs//3;net.margin=.15*min(1.,max(0.,(epoch-4)/10))
  rate=.0001+.0009*(1+math.cos(math.pi*epoch/a.epochs))/2
  for group in optimizer.param_groups:group['lr']=rate
  total=right=count=0
  for at in range(0,a.windows,64):
   batch=min(64,a.windows-at);classes=rng.integers(0,len(train.ids),batch)
   x=input_batch([train.crop(int(rng.choice(train.by_id[train.ids[int(c)]])),rng) for c in classes],gain);y=torch.from_numpy(classes.astype(np.int64))
   optimizer.zero_grad(set_to_none=True);prediction=net(x,y);loss=F.cross_entropy(prediction,y)
   assert torch.isfinite(loss);loss.backward();torch.nn.utils.clip_grad_norm_(net.parameters(),5);optimizer.step()
   total+=float(loss.detach())*batch;right+=int((prediction.argmax(1)==y).sum());count+=batch
  metrics=evaluate(net,xval,yval,val_ids);row=dict(epoch=epoch+1,qat=net.quantized,margin=net.margin,loss=total/count,training_accuracy=right/count,elapsed_s=round(time.monotonic()-start,1),validation=metrics)
  history.append(row);(out/'training_history.json').write_text(json.dumps(history,indent=2)+'\n')
  checkpoint=dict(state_dict=net.state_dict(),manifest=metadata,validation=metrics,epoch=epoch+1,quantized=net.quantized)
  score=metrics['open_set_four_profiles']['balanced_error']
  if net.quantized and score<best:best=score;torch.save(checkpoint,out/'best.pt')
  torch.save(checkpoint,out/'last.pt');print(json.dumps(row),flush=True)
 print('TRAINING_COMPLETED '+a.run,flush=True)
if __name__=='__main__':main()
