"""Shared ESP trusted-TLS/WSS control; UART has one explicit owner."""
import json,re,time
from datetime import datetime,timezone
from collections import deque
from websockets.client import ClientProtocol
from websockets.uri import parse_uri
from websockets.frames import Frame,Opcode
from websockets.http11 import Response
from websockets.protocol import State
from wifi_link import Bridge,ATClient,IpdStream
from probe_z7_nano_tls import HOST
import run_z7_nano_display as display
OWNER=display.ROOT/"output/z7_nano/tls/uart_uncertain.json"

class EspWss:
    def __init__(self,serial,folder,record):
        self.owner=OWNER;self.serial=serial
        if self.owner.exists():raise RuntimeError("Unconfirmed UART state requires public recovery")
        self.bridge=Bridge(None,command=[str(display.base.XSDB),str(display.ROOT/"scripts/z7_nano_uart_rpc.tcl"),serial],cwd=folder)
        self.at=ATClient(self.bridge);self.record=record;self.control=bytearray()
        self.stream=IpdStream(control_sink=self.control.extend)
        self.ws=None;self.messages=deque();self.fragments=bytearray()
        self.connected=self.pending=self.mux_changed=self.ps_owner=False
    def mark_owner(self,reason):
        self.owner.parent.mkdir(parents=True,exist_ok=True)
        self.owner.write_text(json.dumps({"cable_serial":self.serial,"reason":reason,
            "checked_utc":datetime.now(timezone.utc).isoformat()})+"\n",encoding="utf-8")
    def claim_ps(self):
        self.mark_owner("PS_owner");self.ps_owner=True
    def release_ps(self):
        self.ps_owner=False;self.owner.unlink(missing_ok=True)
    def connect(self,model,key):
        self.record["transport_step"]="echo"
        self.at.command("ATE0");version,_=self.at.command("AT+GMR")
        if b"AT version:" not in version or b"AT+GMR" in version:raise RuntimeError("echo")
        self.at.command("AT+SYSSTORE=0")
        self.mux_changed=True;self.at.command("AT+CIPMUX=1")
        self.record["transport_step"]="auth2";self.at.command("AT+CIPSSLCCONF=0,2,0,0")
        auth,_=self.at.command("AT+CIPSSLCCONF?")
        if b"+CIPSSLCCONF:0,2,0,0\r\n" not in auth:raise RuntimeError("auth")
        self.at.command(f'AT+CIPSSLCSNI=0,"{HOST}"');self.at.command(f'AT+CIPSSLCCN=0,"{HOST}"')
        self.record["transport_step"]="tls";self.connected=True;self.at.command(f'AT+CIPSTART=0,"SSL","{HOST}",443',timeout=25)
        self.record["transport_step"]="wss"
        self.ws=ClientProtocol(parse_uri(f"wss://{HOST}/api-ws/v1/realtime?model={model}"),max_size=4096)
        request=self.ws.connect();request.headers["Authorization"]="Bearer "+key
        self.ws.send_request(request)
    def feed_tcp(self,data):
        self.ws.receive_data(data)
        for event in self.ws.events_received():
            if isinstance(event,Response):
                self.record["http_status"]=event.status_code
                if event.status_code!=101 or self.ws.handshake_exc is not None or self.ws.state is not State.OPEN:
                    raise RuntimeError("upgrade")
            elif isinstance(event,Frame):
                if event.opcode in (Opcode.TEXT,Opcode.CONT):
                    self.fragments.extend(event.data)
                    if event.fin:self.messages.append(json.loads(self.fragments));self.fragments.clear()
                elif event.opcode is Opcode.CLOSE:self.record["cloud_closed"]=True
    def feed_uart(self,data):
        for identity,payload in self.stream.feed(data):
            if identity!=0:raise RuntimeError("connection")
            self.feed_tcp(payload)
    def flush(self):
        if self.ps_owner:raise RuntimeError("PS owns UART")
        for data in self.ws.data_to_send():
            if not data:continue
            self.mark_owner("CIPSEND_payload");self.pending=True;self.control.clear()
            self.bridge.send(f"AT+CIPSEND=0,{len(data)}\r\n".encode())
            for wanted in (b">",b"\r\nSEND OK\r\n"):
                deadline=time.monotonic()+30
                while wanted not in self.control:
                    if time.monotonic()>deadline:raise RuntimeError("send_timeout")
                    self.feed_uart(self.bridge.drain())
                    if len(self.control)>4096 or re.search(rb"\r\n(?:ERROR|SEND FAIL)\r\n",self.control):raise RuntimeError("send")
                if wanted==b">":self.control.clear();self.bridge.send(data)
            self.pending=False;self.owner.unlink(missing_ok=True)
    def wait_event(self,expected):
        deadline=time.monotonic()+60
        while time.monotonic()<deadline:
            self.flush()
            while self.messages:
                event=self.messages.popleft();kind=event["type"];self.record["events"].append(kind)
                if kind==expected:return event
                if kind=="error":
                    self.record["cloud_error_code"]=event["error"]["code"];raise RuntimeError("cloud_error")
                raise RuntimeError("unexpected_cloud_event")
            if self.record.get("cloud_closed"):raise RuntimeError("cloud_close")
            self.feed_uart(self.bridge.drain())
        raise RuntimeError("event_timeout")
    def settle_ipd(self):
        deadline=time.monotonic()+10
        while self.stream.buffer:
            if time.monotonic()>deadline:raise RuntimeError("incomplete_IPD_at_handoff")
            self.feed_uart(self.bridge.drain())
            self.flush()
    def control_command(self,command):
        self.control.clear();self.bridge.send(command.encode()+b"\r\n");deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            self.stream.feed(self.bridge.drain())
            if re.search(rb"(?:^|\r\n)OK\r\n",self.control):return True
            if len(self.control)>4096 or re.search(rb"(?:^|\r\n)(?:ERROR|FAIL)\r\n",self.control):return False
        return False
    def close(self):
        if self.connected and not(self.pending or self.ps_owner):
            try:self.record["tcp_closed"]=self.control_command("AT+CIPCLOSE=0")
            except Exception:self.record["tcp_closed"]=False
            if not self.record["tcp_closed"]:self.record["status"]="failed"
        elif self.connected:self.record.update(status="failed",uart_state="unconfirmed_payload_or_PS_owner")
        if self.mux_changed and not(self.pending or self.ps_owner):
            try:self.record["mux_restored"]=self.control_command("AT+CIPMUX=0")
            except Exception:self.record["mux_restored"]=False
            if not self.record["mux_restored"]:self.record["status"]="failed"
        self.bridge.close()
