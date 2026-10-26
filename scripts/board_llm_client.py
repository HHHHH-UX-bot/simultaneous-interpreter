#!/usr/bin/env python3
"""Submit arbitrary UTF8 text to the board's real Qwen inference endpoint.

Only Python's standard library is required. Upload validated model/tokenizer
profiles separately on 4178/4179 before submitting a job on 4190.
"""
import argparse
import codecs
import json
import pathlib
import random
import socket
import struct
import sys
import time
import zlib

MAGIC=0x314c4254
STATUS,BEGIN,DATA,RUN,GET,CANCEL,RELEASE=range(1,8)
VOICE=8
NETWORK=10
PHASES=['wait_assets','ready','receiving','queued','prefill','generating','done','failed','cancelled']
FIELDS=['version','state','flags','job','input_bytes','system_bytes','received','prompt_tokens','generated','max_tokens','output_bytes','position','layer','error','finish_reason','detail']

class Client:
    def __init__(self,board,bind='',local_port=4191,port=4190):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind((bind,local_port));self.sock.connect((board,port))
        self.sock.settimeout(.5);self.sequence=0
    def close(self):self.sock.close()
    def request(self,op,job=0,offset=0,value=0,data=b'',retries=12):
        self.sequence=(self.sequence+1)&0xffffffff
        header=struct.pack('<7I',MAGIC,op,self.sequence,job,offset,value,len(data))
        packet=header+struct.pack('<I',zlib.crc32(header+data)&0xffffffff)+data
        for _ in range(retries):
            self.sock.send(packet)
            deadline=time.perf_counter()+.5
            while time.perf_counter()<deadline:
                try:reply=self.sock.recv(2048)
                except socket.timeout:break
                if len(reply)<96:continue
                h=struct.unpack_from('<8I',reply)
                if h[0]!=MAGIC or h[1]!=(op|0x80) or h[2]!=self.sequence or h[6]!=len(reply)-32:continue
                if h[7]!=(zlib.crc32(reply[:28]+reply[32:])&0xffffffff):continue
                info=dict(zip(FIELDS,struct.unpack_from('<16I',reply,32)))
                if info['detail']&0x80000000:info['detail']-=0x100000000
                info['phase']=PHASES[info['state']] if info['state']<len(PHASES) else 'unknown'
                payload=reply[96:]
                if h[5]:raise RuntimeError(f"board error {h[5]}: {payload.decode('utf-8',errors='replace')} (status={json.dumps(info)})")
                return info,payload
        raise TimeoutError(f'No valid board reply for operation {op}; source port {self.sock.getsockname()[1]} must remain stable')
    def status(self):return self.request(STATUS)[0]
    def network(self):
        _,data=self.request(NETWORK)
        if len(data)<8:raise RuntimeError('Incomplete network identity')
        connected,size=struct.unpack_from('<2I',data)
        if connected not in (0,1) or size>32 or len(data)!=8+size:raise RuntimeError('Invalid network identity')
        return dict(wifi_connected=bool(connected),wifi_name=data[8:].decode('utf-8',errors='replace') if connected else '')
    def voice(self):
        _,data=self.request(VOICE)
        if len(data)!=96:raise RuntimeError('Voice status requires the current voice firmware')
        words=struct.unpack('<24I',data)
        if words[0]!=0x31564c51:raise RuntimeError('Invalid public voice status')
        names=['magic','state','role','error','speech_started_ms','speech_stopped_ms','asr_final_ms','model_start_ms','first_text_ms','model_done_ms','first_pcm_ms','audio_done_ms','dac_start_ms','dac_done_ms','text_bytes','finish_sent_ms','asr_count','tts_count','spoken_count','cloud_errors','now_ms','pcm_received','pcm_played','dac_underruns']
        return dict(zip(names,words))
    def submit(self,text,system='',maximum=128,job=None):
        inp=text.encode('utf-8');sys_bytes=system.encode('utf-8')
        if not inp or len(inp)>2048:raise ValueError('input must contain 1..2048 UTF8 bytes')
        if len(sys_bytes)>512:raise ValueError('system must contain at most 512 UTF8 bytes')
        if '\0' in text or '\0' in system:raise ValueError('NUL bytes are unsupported')
        if not 1<=maximum<=256:raise ValueError('max tokens must be 1..256')
        job=job or random.SystemRandom().randrange(1,0x100000000)
        info,_=self.request(BEGIN,job,value=maximum,data=struct.pack('<2I',len(inp),len(sys_bytes)))
        content=sys_bytes+inp
        for offset in range(info['received'],len(content),1024):
            self.request(DATA,job,offset=offset,data=content[offset:offset+1024])
        self.request(RUN,job)
        return job
    def output(self,job,offset=0):return self.request(GET,job,offset=offset,value=1024)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board',default='192.168.1.50');p.add_argument('--bind',default='')
    p.add_argument('--port',type=int,default=4190);p.add_argument('--local-port',type=int,default=4191)
    p.add_argument('--status',action='store_true');p.add_argument('--cancel',action='store_true');p.add_argument('--release',action='store_true');p.add_argument('--fetch',action='store_true')
    p.add_argument('--job',type=lambda s:int(s,0));p.add_argument('--text');p.add_argument('--text-file',type=pathlib.Path)
    p.add_argument('--system',default='');p.add_argument('--system-file',type=pathlib.Path)
    p.add_argument('--max-tokens',type=int,default=128);p.add_argument('--timeout',type=float,default=3600)
    p.add_argument('--poll',type=float,default=.5);p.add_argument('--output',type=pathlib.Path)
    args=p.parse_args()
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
    if hasattr(sys.stderr,'reconfigure'):sys.stderr.reconfigure(encoding='utf-8')
    client=Client(args.board,args.bind,args.local_port,args.port)
    try:
        if args.status:
            print(json.dumps(client.status(),ensure_ascii=False,indent=2));return
        if args.cancel or args.release:
            if not args.job:p.error('--cancel/--release requires --job')
            print(json.dumps(client.request(CANCEL if args.cancel else RELEASE,args.job)[0],indent=2));return
        text=args.text_file.read_text(encoding='utf-8') if args.text_file else args.text
        system=args.system_file.read_text(encoding='utf-8') if args.system_file else args.system
        if args.fetch:
            if not args.job:p.error('--fetch requires --job')
            job=args.job
        else:
            if text is None:p.error('provide --text or --text-file')
            job=client.submit(text,system,args.max_tokens,args.job)
        print(f'job={job} source_port={args.local_port}; inference runs on board',file=sys.stderr)
        deadline=time.perf_counter()+args.timeout;offset=0;raw=bytearray();last=None
        decoder=codecs.getincrementaldecoder('utf-8')('replace')
        while time.perf_counter()<deadline:
            info,part=client.output(job,offset)
            if part:
                offset+=len(part);raw.extend(part)
                print(decoder.decode(part,final=False),end='',flush=True)
            summary=(info['phase'],info['position'],info['layer'],info['generated'])
            if summary!=last:
                print(f"\r{info['phase']} prompt={info['prompt_tokens']} generated={info['generated']} pos={info['position']} layer={info['layer']}   ",end='',file=sys.stderr,flush=True)
                last=summary
            if info['state'] in (6,7,8) and offset>=info['output_bytes']:
                print(decoder.decode(b'',final=True));print(file=sys.stderr)
                if args.output:args.output.write_bytes(raw)
                print(json.dumps(info,ensure_ascii=False),file=sys.stderr)
                if info['state']==7:raise RuntimeError(f"inference failed: error {info['error']}, detail {info['detail']}")
                return
            if not part:time.sleep(max(.05,args.poll))
        raise TimeoutError(f'Inference timeout; job {job} remains available. Use the same --local-port to cancel or fetch it.')
    finally:client.close()

if __name__=='__main__':
    try:main()
    except (OSError,RuntimeError,TimeoutError,ValueError) as exc:
        print(f'ERROR: {exc}',file=sys.stderr);raise SystemExit(1)
