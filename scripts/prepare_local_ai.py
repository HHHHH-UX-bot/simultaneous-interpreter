"""Build tiny DTW phrase models using public Windows synthetic voices.

Models recognize only assets/local_phrases.json, not arbitrary conversation.
No personal audio or cloud credential is needed or written by this builder.
"""
import ctypes as C, hashlib, json, math, subprocess, wave
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/local_ai'
OUT.mkdir(parents=True,exist_ok=True)

def table(ctype,name,values):
    a=np.asarray(values)
    shape=''.join(f'[{n}]' for n in a.shape)
    def braces(v):return '{'+','.join(braces(x) if isinstance(x,list) else str(x) for x in v)+'}'
    return f'static const {ctype} {name}{shape}='+braces(a.tolist())+';\n'

def tables():
    mel=lambda f:2595*math.log10(1+f/700)
    hz=lambda m:700*(10**(m/2595)-1)
    knots=[int(round(hz(m)*512/16000)) for m in np.linspace(mel(80),mel(7600),22)]
    assert all(a<b for a,b in zip(knots,knots[1:]))
    text='/* Generated integer MFCC constants; no learned weights. */\n'
    text+=table('int16_t','tiny_window',[round((.54-.46*math.cos(2*math.pi*i/399))*32767) for i in range(400)])
    text+=table('int16_t','tiny_cos',[round(math.cos(2*math.pi*i/512)*32767) for i in range(256)])
    text+=table('int16_t','tiny_sin',[round(math.sin(2*math.pi*i/512)*32767) for i in range(256)])
    text+=table('uint16_t','tiny_mel',knots)
    text+=table('int16_t','tiny_dct',[[round(math.cos(math.pi*(d+1)*(b+.5)/20)*32767) for b in range(20)] for d in range(12)])
    (ROOT/'software/network_ui/tiny_tables.h').write_text(text)

def load_audio(path,rate=16000):
    with wave.open(str(path),'rb') as w:
        sr=w.getframerate();assert w.getsampwidth()==2 and w.getnchannels()==1
        pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2')
    return np.interp(np.arange(round(len(pcm)*rate/sr))*sr/rate,np.arange(len(pcm)),pcm).astype(np.int16)

def frontend(lib,pcm):
    frames=[]
    for at in range(0,len(pcm)-399,320):
        f=np.zeros(12,dtype=np.int16)
        energy=lib.tiny_features(pcm[at:at+400].ctypes.data_as(C.POINTER(C.c_int16)),f.ctypes.data_as(C.POINTER(C.c_int16)))
        if energy>10000:frames.append(f)
    return np.asarray(frames,dtype=np.int16)

def main():
    tables()
    dll=OUT/'tiny_features.dll'
    subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-shared','-o',str(dll),str(ROOT/'software/network_ui/tiny_features.c')],check=True)
    lib=C.CDLL(str(dll));lib.tiny_features.restype=C.c_uint32;lib.tiny_dtw.restype=C.c_uint32
    phrases=json.loads((ROOT/'assets/local_phrases.json').read_text(encoding='utf-8'))
    text='#include <stdint.h>\n#include "tiny_model.h"\n'
    metadata=[];models=[];audio=[]
    for lang in ('en','zh'):
        for i,p in enumerate(phrases):
            for rate in (0,2,4):
                path=OUT/f'voices/{lang}_{i}_{rate}.wav'
                f=frontend(lib,load_audio(path))
                # Uniform temporal compression limits model memory and DTW work.
                if len(f)>64:f=f[np.linspace(0,len(f)-1,64).astype(int)]
                name=f'tpl_{lang}_{i}_{rate}';text+=table('int16_t',name,f)
                models.append((0 if lang=='en' else 1,i,len(f),name))
                metadata.append(dict(file=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),frames=len(f)))
            # Target speech is stored at DAC's native 24 kHz, signed 8 bit.
            pcm=load_audio(OUT/f'voices/{lang}_{i}_2.wav',24000)
            name=f'voice_{lang}_{i}';text+=table('int8_t',name,(pcm.astype(np.int32)>>8).astype(np.int8))
            audio.append((lang,i,len(pcm),name))
    text+='const struct tiny_template tiny_templates[]={\n'+',\n'.join('{%d,%d,%d,%s[0]}'%x for x in models)+'};\n'
    text+='const uint32_t tiny_template_count=sizeof(tiny_templates)/sizeof(tiny_templates[0]);\n'
    text+='const uint32_t tiny_text[2][12][64]='+repr([[[ord(c) for c in p[l]]+[0]*(64-len(p[l])) for p in phrases] for l in ('en','zh')]).replace('[','{').replace(']','}')+';\n'
    text+='const struct tiny_voice tiny_voices[2][12]={\n'
    for lang in ('en','zh'):
        text+='{'+','.join('{%d,%s}'%(n,name) for l,i,n,name in audio if l==lang)+'},\n'
    text+='};\n'
    (OUT/'tiny_model.c').write_text(text)
    (OUT/'manifest.json').write_text(json.dumps(dict(kind='fixed-phrase MFCC/DTW acoustic templates',scope='12 phrases in each direction; synthetic training only; real speaker accuracy unvalidated',template_parameters=sum(x[2]*12 for x in models),voices='Windows SAPI Huihui and Zira, rates -2/0/2',training=metadata),indent=2))
    # Deterministic on-board fixture: one Chinese phrase and an English
    # out-of-language candidate. The separate self-test firmware stays offline.
    parts=[]
    for lang,index in (('zh',5),('en',7)):
        parts.extend((np.zeros(16000,dtype=np.int16),load_audio(OUT/f'voices/{lang}_{index}_2.wav'),np.zeros(24000,dtype=np.int16)))
    pcm=np.concatenate(parts)
    fixture='#include <stdint.h>\n'+table('int16_t','selftest_pcm',pcm).replace('static const','const')
    fixture+=f'const uint32_t selftest_samples={len(pcm)}u;\nconst uint32_t selftest_speech_ms={(len(pcm)+15)//16}u;\n'
    (OUT/'board_selftest_pcm.c').write_text(fixture)
    print('PASS_LOCAL_MODEL',len(models),'templates',sum(x[2]*12 for x in models),'int16 acoustic parameters')
if __name__=='__main__':main()
