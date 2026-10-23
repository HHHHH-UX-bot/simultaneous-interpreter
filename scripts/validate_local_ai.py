"""Exercise the real board C frontend/model, enrollment and touch UI on Windows.
Synthetic speech proves execution, never human accuracy or authentication.
"""
import ctypes as C,json,subprocess,sys,time,hashlib
from pathlib import Path
import numpy as np
from PIL import Image
from prepare_local_ai import ROOT,OUT,load_audio

def main():
    dll=OUT/'local_ai_host.dll'
    sources=['tests/local_ai_host.c','software/network_ui/tiny_features.c','output/local_ai/tiny_model.c',
        'software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c']
    command=['gcc','-shared','-O2','-std=c11','-Wall','-Wextra','-Werror','-DZ7_LOCAL_AI','-DZ7_ARM_TLS','-DZ7_TRANSLATE_EN',
        '-I',str(ROOT/'software/network_ui'),'-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'software/z7_nano_usb'),
        *[str(ROOT/p) for p in sources],'-o',str(dll)]
    r=subprocess.run(command,capture_output=True);(OUT/'native_build.log').write_bytes(r.stdout+r.stderr)
    if r.returncode:print(r.stderr.decode(errors='replace'));r.check_returncode()
    lib=C.CDLL(str(dll));lib.host_get.restype=C.c_uint32
    def get(i):return lib.host_get(i)
    def feed(pcm):
        padded=np.concatenate([np.zeros(800,dtype=np.int16),pcm,np.zeros(12000,dtype=np.int16)])
        lib.host_feed(padded.ctypes.data_as(C.POINTER(C.c_int16)),len(padded))
    results=[]
    for directory,rates in [('voices',(0,2,4)),('heldout',(1,3))]:
        for lang in ('en','zh'):
            for i in range(12):
                for rate in rates:
                    path=OUT/f'{directory}/{lang}_{i}_{rate}.wav'
                    if not path.exists():continue
                    lib.host_init(0 if lang=='en' else 1,1)
                    started=time.perf_counter();feed(load_audio(path));elapsed=time.perf_counter()-started
                    results.append(dict(file=str(path.relative_to(OUT)),expected=i+1,predicted=get(2),result=get(1),cost=get(3),seconds=elapsed))
    # Silence, bounded noise, pause and old-page pointer isolation.
    lib.host_init(1,1);feed(np.zeros(32000,dtype=np.int16));assert get(0)==0
    noise=np.random.default_rng(1701).integers(-90,91,32000,dtype=np.int16);feed(noise);assert get(0)==0
    negatives=[]
    for lang in ('en','zh'):
        for i in range(4):
            lib.host_init(0 if lang=='en' else 1,1);feed(load_audio(OUT/f'unknown/{lang}_{i}_2.wav'))
            negatives.append(dict(language=lang,index=i,result=get(1),cost=get(3)))
            assert get(0)==0,(lang,i,get(2))
    lib.host_init(1,1)
    feed(np.random.default_rng(1702).integers(-4000,4001,32000,dtype=np.int16));assert get(0)==0
    # Accept a true enrolled acoustic identity; clear erases that identity.
    lib.host_init(1,1)
    # UI commands are discovered from the header enum, not duplicated ordinals.
    header=(ROOT/'software/subtitle_ui/ui_model.h').read_text();block=header.split('enum ui_command {')[1].split('};')[0]
    commands={v.strip():i for i,v in enumerate(block.split(',')) if v.strip()}
    cmd=lambda s:lib.host_command(commands[s])
    lib.host_external_epoch();cmd('UI_AI_ENROLL_1');assert get(8)==1,'Enrollment cancelled by a prior cloud reconnect'
    cmd('UI_AI_CANCEL_ENROLL')
    cmd('UI_AI_ENROLL_1')
    enrollment=load_audio(OUT/'voices/zh_5_2.wav')
    for _ in range(4):feed(enrollment)
    assert get(6)==1 and get(8)==0,(get(6),get(7),get(8))
    feed(enrollment);speaker=dict(id=get(4),score=get(5),status=get(15));assert speaker['id']==1,speaker
    for i in range(12):
        feed(load_audio(OUT/f'voices/en_{i}_2.wav'));assert get(4)==0,('different synthetic voice accepted',i,get(5))
    cmd('UI_AI_ENROLL_2')
    for _ in range(4):feed(enrollment)
    feed(enrollment);assert get(4)==0,'Identical enrolled profiles must be ambiguous'
    cmd('UI_AI_ERASE');assert get(6)==0
    feed(enrollment);assert get(4)==0
    cmd('UI_AI_ENROLL_2');lib.host_tick(31000);assert get(8)==0 and get(15)==6
    cmd('UI_PAUSE');before=get(0);feed(enrollment);assert get(0)==before
    cmd('UI_RESUME');epoch=get(9);cmd('UI_AI_CLOUD');assert get(10)==0 and get(9)>epoch
    cmd('UI_OPEN_SETTINGS');assert lib.host_touch(700,456)>0 and get(11)==1
    assert lib.host_touch(400,160)>0 and get(10)==1
    class Stats(C.Structure):_fields_=[(n,C.c_uint32) for n in ['missing','clipped','caption_lines','source_lines','history_lines','pages','page','drawn']]
    images=[]
    for name in ('ai_page','settings_page','main_page'):
        if name=='settings_page':cmd('UI_CLOSE_AI')
        if name=='main_page':cmd('UI_CLOSE_SETTINGS')
        pixels=(C.c_uint32*(1024*600))();stats=Stats();lib.host_render(pixels,C.byref(stats))
        assert stats.missing==0 and stats.clipped==0,(name,list(bytes(stats)))
        file=OUT/(name+'.png');Image.frombytes('RGB',(1024,600),bytes(pixels),'raw','BGRX').save(file);images.append(file.name)
    groups={g:dict(total=sum(x['file'].startswith(g) for x in results),correct=sum(x['file'].startswith(g) and x['predicted']==x['expected'] and x['result']==1 for x in results)) for g in ('voices','heldout')}
    record=dict(status='passed' if groups['voices']['correct']==groups['voices']['total'] else 'failed',scope='actual C inference, synthetic voices; no human-microphone or speaker accuracy claim',model_bytes=lib.host_size(),groups=groups,speaker=speaker,negative_phrases=negatives,images=images,cases=results)
    (OUT/'native_validation.json').write_text(json.dumps(record,indent=2))
    print(json.dumps({k:v for k,v in record.items() if k!='cases'}))
    assert groups['voices']['correct']==groups['voices']['total'],groups
if __name__=='__main__':main()
