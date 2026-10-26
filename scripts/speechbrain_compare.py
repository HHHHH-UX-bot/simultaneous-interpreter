"""Optional PC-only ECAPA speaker verification / named enrollment.
The board's integer MFCC prototype is independent of this PyTorch program.
"""
import argparse,hashlib,json,os,time,urllib.request,wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/local_ai/speechbrain';OUT.mkdir(parents=True,exist_ok=True)
os.environ.setdefault('HF_HOME',str(OUT/'hf_cache'))
os.environ.setdefault('TORCH_HOME',str(OUT/'torch_cache'))

def prepare():
    model=OUT/'model';model.mkdir(exist_ok=True);manifest=model/'manifest.json'
    if manifest.exists():
        d=json.loads(manifest.read_text())
        if all((model/n).is_file() and hashlib.sha256((model/n).read_bytes()).hexdigest()==h for n,h in d['sha256'].items()):return model
    with urllib.request.urlopen('https://huggingface.co/api/models/speechbrain/spkrec-ecapa-voxceleb',timeout=30) as response:d=json.load(response)
    revision=d['sha'];hashes={}
    for name in ('hyperparams.yaml','embedding_model.ckpt','mean_var_norm_emb.ckpt','classifier.ckpt','label_encoder.txt','README.md'):
        url='https://huggingface.co/speechbrain/spkrec-ecapa-voxceleb/resolve/'+revision+'/'+name
        with urllib.request.urlopen(url,timeout=120) as response:data=response.read()
        (model/name).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
        print('MODEL_FILE_READY',name,flush=True)
    manifest.write_text(json.dumps(dict(repository='speechbrain/spkrec-ecapa-voxceleb',revision=revision,sha256=hashes),indent=2))
    return model

def load():
    import torch
    from speechbrain.inference.speaker import SpeakerRecognition
    from speechbrain.utils.fetching import LocalStrategy
    torch.set_num_threads(4)
    model=prepare()
    return SpeakerRecognition.from_hparams(source=str(model),savedir=str(OUT/'inference'),
        overrides={'pretrained_path':str(model).replace('\\','/')},run_opts={'device':'cpu'},local_strategy=LocalStrategy.COPY)

def audio(path):
    import torch,numpy as np
    with wave.open(str(path),'rb') as w:
        if w.getnchannels()!=1 or w.getsampwidth()!=2:raise ValueError('Use mono 16-bit PCM WAV')
        rate=w.getframerate();data=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float32)/32768
    if not 0<len(data)<=rate*30:raise ValueError('Audio must be 0..30 seconds')
    if rate!=16000:data=np.interp(np.arange(round(len(data)*16000/rate))*rate/16000,np.arange(len(data)),data).astype(np.float32)
    return torch.from_numpy(data).unsqueeze(0)

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=('verify','enroll','identify','selftest'))
    p.add_argument('--reference',type=Path);p.add_argument('--audio',type=Path);p.add_argument('--name')
    p.add_argument('--threshold',type=float,default=.65);p.add_argument('--margin',type=float,default=.08)
    a=p.parse_args();started=time.monotonic();model=load()
    import torch
    registry=OUT/'registry.json'
    with torch.no_grad():
        if a.action=='selftest':
            x=audio(ROOT/'output/local_ai/voices/zh_5_2.wav');y=audio(ROOT/'output/local_ai/heldout/zh_5_1.wav');z=audio(ROOT/'output/local_ai/voices/en_7_2.wav')
            same,_=model.verify_batch(x,y);different,_=model.verify_batch(x,z)
            result=dict(same_synthetic_voice=float(same[0]),different_synthetic_voice=float(different[0]),seconds=time.monotonic()-started,
                scope='PC ECAPA with two synthetic voices; excludes human identity accuracy and board deployment')
            result['status']='passed' if result['same_synthetic_voice']>result['different_synthetic_voice']+.15 else 'failed'
            (OUT/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));assert result['status']=='passed'
        elif a.action=='verify':
            if not a.reference or not a.audio:p.error('verify requires --reference and --audio')
            score,pred=model.verify_batch(audio(a.reference),audio(a.audio),threshold=a.threshold)
            print(json.dumps(dict(score=float(score[0]),same=bool(pred[0]),threshold=a.threshold)))
        else:
            if not a.audio:p.error('--audio is required')
            emb=model.encode_batch(audio(a.audio)).reshape(-1);emb=torch.nn.functional.normalize(emb,dim=0)
            entries=json.loads(registry.read_text(encoding='utf-8')) if registry.exists() else {}
            if a.action=='enroll':
                if not a.name:p.error('enroll requires --name')
                entries[a.name]=emb.tolist();registry.write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
                print(json.dumps(dict(enrolled=a.name,dimensions=len(emb)),ensure_ascii=False))
            else:
                scores=sorted(((float(torch.dot(emb,torch.tensor(v))),name) for name,v in entries.items()),reverse=True)
                best=scores[0] if scores else (0.0,'unknown');second=scores[1][0] if len(scores)>1 else -1.0
                identity=best[1] if best[0]>=a.threshold and best[0]-second>=a.margin else 'unknown'
                print(json.dumps(dict(identity=identity,score=best[0],threshold=a.threshold,margin=a.margin),ensure_ascii=False))
if __name__=='__main__':main()
