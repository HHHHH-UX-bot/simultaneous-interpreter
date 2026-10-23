"""Re-fetch the exact upstream revision recorded for this deployed model."""
import hashlib,json,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODELS=ROOT/'models'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def main():
    manifest=json.loads((MODELS/'source_manifest.json').read_text())
    for name,record in manifest['files'].items():
        if Path(name).name!=name:raise ValueError('Unexpected manifest path')
        path=MODELS/name
        if path.exists() and digest(path)==record['sha256']:
            print('VERIFIED_EXISTING',name,flush=True);continue
        url='https://huggingface.co/'+manifest['repository']+'/resolve/'+manifest['revision']+'/'+name
        temp=path.with_suffix(path.suffix+'.part')
        with urllib.request.urlopen(url,timeout=90) as response,temp.open('wb') as stream:
            for block in iter(lambda:response.read(4*1024*1024),b''):stream.write(block)
        if temp.stat().st_size!=record['bytes'] or digest(temp)!=record['sha256']:
            raise RuntimeError('Upstream checksum mismatch: '+name)
        temp.replace(path);print('VERIFIED_DOWNLOADED',name,flush=True)
if __name__=='__main__':main()
