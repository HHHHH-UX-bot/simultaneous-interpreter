"""Native network/RLE validation plus shared physical/remote UI command tests."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
from remote_display_server import decode_frame,decode_history,history_file,WIDTH,HEIGHT
import struct,csv,io
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc',required=True,type=Path);a=p.parse_args()
    out=ROOT/'output/remote_display/native';out.mkdir(parents=True,exist_ok=True)
    exe=out/'remote_test.exe';raw=out/'frame.rle'
    r=subprocess.run([str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/remote_display_test.c'),'-o',str(exe)],capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(exe),str(raw)],capture_output=True);(out/'test.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode(errors='replace'));r.check_returncode()
    decoded=decode_frame(raw.read_bytes());assert len(decoded)==WIDTH*HEIGHT*3
    for i in range(WIDTH*HEIGHT):
        expected=0x123456 if i<200000 else (0xeef3f2 if i%71 else i*17)&0xffffff
        assert decoded[i*3:i*3+3]==expected.to_bytes(3,'big')
    for corrupt in (b'\0\0abc',b'\1\0abc',b'bad'):
        try:decode_frame(corrupt)
        except ValueError:pass
        else:raise AssertionError('Invalid RLE accepted')
    def words(text):return [ord(c) for c in text]+[0]*(256-len(text))
    history_bytes=struct.pack('<8I',0x31545348,2,2072,3,42,5,0,0)
    for lang,source,target in [(1,'今天我们测试较长的中文句子。','Today we test longer Chinese sentences.'),(0,'=SUM(1,2)','测试中文，逗号和换行\n第二行')]:
        history_bytes+=struct.pack('<518I',lang,100,42,lang+1,1,1,*words(source),*words(target))
    history=decode_history(history_bytes);assert history['count']==2 and history['discarded']==3
    encoded,mime=history_file(history,'csv');rows=list(csv.reader(io.StringIO(encoded.decode('utf-8-sig'))));assert rows[1][2]==history['records'][0]['source'] and rows[2][2]=="'=SUM(1,2)" and rows[2][3].endswith('第二行')
    exported=json.loads(history_file(history,'json')[0]);assert exported['records']==history['records'] and exported['full_text']['source'].endswith('=SUM(1,2)')
    assert '原文：今天' in history_file(history,'txt')[0].decode('utf-8-sig')
    assert decode_history(struct.pack('<8I',0x31545348,0,2072,0,1,0,0,0))['records']==[]
    bad_language=bytearray(history_bytes);struct.pack_into('<I',bad_language,32,2)
    bad_unicode=bytearray(history_bytes);struct.pack_into('<I',bad_unicode,32+24,0xd800)
    for corrupt in (history_bytes[:-1],history_bytes[:32]+b'bad',struct.pack('<8I',0x31545348,65,2072,0,1,0,0,0),bad_language,bad_unicode):
        try:decode_history(corrupt)
        except ValueError:pass
        else:raise AssertionError('Invalid history accepted')
    pointer_exe=out/'pointer_test.exe'
    save=subprocess.run([sys.executable,str(ROOT/'tests/history_save_test.py')],capture_output=True);save.check_returncode();(out/'history_save.log').write_bytes(save.stdout+save.stderr);print(save.stdout.decode().strip())
    sources=['tests/remote_pointer_test.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']
    c=subprocess.run([str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DZ7_ARM_TLS','-DZ7_SOURCE_EN','-DZ7_TRANSLATE_ZH','-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'software/z7_nano_usb'),*[str(ROOT/x) for x in sources],'-o',str(pointer_exe)],capture_output=True);c.check_returncode()
    checked=subprocess.run([str(pointer_exe)],capture_output=True);checked.check_returncode();print(checked.stdout.decode().strip());(out/'pointer.log').write_bytes(checked.stdout)
    inputs=['tests/remote_pointer_test.c','software/subtitle_ui/ui_touch.c','software/subtitle_ui/ui_touch.h','software/subtitle_ui/ui_model.c','software/network_ui/remote_display.c','software/network_ui/remote_display.h','tests/remote_display_test.c','scripts/remote_display_server.py','scripts/history_save_dialog.py','assets/remote_display.html','scripts/validate_remote_display.py']
    inputs.append('tests/history_save_test.py')
    record=dict(status='passed',checks=r.stdout.decode().strip(),pointer_checks=checked.stdout.decode().strip(),history_save_checks=save.stdout.decode().strip(),pixels_verified=WIDTH*HEIGHT,scope='real C encoder/ARP/IP/UDP and Python decoder, synthetic pixels; hardware and mouse UI separately verified',input_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in inputs})
    (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS_REMOTE_DISPLAY_NATIVE')
if __name__=='__main__':main()
