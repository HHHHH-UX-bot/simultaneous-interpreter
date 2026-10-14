"""Project-local CPU inference runtime; the existing machine Torch is reused."""
import hashlib,json,os,re,sys
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/model_backend'
SITE=OUT/'runtime/site-packages'
COMPAT=OUT/'runtime/compat'
VAD_SHA256='4cbf549b8326f60f80f2536d9eefeb450a9abe83365a098031c89719f1be17d2'
MODEL_PROFILES = {
    'asr': {
        'Repo': 'Systran/faster-whisper-small.en', 'License': 'mit',
        'Required': {'config.json', 'model.bin', 'tokenizer.json'},
        'Choice': {'vocabulary.json', 'vocabulary.txt'},
        'Optional': {'preprocessor_config.json', 'README.md'},
    },
    'mt': {
        'Repo': 'Helsinki-NLP/opus-mt-en-zh', 'License': 'apache-2.0',
        'Required': {'config.json', 'source.spm', 'target.spm', 'vocab.json', 'tokenizer_config.json'},
        'Choice': {'model.safetensors', 'pytorch_model.bin'},
        'Optional': {'generation_config.json', 'special_tokens_map.json', 'README.md', 'LICENSE'},
    },
}

def now():return datetime.now(timezone.utc).isoformat()
def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()
def save(name,record):
    path=OUT/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
def bootstrap():
    if not SITE.is_dir():raise RuntimeError('Project-local inference runtime is not installed')
    if str(SITE) not in sys.path:sys.path.insert(0,str(SITE))
    if COMPAT.is_dir() and str(COMPAT) not in sys.path:sys.path.insert(0,str(COMPAT))
    os.environ.update(HF_HOME=str(OUT/'hub'),HF_HUB_CACHE=str(OUT/'hub'),
        HF_HUB_DISABLE_IMPLICIT_TOKEN='1',HF_HUB_DISABLE_XET='1',
        HF_HUB_DISABLE_PROGRESS_BARS='1',TOKENIZERS_PARALLELISM='false')

def verified_models():
    record=json.loads((OUT/'models/manifest.json').read_text(encoding='utf-8'))
    if record.get('Status')!='passed' or set(record.get('Models', {}))!=set(MODEL_PROFILES):
        raise RuntimeError('Complete verified ASR and MT profiles required')
    model_root=(OUT/'models').resolve()
    for kind,profile in MODEL_PROFILES.items():
        model=record['Models'][kind]
        if (model.get('Repo')!=profile['Repo'] or model.get('License')!=profile['License']
                or not re.fullmatch('[0-9a-f]{40}', str(model.get('Revision', '')))):
            raise RuntimeError('Unexpected local model profile')
        files=model.get('Files')
        if not isinstance(files, list) or not files:
            raise RuntimeError('Model artifact manifest is incomplete')
        directory=(OUT/'models'/kind).resolve()
        if not directory.is_relative_to(model_root):
            raise RuntimeError('Model directory escaped its root')
        names=[]
        for item in files:
            name=item.get('Name')
            if (not isinstance(name, str) or Path(name).name!=name
                    or item.get('Path')!=f'models/{kind}/{name}'):
                raise RuntimeError('Model artifact name/path mismatch')
            path=directory/name
            size=item.get('Bytes')
            digest=item.get('SHA256')
            if (not path.resolve().is_relative_to(directory) or not path.is_file()
                    or type(size) is not int or size<=0 or path.stat().st_size!=size
                    or not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest)
                    or sha(path)!=digest):
                raise RuntimeError('Local model artifact changed')
            names.append(name)
        selected=set(names)
        if (len(names)!=len(selected) or not profile['Required'].issubset(selected)
                or len(profile['Choice']&selected)!=1
                or not selected.issubset(profile['Required']|profile['Choice']|profile['Optional'])):
            raise RuntimeError('Required or selected model artifacts do not match')
        # Transformers can prefer a newly added safetensors/checkpoint index,
        # adapter config, or tokenizer file. Reject unlisted artifacts before
        # invoking either loader, instead of merely hashing the listed subset.
        actual=set()
        for path in directory.iterdir():
            if path.name=='.cache' and path.is_dir():
                continue  # Hugging Face download metadata; not a loader input.
            if not path.is_file():
                raise RuntimeError('Unexpected model artifact directory')
            actual.add(path.name)
        if actual!=selected:
            raise RuntimeError('Unlisted model artifacts present')
    return record

def verified_vad():
    from importlib.metadata import version
    from faster_whisper.utils import get_assets_path
    directory=Path(get_assets_path()).resolve()
    expected=(SITE/'faster_whisper/assets').resolve()
    path=directory/'silero_vad_v6.onnx'
    if (directory!=expected or version('faster-whisper')!='1.2.1'
            or not path.resolve().is_relative_to(expected) or sha(path)!=VAD_SHA256):
        raise RuntimeError('Unexpected bundled voice-activity model')
    return {'Package':'faster-whisper','Version':'1.2.1','Name':path.name,
            'Bytes':path.stat().st_size,'SHA256':VAD_SHA256,
            'Source':'Verified official PyPI wheel; project-local bundled asset'}
