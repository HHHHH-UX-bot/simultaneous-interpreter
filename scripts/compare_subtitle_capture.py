"""Re-render an immutable board snapshot natively and compare every DDR pixel."""
import argparse,ctypes,hashlib,json,os,struct
from datetime import datetime,timezone
from pathlib import Path
from PIL import Image
from validate_subtitle_ui import ROOT,OUT,Stats

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def compare(folder,gcc_directory,artifact_out=OUT,host_file=None,build_file=None):
    folder=folder.resolve()
    folder.relative_to((artifact_out/'hardware').resolve())
    result=json.loads((folder/'result.json').read_text(encoding='utf-8'))
    host_path=host_file or OUT/'host_validation.json'
    build_path=build_file or artifact_out/'build.json'
    host=json.loads(host_path.read_text(encoding='utf-8'))
    build=json.loads(build_path.read_text(encoding='utf-8'))
    record={'Status':'pending','VerifiedAtUTC':datetime.now(timezone.utc).isoformat(),
            'Capture':str(folder.relative_to(ROOT)).replace('\\','/'),
            'CaptureRecordSHA256':sha(folder/'result.json'),'ApplicationELFSHA256':build['ELFSHA256'],
            'ComparisonScriptSHA256':sha(Path(__file__)),
            'HostValidationSHA256':sha(host_path),'ApplicationBuildSHA256':sha(build_path)}
    try:
        if result['Status']!='passed' or result['Action']!='capture' or host['Status']!='passed' or \
           build['Status']!='passed' or result['ApplicationELFSHA256']!=build['ELFSHA256']:
            raise RuntimeError('Capture does not belong to the current validated UI application')
        for evidence in (host,build):
            if any(sha(ROOT/p)!=h for p,h in evidence['SourceHashes'].items()):
                raise RuntimeError('Rendering inputs have changed since validation/build')
        dll=ROOT/host['NativeLibrary']
        if sha(dll)!=host['NativeLibrarySHA256']: raise RuntimeError('Native renderer library changed')
        snapshot=(folder/'snapshot.bin').read_bytes();raw=(folder/'framebuffer.bin').read_bytes()
        header=struct.unpack_from('<8I',snapshot)
        if len(raw)!=1024*600*4 or len(snapshot)!=32+build['ModelBytes']+32 or \
           header[:4]!=(0x55495331,1,len(snapshot),build['ModelBytes']) or \
           sha(folder/'framebuffer.bin')!=result['PixelSHA256'] or \
           sha(folder/'snapshot.bin')!=result['SnapshotSHA256']:
            raise RuntimeError('Captured bytes/layout differ from their verified record')
        with os.add_dll_directory(str(gcc_directory)):
            library=ctypes.CDLL(str(dll))
            library.ui_host_from_state.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,
                ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Stats)]
            library.ui_host_from_state.restype=ctypes.c_int
            state=(ctypes.c_uint32*(build['ModelBytes']//4)).from_buffer_copy(snapshot[32:32+build['ModelBytes']])
            pixels=(ctypes.c_uint32*(1024*600))();stats=Stats()
            if not library.ui_host_from_state(state,build['ModelBytes'],header[4],header[5],pixels,ctypes.byref(stats)):
                raise RuntimeError('Native renderer rejected captured state')
            expected=bytes(pixels)
        native_stats=[getattr(stats,k) for k,_ in stats._fields_]
        board_stats=list(struct.unpack_from('<8I',snapshot,32+build['ModelBytes']))
        Image.frombytes('RGB',(1024,600),expected,'raw','BGRX').save(folder/'reconstructed.png')
        mismatches=sum(a!=b for a,b in zip(struct.unpack('<614400I',raw),struct.unpack('<614400I',expected)))
        record.update(PixelsCompared=614400,PixelMismatches=mismatches,BoardPixelSHA256=sha(folder/'framebuffer.bin'),
            NativePixelSHA256=hashlib.sha256(expected).hexdigest(),BoardGlyphStats=board_stats,
            NativeGlyphStats=native_stats,NativeLibrarySHA256=host['NativeLibrarySHA256'])
        if mismatches or board_stats!=native_stats or stats.missing_glyphs or stats.clipped_glyph_pixels:
            raise RuntimeError('Board pixels or glyph statistics do not match their exact native state')
        record.update(Status='passed',Scope='Actual DDR image/native pixel equivalence; HDMI appearance still requires user acceptance')
        print('PASS_SUBTITLE_PIXEL_EQUIVALENCE pixels=614400 mismatches=0',flush=True)
    except Exception as error:
        record.update(Status='failed',Error=str(error));print(error,flush=True)
    (folder/'pixel_comparison.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    (artifact_out/'pixel_comparison.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if record['Status']=='passed' else 1
def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path)
    parser.add_argument('--gcc-directory',type=Path,required=True);args=parser.parse_args()
    return compare(args.folder,args.gcc_directory)
if __name__=='__main__': raise SystemExit(main())
