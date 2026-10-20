"""Encode 16 kHz mono English audio with the firmware's Opus/Ogg code."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import wave
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc',required=True,type=Path)
    p.add_argument('--mode',required=True,choices=('enzh','enen'))
    p.add_argument('--input',type=Path,default=ROOT/'output/selftest_en/reference.wav')
    p.add_argument('--output-dir',type=Path);a=p.parse_args()
    source=a.input
    with wave.open(str(source),'rb') as wav:
        if (wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getcomptype())!=(1,2,16000,'NONE'):
            p.error('Input must be uncompressed 16 kHz mono signed 16-bit WAV')
        pcm=wav.readframes(wav.getnframes())
    source_hash=hashlib.sha256(pcm).hexdigest()
    temp=ROOT/'tmp/english_opus'/a.mode;temp.mkdir(parents=True,exist_ok=True)
    flags=['-shared','-O2','-DHAVE_CONFIG_H','-Wl,--export-all-symbols']
    if a.mode=='enzh':flags.append('-DZ7_TRANSLATE_AUDIO')
    for d in ['','include','config','celt','silk','silk/fixed']:flags+=['-I',str(ROOT/'third_party/opus'/d)]
    sources=[ROOT/'software/network_ui/opus_codec.c',ROOT/'software/network_ui/ogg_opus.c',*runpy.run_path(str(ROOT/'scripts/build_cloud_app.py'))['opus_sources']()]
    dll=temp/'firmware_codec.dll'
    r=subprocess.run([str(a.gcc),*flags,*map(str,sources),'-o',str(dll)],capture_output=True,timeout=180)
    (temp/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    lib=c.CDLL(str(dll));lib.opus_codec_init.restype=c.c_int;assert lib.opus_codec_init()==1
    lib.opus_codec_encode.argtypes=[c.POINTER(c.c_int16),c.POINTER(c.c_uint8),c.c_uint32]
    for name in ['ogg_opus_head','ogg_opus_tags']:getattr(lib,name).argtypes=[c.POINTER(c.c_uint8),c.c_uint32,c.c_uint32]
    lib.ogg_opus_page.argtypes=[c.POINTER(c.c_uint8),c.c_uint32,c.c_int,c.c_uint32,c.c_uint32,c.c_uint32,c.POINTER(c.POINTER(c.c_uint8)),c.POINTER(c.c_uint8),c.c_uint32]
    pcm+=bytes(32000*3)
    out=(c.c_uint8*4096)();pages=[]
    for name in ['ogg_opus_head','ogg_opus_tags']:
        n=getattr(lib,name)(out,len(out),0x5a374e32);assert n>0;pages.append({'at':0,'hex':bytes(out[:n]).hex()})
    packets=[];frames=(len(pcm)+639)//640
    for i in range(frames):
        raw=(c.c_int16*320).from_buffer_copy(pcm[i*640:(i+1)*640].ljust(640,b'\0'));packet=(c.c_uint8*128)()
        n=lib.opus_codec_encode(raw,packet,128);assert 0<n<128;packets.append(bytes(packet[:n]))
        if len(packets)==10 or i==frames-1:
            buffers=[(c.c_uint8*len(x)).from_buffer_copy(x) for x in packets]
            pointers=(c.POINTER(c.c_uint8)*len(buffers))(*[c.cast(x,c.POINTER(c.c_uint8)) for x in buffers])
            lengths=(c.c_uint8*len(buffers))(*map(len,packets))
            n=lib.ogg_opus_page(out,len(out),0,(i+1)*960,0x5a374e32,len(pages),pointers,lengths,len(packets));assert n>0
            pages.append({'at':(i+1)*.02,'hex':bytes(out[:n]).hex()});packets=[]
    record={'mode':a.mode,'dtx':a.mode=='enzh','scope':'supplied WAV through matching fixed-point firmware codec/Ogg, 24 kbit/s; PC API check only','seconds':frames*.02,'input_pcm_sha256':source_hash,'source_pcm_sha256':hashlib.sha256(pcm).hexdigest(),'pages':pages}
    out=a.output_dir or ROOT/'output'/a.mode;out.mkdir(parents=True,exist_ok=True)
    (out/'opus_pages.json').write_text(json.dumps(record)+'\n',encoding='utf-8')
    print('PASS_REAL_ENGLISH_OPUS',len(pages),'pages',frames,'frames')
if __name__=='__main__':main()
