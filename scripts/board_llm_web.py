#!/usr/bin/env python3
"""Local UI for board-only Qwen inference; no PC model runtime.

python board_llm_web.py --board 192.168.1.50 --bind 192.168.1.10
HTTP listens exclusively on 127.0.0.1:8790. One worker owns UDP source port4191.
"""
import argparse
import codecs
import json
import queue
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from board_llm_client import Client,CANCEL

HTML='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>板上 Qwen · 任意文本</title>
<style>body{font:16px/1.6 system-ui,sans-serif;background:#111827;color:#e5e7eb;margin:0}main{max-width:1050px;margin:32px auto;padding:0 22px}h1{font-size:28px;margin-bottom:4px}p{color:#aab7cb}a{color:#93c5fd}section{background:#1f2937;padding:22px;border-radius:14px;margin:20px 0}label{display:block;margin:12px 0 6px}textarea,input,select{box-sizing:border-box;border:1px solid #53627a;border-radius:8px;background:#111827;color:#f9fafb;padding:10px;font:inherit}textarea{width:100%;resize:vertical}select{min-width:180px}button{font:inherit;border:0;border-radius:8px;padding:10px 22px;margin:14px 10px 0 0;background:#60a5fa;color:#0f172a;cursor:pointer}button.secondary{background:#475569;color:white}button:disabled{opacity:.4;cursor:default}.row{display:flex;gap:24px;flex-wrap:wrap;align-items:flex-end}.small{font-size:13px;color:#aab7cb}.status{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:14px}.status b{display:block;color:#fff;font-size:19px}#output{white-space:pre-wrap;overflow-wrap:anywhere;min-height:120px;margin:0;font:18px/1.9 system-ui,sans-serif}#error{white-space:pre-wrap;color:#fca5a5}.pill{display:inline-block;border:1px solid #475569;border-radius:20px;padding:2px 12px;color:#bfdbfe}summary{cursor:pointer}input[type=number]{width:100px}footer{font-size:13px;color:#94a3b8;margin-bottom:30px}</style>
<main><h1>板上 Qwen <span class="pill" id="phase">正在连接</span></h1><p>任意中英文本翻译，也可用自定义 system 提问。模型前向计算与生成都在 Zynq 板上；本页负责提交和显示。</p>
<p id="network" role="status">当前网络名称：等待板卡连接</p>
<section><div class="status"><div>连接 / 资产<b id="assets">等待状态</b></div><div>输入 token<b id="promptCount">—</b></div><div>生成 token<b id="generated">—</b></div><div>当前位置 / 层<b id="progress">—</b></div></div><div id="error" role="alert"></div></section>
<section><div class="row"><div><label for="direction">任务</label><select id="direction"><option value="en_zh">英语 → 简体中文</option><option value="zh_en">中文 → 英语</option><option value="free">通用问答</option></select></div><div><label for="maximum">最多生成 token</label><input id="maximum" type="number" min="1" max="256" value="128"></div></div>
<label for="text">输入文本 <span class="small" id="textSize">0 / 2048 UTF8 字节</span></label><textarea id="text" rows="6" placeholder="输入任何需要翻译的句子；无需注册短句或录入模板。"></textarea>
<details><summary>可选自定义 system（填写后覆盖上面的任务设定）</summary><textarea id="system" rows="3" placeholder="最多 512 UTF8 字节"></textarea><span class="small" id="systemSize">0 / 512 UTF8 字节</span></details>
<button id="start" disabled>开始板上生成</button><button id="cancel" class="secondary" disabled>取消当前任务</button><div class="small">上下文共 512 token（含 system、输入和预留生成）。超限会显示错误；可缩短文本或减少生成上限。</div></section>
<section><div class="row" style="justify-content:space-between"><h2 style="margin-top:0">输出</h2><button id="copy" class="secondary" style="margin-top:0">复制</button></div><pre id="output" aria-live="polite">尚无输出。</pre></section>
<footer>UDP 4190 · 单任务 · 小模型生成可能较慢。取消可能需等待数秒。麦克风识别与播报调用百炼云，翻译在开发板运行；播报结束后可继续输入。 <a href="http://127.0.0.1:8787" target="_blank" rel="noopener">打开原有板屏镜像</a>。当前页面不会占用屏幕镜像会话。</footer></main>
<script>const el=id=>document.getElementById(id),enc=new TextEncoder();let snapshot={},sending=false;
const names={wait_assets:'等待上传模型',ready:'模型就绪',receiving:'接收文本',queued:'等待执行',prefill:'处理输入',generating:'正在生成',done:'生成结束',failed:'任务失败',cancelled:'已取消',connecting:'正在连接',queued_local:'准备提交',cancel_requested:'正在取消',connection_error:'连接异常'};
function counts(){let n=enc.encode(el('text').value).length,s=enc.encode(el('system').value).length;el('textSize').textContent=n+' / 2048 UTF8 字节';el('systemSize').textContent=s+' / 512 UTF8 字节';let running=[2,3,4,5].includes(snapshot.state)||snapshot.pending;let busy=running||!!(snapshot.flags&128);el('start').disabled=sending||!snapshot.connected||!(snapshot.flags&4)||busy||!n||n>2048||s>512;el('cancel').disabled=sending||!running||!!(snapshot.flags&64);}
async function action(path,data){sending=true;counts();try{let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-Board-LLM':'1'},body:JSON.stringify(data)}),j=await r.json();if(!r.ok)throw Error(j.error||r.statusText);snapshot=j;render();}catch(e){el('error').textContent=e.message;}finally{sending=false;counts();}}
function render(){el('network').textContent='当前网络名称：'+(!snapshot.connected?'等待板卡连接':snapshot.wifi_connected?snapshot.wifi_name:'未连接 Wi-Fi');el('phase').textContent=(snapshot.flags&256?'语音异常':snapshot.flags&128?'译音处理中':names[snapshot.phase])||snapshot.phase||'等待';el('assets').textContent=!snapshot.connected?'离线':snapshot.flags&4?'模型 / 分词器就绪':snapshot.flags&3?'资产上传中':'等待资产';el('promptCount').textContent=snapshot.prompt_tokens||0;el('generated').textContent=(snapshot.generated||0)+' / '+(snapshot.max_tokens||0);el('progress').textContent=(snapshot.position||0)+' / '+(snapshot.layer||0);el('output').textContent=snapshot.output||'尚无输出。';el('error').textContent=snapshot.error_text||'';counts();}
el('text').addEventListener('input',counts);el('system').addEventListener('input',counts);el('start').onclick=()=>action('/api/start',{text:el('text').value,system:el('system').value,direction:el('direction').value,max_tokens:Number(el('maximum').value)});el('cancel').onclick=()=>action('/api/cancel',{});el('copy').onclick=()=>navigator.clipboard.writeText(snapshot.output||'');async function poll(){try{let r=await fetch('/api/status',{cache:'no-store'});snapshot=await r.json();render();}catch(e){el('error').textContent='本地网页服务未连接：'+e.message;el('network').textContent='当前网络名称：等待板卡连接';}setTimeout(poll,400);}poll();</script></html>'''

SYSTEMS={
    'en_zh':"Translate English to Chinese. Output only translation.",
    'zh_en':"Translate Chinese to English. Output only translation.",
    'free':'You are a helpful assistant. Answer the user accurately.',
}
ERRORS={11:'输入 token 与预留生成长度超过 512。请缩短文本或减少生成上限。',12:'板上模型初始化失败。',13:'板上模型前向计算失败。',14:'分词或 UTF8 解码失败。',15:'输出超过缓冲区上限。',16:'模型未产生有效 logits。'}

class Controller:
    def __init__(self,client_factory):
        self.client_factory=client_factory;self.lock=threading.Lock();self.commands=queue.Queue(maxsize=8)
        self.stop=threading.Event();self.client=None;self.job=0;self.raw=bytearray();self.decoder=codecs.getincrementaldecoder('utf-8')('replace')
        self.state=dict(connected=False,phase='connecting',state=0,flags=0,job=0,prompt_tokens=0,generated=0,max_tokens=0,position=0,layer=0,output='',error_text='',pending=False,wifi_connected=False,wifi_name='')
        self.thread=threading.Thread(target=self.worker,name='board-llm-udp',daemon=True);self.thread.start()
    def snapshot(self):
        with self.lock:return dict(self.state)
    def patch(self,**values):
        with self.lock:self.state.update(values)
    def start(self,args):
        text=args.get('text','');system=args.get('system','');direction=args.get('direction','en_zh');maximum=args.get('max_tokens',128)
        if not isinstance(text,str) or not isinstance(system,str):raise ValueError('text 和 system 必须是字符串。')
        if direction not in SYSTEMS:raise ValueError('未知任务方向。')
        if not system:system=SYSTEMS[direction]
        if not 1<=len(text.encode('utf-8'))<=2048 or len(system.encode('utf-8'))>512 or '\0' in text+system:raise ValueError('输入需为 1..2048 UTF8 字节，system 至多 512 字节，且不含 NUL。')
        if isinstance(maximum,bool) or not isinstance(maximum,int) or not 1<=maximum<=256:raise ValueError('生成上限必须是 1..256 的整数。')
        with self.lock:
            if self.state['pending'] or self.state.get('state') in (2,3,4,5) or self.state.get('flags',0)&128:raise ValueError('已有任务正在执行或播报，请等待完成。')
            self.state.update(pending=True,phase='queued_local',state=2,job=0,output='',error_text='',generated=0,prompt_tokens=0,max_tokens=maximum)
        self.commands.put_nowait(('start',text,system,maximum));return self.snapshot()
    def cancel(self):
        if self.snapshot().get('flags',0)&64:raise ValueError('麦克风任务请在板屏镜像中暂停。')
        self.commands.put_nowait(('cancel',));self.patch(phase='cancel_requested');return self.snapshot()
    def accept(self,info,part=b'',voice=None):
        job=info.get('job',0)
        if job!=self.job:
            self.job=job;self.raw=bytearray();self.decoder=codecs.getincrementaldecoder('utf-8')('replace');self.patch(output='')
        extra=''
        if part:self.raw.extend(part);extra=self.decoder.decode(part,final=False)
        terminal=info.get('state') in (6,7,8)
        if terminal and len(self.raw)>=info.get('output_bytes',0):extra+=self.decoder.decode(b'',final=True)
        with self.lock:
            out=self.state.get('output','')+extra
            self.state.update(info);self.state.update(connected=True,pending=False,output=out,error_text=ERRORS.get(info.get('error'),f"板上错误 {info.get('error')}，详情 {info.get('detail')}。") if info.get('state')==7 else '')
            if voice is not None:
                self.state['voice']=voice
                if voice.get('error') and not info.get('error'):
                    self.state['error_text']=f"语音链路错误 {voice['error']}，译音可能未播放。请在板屏镜像中暂停后继续。"
    def worker(self):
        network_poll=0
        while not self.stop.is_set():
            try:
                if self.client is None:self.client=self.client_factory()
                try:command=self.commands.get(timeout=.25)
                except queue.Empty:command=None
                if command:
                    if command[0]=='start':
                        self.job=0;self.raw=bytearray();self.decoder=codecs.getincrementaldecoder('utf-8')('replace')
                        job=self.client.submit(command[1],command[2],command[3]);self.patch(job=job,pending=False)
                    elif command[0]=='cancel':
                        job=self.snapshot().get('job',0)
                        if job:self.client.request(CANCEL,job)
                if time.monotonic()-network_poll>=1:
                    self.patch(**self.client.network());network_poll=time.monotonic()
                info=self.client.status()
                if info.get('job'):
                    if info['job']!=self.job:
                        self.job=info['job'];self.raw=bytearray();self.decoder=codecs.getincrementaldecoder('utf-8')('replace');self.patch(output='')
                    info,part=self.client.output(self.job,len(self.raw));self.accept(info,part,self.client.voice())
                else:self.accept(info,voice=self.client.voice())
            except (OSError,RuntimeError,TimeoutError,ValueError) as exc:
                self.patch(connected=False,pending=False,phase='connection_error',error_text=str(exc))
                self.stop.wait(.75)
        if self.client:self.client.close()
    def close(self):self.stop.set();self.thread.join(timeout=2)

class Handler(BaseHTTPRequestHandler):
    server_version='BoardQwen/1.0'
    def log_message(self,*args):pass
    def send(self,status,data,mime='application/json; charset=utf-8'):
        if not isinstance(data,bytes):data=json.dumps(data,ensure_ascii=False).encode('utf-8')
        self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
    def do_GET(self):
        if self.path=='/':self.send(200,HTML.encode('utf-8'),'text/html; charset=utf-8')
        elif self.path=='/api/status':self.send(200,self.server.controller.snapshot())
        elif self.path=='/favicon.ico':self.send(204,b'')
        else:self.send(404,dict(error='Not found'))
    def do_POST(self):
        if self.headers.get('X-Board-LLM')!='1' or self.headers.get('Content-Type','').split(';')[0]!='application/json':self.send(403,dict(error='需要网页 JSON 请求。'));return
        origin=self.headers.get('Origin');expected='http://'+self.headers.get('Host','')
        if origin and origin!=expected:self.send(403,dict(error='跨站请求被拒绝。'));return
        try:
            length=int(self.headers.get('Content-Length','0'))
            if not 0<length<=16384:raise ValueError('请求长度无效。')
            data=json.loads(self.rfile.read(length).decode('utf-8'))
            if not isinstance(data,dict):raise ValueError('请求必须是 JSON 对象。')
            if self.path=='/api/start':reply=self.server.controller.start(data)
            elif self.path=='/api/cancel':reply=self.server.controller.cancel()
            else:self.send(404,dict(error='Not found'));return
            self.send(200,reply)
        except (ValueError,UnicodeError,queue.Full) as exc:self.send(400,dict(error=str(exc)))

def make_server(controller,port=8790):
    server=ThreadingHTTPServer(('127.0.0.1',port),Handler);server.daemon_threads=True;server.controller=controller;return server
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--board',default='192.168.1.50');p.add_argument('--bind',default='');p.add_argument('--local-port',type=int,default=4191);args=p.parse_args()
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
    controller=Controller(lambda:Client(args.board,args.bind,args.local_port));server=make_server(controller)
    print('板上 Qwen 网页：http://127.0.0.1:8790（模型仅在板上推理）',flush=True)
    try:server.serve_forever(poll_interval=.2)
    except KeyboardInterrupt:pass
    finally:server.server_close();controller.close()
if __name__=='__main__':main()
