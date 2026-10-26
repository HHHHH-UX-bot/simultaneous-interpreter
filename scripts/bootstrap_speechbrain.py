"""Install the optional PC-only SpeechBrain comparison in a project venv.
Use official PyPI JSON and SHA256-verified wheels: the local pip index request
has stalled on this machine. No global Python package is changed.
"""
import hashlib,importlib.metadata,json,os,subprocess,sys,urllib.request,venv
from pathlib import Path
from pip._vendor.packaging.requirements import Requirement
from pip._vendor.packaging.tags import sys_tags
from pip._vendor.packaging.utils import parse_wheel_filename
from pip._vendor.packaging.version import Version,InvalidVersion
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/local_ai/speechbrain_runtime';OUT.mkdir(parents=True,exist_ok=True)
ENV=ROOT/'output/local_ai/speechbrain_env';PY=ENV/'Scripts/python.exe'
TAGS=set(sys_tags());chosen={};pending=[Requirement('speechbrain')]

def metadata(name,version=''):
    with urllib.request.urlopen('https://pypi.org/pypi/'+name+('/'+version if version else '')+'/json',timeout=25) as r:return json.load(r)

def main():
    if not PY.exists():venv.create(ENV,with_pip=True)
    installed=json.loads(subprocess.check_output([str(PY),'-m','pip','list','--format=json']))
    installed={x['name'].lower().replace('_','-'):x['version'] for x in installed}
    while pending:
        req=pending.pop(0);name=req.name.lower().replace('_','-')
        if req.marker and not req.marker.evaluate({'extra':''}):continue
        if name in installed and req.specifier.contains(installed[name]):continue
        if name in chosen:
            if not req.specifier.contains(chosen[name]['version']):raise RuntimeError('Dependency version conflict: '+str(req))
            continue
        d=metadata(req.name)
        versions=[]
        for value in d['releases']:
            try:v=Version(value)
            except InvalidVersion:continue
            if not v.is_prerelease and req.specifier.contains(v):versions.append(v)
        candidates=[d['info']['version']]+[str(v) for v in sorted(versions,reverse=True) if str(v)!=d['info']['version']]
        match=None
        for version in candidates:
            if not req.specifier.contains(version):continue
            item=d if version==d['info']['version'] else metadata(req.name,version)
            for u in item['urls']:
                if u['filename'].endswith('.whl') and parse_wheel_filename(u['filename'])[3]&TAGS:match=(item,u);break
            if match:break
        if not match:raise RuntimeError('No supported wheel for '+str(req))
        item,u=match;file=OUT/u['filename']
        if not file.exists() or hashlib.sha256(file.read_bytes()).hexdigest()!=u['digests']['sha256']:
            with urllib.request.urlopen(u['url'],timeout=60) as response:file.write_bytes(response.read())
        assert hashlib.sha256(file.read_bytes()).hexdigest()==u['digests']['sha256']
        chosen[name]=dict(version=item['info']['version'],file=file.name,sha256=u['digests']['sha256'],url=u['url'])
        pending.extend(Requirement(r) for r in (item['info']['requires_dist'] or []))
        print('VERIFIED_WHEEL',name,item['info']['version'],flush=True)
    manifest=OUT/'wheel_manifest.json'
    previous=json.loads(manifest.read_text()) if manifest.exists() else {}
    previous.update(chosen);manifest.write_text(json.dumps(previous,indent=2)+'\n')
    if chosen:subprocess.run([str(PY),'-m','pip','install','--no-index','--no-deps',*[str(OUT/x['file']) for x in chosen.values()]],check=True)
    subprocess.run([str(PY),'-m','pip','check'],check=True)
    (OUT/'installed.lock').write_bytes(subprocess.check_output([str(PY),'-m','pip','freeze']))
    print('PASS_SPEECHBRAIN_RUNTIME')
if __name__=='__main__':main()
