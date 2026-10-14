"""Controlled CPU file baseline; real model output is never replaced by demo text."""
import json,re,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'software/model_backend'))
import runtime
from engine import LocalInterpreter,decode_wav,VAD_OPTIONS

def words(text):return re.findall(r"[a-z]+(?:'[a-z]+)?|\d+",text.lower())
def word_error_rate(reference,actual):
    a,b=words(reference),words(actual);last=list(range(len(b)+1))
    for i,word in enumerate(a,1):
        row=[i]
        for j,other in enumerate(b,1):row.append(min(row[-1]+1,last[j]+1,last[j-1]+(word!=other)))
        last=row
    return last[-1]/len(a) if a else float(bool(b))

def main():
    record={'Status':'pending','StartedAtUTC':runtime.now(),
            'Scope':'Controlled synthetic English file, local CPU ASR/MT only; no mic/stream/server/board integration'}
    runtime.save('file_baseline.json',record)
    try:
        reference=json.loads((runtime.OUT/'fixtures/reference.json').read_text(encoding='utf-8'))
        audio=ROOT/reference['Audio'];data=audio.read_bytes()
        if runtime.sha(audio)!=reference['AudioSHA256']:raise RuntimeError('Controlled audio fixture changed')
        decode_wav(data)
        record['SourceHashes']={str(p.relative_to(ROOT)).replace('\\','/'):runtime.sha(p) for p in
            (Path(__file__),ROOT/'software/model_backend/runtime.py',ROOT/'software/model_backend/engine.py')}
        print('LOADING_LOCAL_CPU_MODELS',flush=True)
        interpreter=LocalInterpreter(threads=8)
        print('RUNNING_CONTROLLED_ENGLISH_AUDIO',flush=True)
        started=time.perf_counter();result=interpreter.transcribe_translate(data)
        result['CombinedSeconds']=time.perf_counter()-started
        result['CombinedRTF']=result['CombinedSeconds']/result['AudioSeconds']
        if not result['Transcript'] or not result['Chinese']:raise RuntimeError('Controlled speech did not produce both texts')
        record.update(Status='executed',VerifiedAtUTC=runtime.now(),AudioSHA256=reference['AudioSHA256'],
            ReferenceEnglish=reference['Source'],ReferenceChinese=reference['ReferenceChinese'],Result=result,
            WER=word_error_rate(reference['Source'],result['Transcript']),ModelLoadSeconds=interpreter.load_seconds,
            ModelManifestSHA256=runtime.sha(runtime.OUT/'models/manifest.json'),Device='cpu',Threads=8,
            ASRComputeType='int8',MTComputeType='float32',HumanTranslationAcceptance='pending',
            VAD=interpreter.vad_asset,VADOptions=VAD_OPTIONS,
            QualityScope='One synthetic short file; does not establish streaming or microphone quality')
        runtime.save('file_baseline.json',record)
        print('REAL_ENGLISH_ASR '+result['Transcript'],flush=True)
        print('REAL_CHINESE_MT '+result['Chinese'],flush=True)
        print('PASS_LOCAL_MODEL_FILE_EXECUTION RTF='+str(round(result['CombinedRTF'],3)),flush=True)
        return 0
    except Exception as error:
        record.update(Status='failed',ErrorType=type(error).__name__,FailedAtUTC=runtime.now())
        runtime.save('file_baseline.json',record);print('Local file baseline stopped: '+type(error).__name__,flush=True);return 1
if __name__=='__main__':raise SystemExit(main())
