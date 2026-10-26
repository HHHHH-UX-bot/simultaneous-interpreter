"""Fetch public, licensed training audio; no user audio or cloud inference.
Archives are verified against the publisher's MD5 and recorded with SHA256.
"""
import argparse,hashlib,json,tarfile,time,urllib.request,zipfile,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FILES=[
 ('train-clean-5','31','5df7d4e78065366204ca6845bb08f490'),
 ('train-clean-100','12','2a93770f6d5c6c964bc36631d331a522'),
 ('dev-clean','12','42e2234ba48799c1f50f24a7926300a1')]
def digest(path,kind):
 h=hashlib.new(kind)
 with path.open('rb') as f:
  for block in iter(lambda:f.read(2**20),b''):h.update(block)
 return h.hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--only',choices=[x[0] for x in FILES]);a=p.parse_args()
 base=ROOT/'tmp/speaker_cnn/corpus';base.mkdir(parents=True,exist_ok=True)
 manifest=[]
 for name,resource,expected in FILES:
  if a.only and name!=a.only:continue
  filename=name+'.tar.gz';archive=base/filename
  url='https://www.openslr.org/resources/'+resource+'/'+filename
  if not archive.exists() or digest(archive,'md5')!=expected:
   part=archive.with_suffix('.part')
   with urllib.request.urlopen(url,timeout=45) as response,part.open('wb') as f:
    total=int(response.headers.get('Content-Length',0));done=0;last=time.monotonic()
    for block in iter(lambda:response.read(2**20),b''):
     f.write(block);done+=len(block)
     if time.monotonic()-last>8:
      print(json.dumps({'download':name,'bytes':done,'total':total}),flush=True);last=time.monotonic()
   assert digest(part,'md5')==expected,'Publisher checksum mismatch'
   part.replace(archive)
  destination=base/'audio'
  marker=destination/(name+'.complete')
  if not marker.exists():
   destination.mkdir(exist_ok=True)
   with tarfile.open(archive,'r:gz') as tar:
    for member in tar:
     target=(destination/member.name).resolve()
     assert target.is_relative_to(destination.resolve()) and not member.issym() and not member.islnk()
     tar.extract(member,destination,filter='data')
   marker.write_text(expected,encoding='ascii')
  item=dict(name=name,url=url,md5=expected,sha256=digest(archive,'sha256'),license='CC BY 4.0',purpose='offline speaker CNN research training/evaluation',user_audio=False)
  manifest.append(item);print('VERIFIED_AND_EXTRACTED '+name,flush=True)
 out=ROOT/'output/speaker_cnn';out.mkdir(exist_ok=True)
 (out/('corpus_'+(a.only or 'all')+'.json')).write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
