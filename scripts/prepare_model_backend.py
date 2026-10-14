"""Explicitly acquire only selected public model assets at immutable revisions."""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'software/model_backend'))
import runtime
PROFILES=runtime.MODEL_PROFILES

def verify_file(path,item):
    size=path.stat().st_size
    if item.size is None or size!=item.size:raise RuntimeError('Model file size mismatch')
    sha256=hashlib.sha256();git=hashlib.sha1();git.update(f'blob {size}\0'.encode())
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):sha256.update(chunk);git.update(chunk)
    lfs=item.lfs
    upstream=getattr(lfs,'sha256',None) if lfs else None
    if lfs and isinstance(lfs,dict):upstream=lfs.get('sha256')
    if upstream:
        if sha256.hexdigest()!=upstream:raise RuntimeError('Model LFS hash mismatch')
        proof='sha256_lfs'
    elif item.blob_id and git.hexdigest()==item.blob_id:proof='sha1_git_blob'
    else:raise RuntimeError('Upstream model hash unavailable or mismatched')
    return {'Bytes':size,'SHA256':sha256.hexdigest(),'UpstreamHash':upstream or item.blob_id,'Proof':proof}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--acquire-public-models',action='store_true',required=True)
    args=parser.parse_args();runtime.bootstrap()
    from huggingface_hub import HfApi,hf_hub_download
    validation=json.loads((runtime.OUT/'runtime/validation.json').read_text(encoding='utf-8'))
    if validation['Status']!='passed':raise RuntimeError('Validated isolated runtime required')
    record={'Status':'pending','StartedAtUTC':runtime.now(),'Models':{},
            'SourceHashes':{str(p.relative_to(ROOT)).replace('\\','/'):runtime.sha(p) for p in
                (Path(__file__),ROOT/'software/model_backend/runtime.py')},
            'Scope':'Public checkpoint assets only; no audio upload, inference, remote Python code or login'}
    runtime.save('models/manifest.json',record)
    try:
        api=HfApi(token=False)
        for kind,profile in PROFILES.items():
            info=api.model_info(profile['Repo'],files_metadata=True,token=False)
            if not info.sha or len(info.sha)!=40 or any(c not in '0123456789abcdef' for c in info.sha):
                raise RuntimeError('Immutable model revision required')
            entries={item.rfilename:item for item in info.siblings}
            required=set(profile['Required'])
            if kind=='asr':required.add('vocabulary.json' if 'vocabulary.json' in entries else 'vocabulary.txt')
            if kind=='mt':required.add('model.safetensors' if 'model.safetensors' in entries else 'pytorch_model.bin')
            if not required.issubset(entries):raise RuntimeError('Required model assets missing')
            model={'Repo':profile['Repo'],'Revision':info.sha,'License':profile['License'],'Files':[]}
            record['Models'][kind]=model;runtime.save('models/manifest.json',record)
            names=required|(set(profile['Optional'])&entries.keys())
            for name in sorted(names):
                print('FETCH_PUBLIC_MODEL_ASSET '+kind+' '+name,flush=True)
                path=Path(hf_hub_download(repo_id=profile['Repo'],filename=name,revision=info.sha,
                    local_dir=runtime.OUT/'models'/kind,cache_dir=runtime.OUT/'hub',token=False))
                if not path.resolve().is_relative_to((runtime.OUT/'models'/kind).resolve()):raise RuntimeError('Model path escaped its directory')
                proof=verify_file(path,entries[name]);proof.update(Name=name,Path=str(path.relative_to(runtime.OUT)).replace('\\','/'))
                model['Files'].append(proof);runtime.save('models/manifest.json',record)
                print('VERIFIED_PUBLIC_MODEL_ASSET '+kind+' '+name+' '+str(proof['Bytes']),flush=True)
        record.update(Status='passed',VerifiedAtUTC=runtime.now());runtime.save('models/manifest.json',record)
        print('PASS_PUBLIC_MODEL_ASSETS');return 0
    except Exception as error:
        # Retain typed failure and verified partial files; no raw transport URLs/tokens.
        record.update(Status='failed',ErrorType=type(error).__name__,FailedAtUTC=runtime.now())
        runtime.save('models/manifest.json',record);print('Model acquisition stopped: '+type(error).__name__);return 1
if __name__=='__main__':raise SystemExit(main())
