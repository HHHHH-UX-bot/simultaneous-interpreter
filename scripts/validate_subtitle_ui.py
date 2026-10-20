"""Render the actual portable C implementation on the host, with bounded buffers."""
import argparse,ctypes,hashlib,json,os,subprocess,uuid
from datetime import datetime,timezone
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/subtitle_ui'
COMMON=('software/network_ui/language_profile.h','software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_model.c',
    'software/subtitle_ui/ui_assets.h','output/subtitle_ui/ui_assets.c',
    'software/subtitle_ui/ui_render.h','software/subtitle_ui/ui_render.c',
    'software/subtitle_ui/ui_demo.h','software/subtitle_ui/ui_demo.c')
INPUTS=(*COMMON,'software/subtitle_ui/ui_host.c','scripts/validate_subtitle_ui.py')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes(): return {p:sha(ROOT/p) for p in INPUTS}
class Stats(ctypes.Structure):
    _fields_=[(s,ctypes.c_uint32) for s in ('missing_glyphs','clipped_glyph_pixels','caption_lines',
        'caption_pages','caption_page','source_lines','history_lines','pixels_written')]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--gcc',type=Path,required=True)
    parser.add_argument('--mode',choices=('enzh','enen'))
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    defines=[] if not args.mode else ['-DZ7_ARM_TLS','-DZ7_SOURCE_EN']+(['-DZ7_TRANSLATE_ZH','-DZ7_TRANSLATE_AUDIO'] if args.mode=='enzh' else ['-DZ7_ASR_SUBTITLE','-DZ7_RECOGNIZE_EN'])
    record_path=OUT/('host_validation_'+args.mode+'.json' if args.mode else 'host_validation.json')
    temp=ROOT/'tmp/subtitle_ui'/('host_'+uuid.uuid4().hex[:8]);temp.mkdir(parents=True)
    record={'Status':'pending','Mode':args.mode or 'shared','SourceHashes':hashes()}
    try:
        dll=temp/'ui_host.dll'
        files=('software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c',
               'software/subtitle_ui/ui_demo.c','software/subtitle_ui/ui_host.c','output/subtitle_ui/ui_assets.c')
        command=[str(args.gcc),'-shared','-O2','-g','-Wall','-Wextra','-Werror','-std=c11',*defines,
                 '-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/p) for p in files],'-o',str(dll)]
        result=subprocess.run(command,cwd=temp,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (OUT/'host_build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode: raise RuntimeError('Host C build failed; see output/subtitle_ui/host_build.log')
        directory=os.add_dll_directory(str(args.gcc.parent))
        library=ctypes.CDLL(str(dll));library.ui_host_state_size.restype=ctypes.c_uint32
        library.ui_host_draw.argtypes=[ctypes.c_uint32]*4+[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Stats)]
        library.ui_host_draw.restype=ctypes.c_int
        library.ui_host_format_elapsed.argtypes=[ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
        count=1024*600;padding=64
        memory=(ctypes.c_uint32*(count+padding*2))()
        pointer=ctypes.cast(ctypes.byref(memory,padding*4),ctypes.POINTER(ctypes.c_uint32))
        images=[];long_pages={}
        for scenario in range(12):
            for font in range(3):
                stats=Stats()
                for i in range(padding): memory[i]=memory[count+padding+i]=0xa5c35a3c
                if not library.ui_host_draw(scenario,font,0,93400,pointer,ctypes.byref(stats)):
                    raise RuntimeError('Native renderer rejected fixture')
                if any(memory[i]!=0xa5c35a3c or memory[count+padding+i]!=0xa5c35a3c for i in range(padding)):
                    raise RuntimeError('Renderer wrote outside its framebuffer')
                if stats.missing_glyphs or stats.clipped_glyph_pixels:
                    raise RuntimeError(f'Unexpected glyph loss in scenario {scenario}, font {font}: {stats.missing_glyphs}/{stats.clipped_glyph_pixels}')
                if scenario==2: long_pages[font]=stats.caption_pages
                pages=stats.caption_pages if scenario==2 else (max(stats.caption_pages,(stats.source_lines+1)//2) if scenario==8 else 1)
                for page in range(pages):
                    if page: library.ui_host_draw(scenario,font,page,93400,pointer,ctypes.byref(stats))
                    if any(memory[i]!=0xa5c35a3c or memory[count+padding+i]!=0xa5c35a3c for i in range(padding)):
                        raise RuntimeError('Paged renderer wrote outside its framebuffer')
                    if stats.missing_glyphs or stats.clipped_glyph_pixels: raise RuntimeError('Paged glyph loss')
                    raw=ctypes.string_at(pointer,count*4)
                    target=OUT/f'preview_{scenario}_{font}_{page}.png'
                    Image.frombytes('RGB',(1024,600),raw,'raw','BGRX').save(target)
                    images.append({'File':str(target.relative_to(ROOT)).replace('\\','/'),
                        'Scenario':scenario,'FontIndex':font,'Page':page,'CaptionPages':stats.caption_pages,
                        'CaptionLines':stats.caption_lines,'SourceLines':stats.source_lines,
                        'PixelSHA256':hashlib.sha256(raw).hexdigest()})
        for elapsed,expected in ((3599999,'59:59'),(3600000,'01:00:00'),(359999999,'99:59:59'),
                                 (360000000,'100:00:00'),(0xffffffff,'1193:02:47')):
            clock=(ctypes.c_uint32*16)();library.ui_host_format_elapsed(elapsed,clock)
            if ''.join(chr(v) for v in clock if v)!=expected:
                raise RuntimeError('Session time formatter failed at '+str(elapsed))
            stats=Stats()
            library.ui_host_draw(1,1,0,elapsed,pointer,ctypes.byref(stats))
            if stats.missing_glyphs or stats.clipped_glyph_pixels or any(
                memory[i]!=0xa5c35a3c or memory[count+padding+i]!=0xa5c35a3c for i in range(padding)):
                raise RuntimeError('Session timer boundary caused glyph loss or out-of-bounds access')
        if not (long_pages[0]<=long_pages[1]<=long_pages[2] and long_pages[1]>1):
            raise RuntimeError('Long text does not page consistently across font sizes')
        if hashes()!=record['SourceHashes']: raise RuntimeError('UI inputs changed during host validation')
        record.update(Status='passed',VerifiedAtUTC=datetime.now(timezone.utc).isoformat(),
            ModelBytes=library.ui_host_state_size(),FrameSize=[1024,600],Images=images,
            Cases=['idle','final/history','long text/all pages','ASR draft retaining Chinese',
                   'paused','cleared','source hidden','negative glyph bearings','255-scalar bearing text',
                   'connecting with retained caption','offline with retained caption','ready to speak'],FontSizes=[28,36,44],
            BufferCanariesVerified=True,MissingGlyphs=0,ClippedGlyphPixels=0,
            NativeLibrary=str(dll.relative_to(ROOT)).replace('\\','/'),NativeLibrarySHA256=sha(dll),
            Scope='Native C renderer and simulated model states only; no board or touch/translation test')
        directory.close();print('PASS_SUBTITLE_HOST_RENDER',flush=True)
    except Exception as error:
        record.update(Status='failed',Error=str(error));print(error,flush=True)
    finally: record_path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if record['Status']=='passed' else 1
if __name__=='__main__': raise SystemExit(main())
