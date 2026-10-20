"""PC-only English audio API smoke test using Python's verified TLS.

Uses only process Z7_CLOUD_KEY; never touches a board or writes credentials.
Firmware TLS/ESP/HDMI behavior must still be tested separately on hardware.
"""
import argparse
import base64
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import ssl
import struct
import time

ROOT=Path(__file__).resolve().parents[1]
class WebSocket:
    def __init__(self,host,path,key):
        self.sock=ssl.create_default_context().wrap_socket(socket.create_connection((host,443),timeout=15),server_hostname=host)
        self.sock.settimeout(.05);self.buf=bytearray();self.fragments=bytearray()
        nonce=base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(('GET '+path+' HTTP/1.1\r\nHost: '+host+'\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: '+nonce+'\r\nSec-WebSocket-Version: 13\r\nAuthorization: Bearer '+key+'\r\n\r\n').encode())
        deadline=time.monotonic()+15
        while b'\r\n\r\n' not in self.buf:
            assert time.monotonic()<deadline,'WebSocket upgrade timed out'
            try:self.buf.extend(self.sock.recv(8192))
            except socket.timeout:continue
        head,tail=bytes(self.buf).split(b'\r\n\r\n',1);self.buf=bytearray(tail)
        assert head.splitlines()[0].split()[1]==b'101','WebSocket upgrade rejected'
        fields={k.lower():v for line in head.splitlines()[1:] if b':' in line for k,v in [line.split(b':',1)]}
        expected=base64.b64encode(hashlib.sha1((nonce+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert fields.get(b'sec-websocket-accept',b'').strip()==expected
    def send(self,payload,opcode=1):
        if not isinstance(payload,bytes):payload=json.dumps(payload,separators=(',',':')).encode()
        n=len(payload);mask=os.urandom(4)
        h=bytes([0x80|opcode,0x80|n]) if n<126 else bytes([0x80|opcode,0xfe])+struct.pack('>H',n) if n<65536 else bytes([0x80|opcode,0xff])+struct.pack('>Q',n)
        self.sock.sendall(h+mask+bytes(v^mask[i&3] for i,v in enumerate(payload)))
    def fill(self,n):
        while len(self.buf)<n:
            try:
                b=self.sock.recv(65536)
                if not b:raise RuntimeError('WebSocket closed')
                self.buf.extend(b)
            except socket.timeout:return False
        return True
    def event(self):
        if not self.fill(2):return None
        first,second=self.buf[:2];n=second&127;at=2
        if n in (126,127):
            extra=2 if n==126 else 8
            if not self.fill(2+extra):return None
            n=int.from_bytes(self.buf[2:2+extra],'big');at+=extra
        assert n<2*1024*1024 and not second&128,'Invalid server frame'
        if not self.fill(at+n):return None
        b=bytes(self.buf[at:at+n]);del self.buf[:at+n]
        opcode=first&15
        if opcode==8:raise RuntimeError('WebSocket close received')
        if opcode==9:self.send(b,10);return None
        if opcode==10:return None
        if opcode in (0,1):
            self.fragments.extend(b)
            if first&128:
                b=bytes(self.fragments);self.fragments.clear();return json.loads(b)
        return None
    def close(self):
        try:self.send(struct.pack('>H',1000),8)
        except OSError:pass
        self.sock.close()

def main():
    p=argparse.ArgumentParser();p.add_argument('--mode',required=True,choices=('enzh','enen'))
    p.add_argument('--audio-pages',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    key=os.environ.get('Z7_CLOUD_KEY','');assert key,'Z7_CLOUD_KEY required in process environment'
    host=re.search(r'#define HOST "([^"]+)"',(ROOT/'software/network_ui/arm_tls.c').read_text(encoding='utf-8')).group(1)
    model='qwen3-asr-flash-realtime' if a.mode=='enen' else 'qwen3.5-livetranslate-flash-realtime'
    session=json.loads((ROOT/'output'/a.mode/'validation/session.json').read_text(encoding='utf-8'))
    pages_path=a.audio_pages or ROOT/'output'/a.mode/'opus_pages.json'
    pages=json.loads(pages_path.read_text(encoding='utf-8'))
    if pages['mode']!=a.mode:p.error('Audio pages mode differs from selected session')
    ws=WebSocket(host,'/api-ws/v1/realtime?model='+model,key)
    record={'mode':a.mode,'utc':datetime.now(timezone.utc).isoformat(),'scope':'PC verified TLS + cloud on supplied audio encoded by firmware Opus/Ogg; ESP and board TLS are bypassed; microphone provenance is separate','audio_pages_sha256':hashlib.sha256(pages_path.read_bytes()).hexdigest(),'input_pcm_sha256':pages.get('input_pcm_sha256'),'asr_finals':[],'translation_finals':[],'audio_bytes':0,'audio_done':0,'errors':[],'session':session}
    deadline=time.monotonic()+15;configured=False;cursor=0
    try:
        while time.monotonic()<deadline:
            e=ws.event()
            if not e:continue
            if e['type']=='session.created':ws.send({'event_id':'english_config','type':'session.update','session':session})
            elif e['type']=='session.updated':configured=True;break
            elif e['type']=='error':record['errors'].append(e.get('error',{}).get('code','configuration_error'));break
        if configured:
            start=time.monotonic();cursor=0;deadline=start+pages['seconds']+12
            while time.monotonic()<deadline:
                elapsed=time.monotonic()-start
                if cursor<len(pages['pages']) and elapsed>=pages['pages'][cursor]['at']:
                    page=pages['pages'][cursor]
                    ws.send({'event_id':'english_%d'%cursor,'type':'input_audio_buffer.append','audio':base64.b64encode(bytes.fromhex(page['hex'])).decode()});cursor+=1
                e=ws.event()
                if not e:continue
                kind=e['type']
                if kind=='conversation.item.input_audio_transcription.completed':record['asr_finals'].append(e.get('transcript',''))
                elif kind in ('response.text.done','response.audio_transcript.done'):record['translation_finals'].append(e.get('text',e.get('transcript','')))
                elif kind=='response.audio.delta':record['audio_bytes']+=len(base64.b64decode(e['delta']))
                elif kind=='response.audio.done':record['audio_done']+=1
                elif kind=='error':record['errors'].append(e.get('error',{}).get('code','cloud_error'));break
                expected_finals=1 if a.audio_pages else 4
                if cursor==len(pages['pages']) and len(record['asr_finals'])>=expected_finals and (a.mode=='enen' or record['audio_done']>=expected_finals):break
        record['session_acknowledged']=configured
        record['sent_pages']=cursor;record['total_pages']=len(pages['pages'])
        record['passed']=configured and not record['errors'] and cursor==len(pages['pages']) and any(s.strip() for s in record['asr_finals'])
        if not a.audio_pages:
            record['passed']=record['passed'] and len(record['asr_finals'])>=3 and any('morning' in s.lower() for s in record['asr_finals'])
        record['acceptance']='ASR output on supplied recording; text quality needs separate review' if a.audio_pages else 'English reference fixture: >=3 finals including morning'
        if a.mode=='enzh':record['passed']=record['passed'] and record['audio_bytes']>0 and any(re.search('[\u4e00-\u9fff]',s) for s in record['translation_finals'])
        out=a.output or ROOT/'output'/a.mode/'validation/cloud_smoke.json'
        out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'mode':a.mode,'passed':record['passed'],'asr_finals':len(record['asr_finals']),'translation_finals':len(record['translation_finals']),'audio_bytes':record['audio_bytes'],'errors':record['errors']},ensure_ascii=False),flush=True)
        if not record['passed']:raise SystemExit(1)
    finally:ws.close()
if __name__=='__main__':main()
