"""Local browser mirror over the board's direct PL Ethernet cable, no JTAG.

The board supplies real framebuffer pixels; clicks go to the shared C hit test.
Only a loopback HTTP listener is exposed. Cloud/Wi-Fi credentials are unused.
"""
import argparse,json,secrets,socket,struct,threading,time,io,csv,datetime,subprocess,sys,os,tempfile
from urllib.parse import urlsplit,parse_qs
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from PIL import Image
MAGIC=0x3152375a;CHUNK=1400;WIDTH=1024;HEIGHT=600;MAX=WIDTH*HEIGHT*5

def decode_frame(data):
    if len(data)%5 or len(data)>MAX:raise ValueError('Invalid RLE length')
    pixels=bytearray()
    for at in range(0,len(data),5):
        count=data[at]|data[at+1]<<8
        if not count or len(pixels)+count*3>WIDTH*HEIGHT*3:raise ValueError('Invalid RLE run')
        pixels.extend(data[at+2:at+5]*count)
    if len(pixels)!=WIDTH*HEIGHT*3:raise ValueError('Incomplete image')
    return bytes(pixels)

def decode_history(data):
    if len(data)<32:raise ValueError('Incomplete history')
    magic,count,stride,discarded,epoch,final_count,_,_=struct.unpack_from('<8I',data)
    if magic!=0x31545348 or count>64 or stride!=2072 or len(data)!=32+count*stride:raise ValueError('Invalid history layout')
    def text(words):
        chars=[]
        for c in words:
            if not c:break
            if c>0x10ffff or 0xd800<=c<=0xdfff:raise ValueError('Invalid history Unicode')
            chars.append(chr(c))
        return ''.join(chars)
    records=[]
    for i in range(count):
        words=struct.unpack_from('<518I',data,32+i*stride)
        if words[0]>1:raise ValueError('Invalid history language')
        records.append(dict(number=i+1,direction='中译英' if words[0] else '英译中',epoch=words[2],segment_id=words[3],revision=words[4],source_revision=words[5],source=text(words[6:262]),translation=text(words[262:518])))
    return dict(exported_at=datetime.datetime.now().astimezone().isoformat(timespec='seconds'),count=count,discarded=discarded,epoch=epoch,final_count=final_count,records=records)

def history_file(history,kind):
    if kind=='json':return (json.dumps(history,ensure_ascii=False,indent=2)+'\n').encode('utf-8'),'application/json; charset=utf-8'
    if kind=='csv':
        out=io.StringIO(newline='');writer=csv.writer(out)
        writer.writerow(['序号','翻译方向','原文','译文','会话','片段','原文版本','译文版本','导出时间','更早已丢弃条数'])
        # Prevent spreadsheet formulas in recognized speech from executing on open.
        def cell(value):return "'"+value if value.lstrip().startswith(('=','+','-','@')) else value
        for r in history['records']:writer.writerow([r['number'],r['direction'],cell(r['source']),cell(r['translation']),r['epoch'],r['segment_id'],r['source_revision'],r['revision'],history['exported_at'],history['discarded']])
        return out.getvalue().encode('utf-8-sig'),'text/csv; charset=utf-8'
    if kind=='txt':
        lines=['译声 · 已定稿翻译历史',f"导出时间：{history['exported_at']}",f"当前保留 {history['count']} 条；更早已丢弃 {history['discarded']} 条。",'']
        for r in history['records']:lines.extend([f"{r['number']}. {r['direction']}",'原文：'+r['source'],'译文：'+r['translation'],''])
        return ('\r\n'.join(lines)+'\r\n').encode('utf-8-sig'),'text/plain; charset=utf-8'
    raise ValueError('请选择 CSV、TXT 或 JSON')

def save_history_dialog(board,kind):
    if kind not in ('csv','txt','json'):raise ValueError('Unknown export format')
    result=subprocess.run([sys.executable,str(Path(__file__).with_name('history_save_dialog.py')),kind],capture_output=True,check=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    chosen=json.loads(result.stdout.decode('utf-8'))
    if not chosen:return dict(cancelled=True)
    data,_=history_file(board.history(),kind);target=Path(chosen)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent,prefix='.z7-export-',delete=False) as f:temporary=Path(f.name);f.write(data)
        os.replace(temporary,target)
    finally:
        if temporary and temporary.exists():temporary.unlink()
    return dict(saved=True,path=str(target))

class Board:
    def __init__(self,local,host):
        self.host=host;self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.bind((local,0));self.sock.settimeout(.15)
        self.cookie=struct.unpack('<II',secrets.token_bytes(8));self.sequence=0;self.lock=threading.Lock();self.action=threading.Lock();self.control=threading.Event()
        self.frame=0;self.epoch=0;self.png=None;self.rgb=None;self.snapshot=(0,0,None,None);self.last_seen=0;self.error='正在连接板子';self.controls=0;self.stop=False
    def request(self,op,frame=0,a=0,b=0,payload=b''):
        with self.lock:
            self.sequence=(self.sequence+1)&0x7fffffff or 1;seq=self.sequence
            packet=struct.pack('<8I',MAGIC,op,*self.cookie,seq,frame,a,b)+payload
            for attempt in range(4):
                self.sock.sendto(packet,(self.host,4177));end=time.monotonic()+.15
                while time.monotonic()<end:
                    try:data,address=self.sock.recvfrom(1536)
                    except socket.timeout:break
                    if address!=(self.host,4177) or len(data)<32:continue
                    h=struct.unpack_from('<8I',data)
                    if h[0]!=MAGIC or h[2:4]!=self.cookie or h[4]!=seq:continue
                    self.last_seen=time.monotonic();return h,data[32:]
            raise TimeoutError('等待板子响应超时')
    def run(self):
        while not self.stop:
            try:
                while self.control.is_set():time.sleep(.002)
                h,_=self.request(1,self.frame)
                if h[1]!=0x81:raise ValueError('Unexpected status')
                frame,size,epoch=h[5:8]
                if frame and frame!=self.frame:
                    if not 0<size<=MAX:raise ValueError('Frame size out of bounds')
                    raw=bytearray()
                    for offset in range(0,size,CHUNK):
                        while self.control.is_set():time.sleep(.002)
                        r,data=self.request(2,frame,offset)
                        if r[1]!=0x82 or r[5]!=frame or r[6]!=offset or r[7]!=size or len(data)!=min(CHUNK,size-offset):raise ValueError('Frame changed, retry')
                        raw.extend(data);time.sleep(.0005)
                    rgb=decode_frame(raw);output=io.BytesIO();Image.frombytes('RGB',(WIDTH,HEIGHT),rgb).save(output,format='PNG',compress_level=1)
                    self.rgb=rgb;self.png=output.getvalue();self.epoch=epoch;self.frame=frame;self.snapshot=(frame,epoch,self.png,rgb)
                self.error='';time.sleep(.05)
            except (OSError,ValueError,TimeoutError) as exc:self.error=str(exc);time.sleep(.15)
    def pointer(self,p):
        values=[int(p[k]) for k in ('press_x','press_y','x','y')];epoch=int(p['epoch'])
        if any(v<0 or v>=(WIDTH if i%2==0 else HEIGHT) for i,v in enumerate(values)):raise ValueError('坐标超出画面')
        self.action.acquire();self.control.set()
        try:
            h,_=self.request(3,payload=struct.pack('>4H',*values)+struct.pack('<I',epoch))
            if h[1]!=0x83:raise ValueError('Unexpected control acknowledgement')
            self.controls+=1;return dict(accepted=bool(h[5]),command=h[5],epoch=h[6],sequence=h[4])
        finally:self.control.clear();self.action.release()
    def history(self):
        with self.action:
            self.control.set()
            try:
                h,_=self.request(4);identity,size,count=h[5:8]
                if h[1]!=0x84 or count>64 or size!=32+count*2072:raise ValueError('Invalid history snapshot')
                raw=bytearray()
                for offset in range(0,size,CHUNK):
                    r,data=self.request(5,identity,offset)
                    if r[1]!=0x85 or r[5]!=identity or r[6]!=offset or r[7]!=size or len(data)!=min(CHUNK,size-offset):raise ValueError('History snapshot changed')
                    raw.extend(data)
                return decode_history(raw)
            finally:self.control.clear()
    def state(self):return dict(connected=time.monotonic()-self.last_seen<3,frame=self.snapshot[0],epoch=self.snapshot[1],error=self.error,controls=self.controls,board=self.host)

def main():
    p=argparse.ArgumentParser();p.add_argument('--local-ip',default='192.168.1.102');p.add_argument('--board-ip',default='192.168.1.50');p.add_argument('--port',type=int,default=8787);a=p.parse_args()
    board=Board(a.local_ip,a.board_ip);threading.Thread(target=board.run,daemon=True).start()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,data,mime='application/json',extra=None):
            self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');
            for k,v in (extra or {}).items():self.send_header(k,str(v))
            self.end_headers();self.wfile.write(data)
        def do_GET(self):
            path=self.path.split('?')[0]
            if path=='/':self.reply(200,(Path(__file__).resolve().parents[1]/'assets/remote_display.html').read_bytes(),'text/html; charset=utf-8')
            elif path=='/state':self.reply(200,json.dumps(board.state(),ensure_ascii=False).encode())
            elif path=='/frame.png' and board.snapshot[2]:
                frame,epoch,png,_=board.snapshot;self.reply(200,png,'image/png',{'X-Frame-ID':frame,'X-UI-Epoch':epoch})
            elif path=='/history':
                try:
                    kind=parse_qs(urlsplit(self.path).query).get('format',['csv'])[0];data,mime=history_file(board.history(),kind)
                    self.reply(200,data,mime,{'Content-Disposition':'attachment; filename="translation-history.'+kind+'"'})
                except (ValueError,TimeoutError,OSError) as exc:self.reply(503,json.dumps(dict(error=str(exc)),ensure_ascii=False).encode())
            else:self.reply(503,b'{"error":"frame not ready"}')
        def do_POST(self):
            if self.path not in ('/pointer','/save-history'):self.reply(404,b'{}');return
            origin=self.headers.get('Origin');expected='http://127.0.0.1:'+str(a.port)
            if origin and origin not in (expected,'http://localhost:'+str(a.port)):self.reply(403,b'{}');return
            try:
                n=int(self.headers.get('Content-Length','0'))
                if not 0<n<=512:raise ValueError('Invalid request length')
                data=json.loads(self.rfile.read(n));result=board.pointer(data) if self.path=='/pointer' else save_history_dialog(board,data['format']);self.reply(200,json.dumps(result,ensure_ascii=False).encode())
            except (ValueError,KeyError,TimeoutError,OSError,subprocess.SubprocessError) as exc:self.reply(400,json.dumps(dict(error=str(exc)),ensure_ascii=False).encode())
    server=ThreadingHTTPServer(('127.0.0.1',a.port),Handler)
    print(f'REMOTE_DISPLAY_READY http://127.0.0.1:{a.port}',flush=True)
    try:server.serve_forever()
    finally:board.stop=True;board.sock.close();server.server_close()
if __name__=='__main__':main()
