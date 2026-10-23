"""Export trained weights and verify integer hardware C against QAT PyTorch."""
import argparse,ctypes as ct,hashlib,json,sys
from pathlib import Path
import numpy as np
import torch
from speaker_cnn_model import SpeakerCNN,weight_quantization
from train_speaker_cnn import Features,input_batch
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--checkpoint',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
 checkpoint=torch.load(a.checkpoint,map_location='cpu',weights_only=False);assert checkpoint['quantized']
 manifest=checkpoint['manifest']
 if manifest.get('model_variant')=='bn_folded_v1':
  from speaker_cnn_bn_model import SpeakerCNNBN
  net=SpeakerCNNBN(len(manifest['train_speakers']))
 else:net=SpeakerCNN(len(manifest['train_speakers']))
 net.load_state_dict(checkpoint['state_dict']);net.quantized=True;net.eval()
 words=np.zeros(8192,np.uint32);words[:3]=[0x314b5053,1,7076];words[16:32]=np.array(manifest['gain_q16'],np.int32).view(np.uint32)
 shifts=[]
 for layer,bias_base,weight_base in [(net.conv1,32,256),(net.conv2,48,292),(net.conv3,80,1444),(net.fc,144,6052)]:
  w,b=net.deployment_params(layer) if hasattr(net,'deployment_params') else (layer.weight,layer.bias)
  _,shift=weight_quantization(w);shifts.append(shift);scale=2.**(-shift)
  weight=torch.round(w.detach()/scale).clamp(-127,127).to(torch.int8).numpy().reshape(-1)
  bias=torch.round(b.detach()/(scale/32)).to(torch.int32).numpy()
  assert np.max(np.abs(bias.astype(np.int64)))<2**23
  words[bias_base:bias_base+len(bias)]=bias.view(np.uint32)
  words[weight_base:weight_base+len(weight)//4]=np.frombuffer(weight.tobytes(),dtype='<u4')
 words[4]=sum(s<<(i*8) for i,s in enumerate(shifts))
 identity=hashlib.sha256(words.tobytes()).hexdigest();words[3]=int(identity[:8],16)
 out=a.output_dir;out.mkdir(parents=True,exist_ok=True);blob=out/'model.bin';blob.write_bytes(words.astype('<u4').tobytes())
 # Full trained-parameter bytes are local, not public utterances or registry data.
 data=Features(ROOT/'tmp/speaker_cnn/features/dev-clean.npz');ids=manifest['validation_speakers'];rng=np.random.default_rng(121582)
 crops=[data.crop(int(i),rng) for s in ids[:4] for i in data.by_id[s][:4]]
 crop=np.array(crops,np.int16);x=input_batch(crops,np.array(manifest['gain_q16'],np.int64))
 with torch.no_grad():expected=net.embed(x).numpy()
 lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/cnn_reference.dll'));fn=lib.speaker_cnn_core
 fn.argtypes=[ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
 raw=[]
 for v in crop:
  result=np.zeros(64,np.int16);status=ct.c_uint32()
  fn(v.ctypes.data_as(ct.POINTER(ct.c_int16)),words.ctypes.data_as(ct.POINTER(ct.c_uint32)),result.ctypes.data_as(ct.POINTER(ct.c_int16)),ct.byref(status))
  assert status.value==0;raw.append(result)
 raw=np.array(raw);got=raw.astype(np.float32);got/=np.maximum(np.linalg.norm(got,axis=1,keepdims=True),1e-9)
 error=float(np.max(np.abs(got-expected)));cosine=np.sum(got*expected,axis=1)
 assert error<1e-5,(error,cosine.tolist())
 np.savez(out/'integer_vectors.npz',features=crop,embeddings=raw)
 lines=['#include <stdint.h>','static const uint32_t test_model[8192]={'+','.join(str(int(v))+'u' for v in words)+'};',
        'static const int16_t test_cnn_input[2048]={'+','.join(map(str,crop[0].ravel().tolist()))+'};',
        'static const int16_t test_cnn_embedding[64]={'+','.join(map(str,raw[0].tolist()))+'};']
 (ROOT/'tests/speaker_cnn/cnn_vectors.h').write_text('\n'.join(lines)+'\n')
 record=dict(checkpoint_sha256=hashlib.sha256(a.checkpoint.read_bytes()).hexdigest(),model_sha256=hashlib.sha256(blob.read_bytes()).hexdigest(),model_identity=identity,bytes=blob.stat().st_size,used_words=7076,weight_shifts=shifts,validation_vectors=len(crop),max_normalized_embedding_difference=error,min_cosine=float(cosine.min()),validation=checkpoint['validation'],test_speakers_evaluated=False,source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['software/speaker_cnn/speaker_cnn.cpp','scripts/speaker_cnn_model.py']})
 (out/'export.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
if __name__=='__main__':main()
