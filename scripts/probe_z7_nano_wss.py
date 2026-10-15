"""Authenticate a no-audio cloud session through ESP TLS, using websockets Sans-I/O."""
import argparse
import json
import re
import time
from datetime import datetime,timezone
from collections import deque
from pathlib import Path
from websockets.client import ClientProtocol
from websockets.uri import parse_uri
from websockets.frames import Frame,Opcode
from websockets.http11 import Response
from websockets.protocol import State
from wifi_link import Bridge,ATClient,IpdStream
from network_connect import read_credentials_line
from probe_z7_nano_tls import HOST
import run_z7_nano_network as network
import run_z7_nano_display as display

def main():
    p=argparse.ArgumentParser();p.add_argument("--credentials-stdin",required=True,action="store_true");a=p.parse_args()
    key=json.loads(read_credentials_line())["key"]
    if not isinstance(key,str) or not key.startswith("sk-") or not key.isascii() or any(c.isspace() for c in key) or len(key)>512:
        raise SystemExit("API input rejected")
    network.configure();app=display.checked_app();out=display.ROOT/"output/z7_nano/tls"
    program=json.loads((display.OUT/"program.json").read_text());active=json.loads((display.OUT/"active_app.json").read_text())
    tls=json.loads((out/"preflight.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"] or tls["status"]!="verified" or tls["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Matching app and trusted TLS preflight required")
    folder=out/"attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ");folder.mkdir(parents=True)
    record={"status":"pending","attempt":folder.relative_to(display.ROOT).as_posix(),"program_attempt":program["attempt"],
        "elf_sha256":app["elf_sha256"],"tls_preflight":tls["attempt"],"events":[],
        "scope":"PC drives UART commands; ESP performs trusted TLS and TCP to cloud; WSS authentication/configuration only, no audio or independent ARM client",
        "input_hashes":display.hashes(["scripts/probe_z7_nano_wss.py","scripts/z7_nano_uart_rpc.tcl","scripts/wifi_link.py","scripts/z7_nano_tls_uart.tcl"])}
    stage="uart";bridge=None;at=None;connected=False;pending=False;mux_changed=False
    control=bytearray();stream=IpdStream(control_sink=control.extend)
    def control_command(command):
        control.clear();bridge.send(command.encode()+b"\r\n");deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            stream.feed(bridge.drain())
            if re.search(rb"(?:^|\r\n)OK\r\n",control):return True
            if len(control)>4096 or re.search(rb"(?:^|\r\n)(?:ERROR|FAIL)\r\n",control):return False
        return False
    with display.base.hardware_lock():
        try:
            bridge=Bridge(None,command=[str(display.base.XSDB),str(display.ROOT/"scripts/z7_nano_uart_rpc.tcl"),program["cable_serial"]],cwd=folder)
            at=ATClient(bridge);at.command("ATE0")
            version,_=at.command("AT+GMR")
            if b"AT version:" not in version or b"AT+GMR" in version:raise RuntimeError("echo")
            at.command("AT+SYSSTORE=0")
            mux_changed=True;at.command("AT+CIPMUX=1")
            at.command("AT+CIPSSLCCONF=0,2,0,0")
            auth,_=at.command("AT+CIPSSLCCONF?")
            if b"+CIPSSLCCONF:0,2,0,0\r\n" not in auth:raise RuntimeError("auth")
            at.command(f'AT+CIPSSLCSNI=0,"{HOST}"');at.command(f'AT+CIPSSLCCN=0,"{HOST}"')
            stage="tls";connected=True;at.command(f'AT+CIPSTART=0,"SSL","{HOST}",443',timeout=25)
            ws=ClientProtocol(parse_uri(f"wss://{HOST}/api-ws/v1/realtime?model=qwen3.8-livetranslate-flash-realtime"),max_size=4096)
            request=ws.connect();request.headers["Authorization"]="Bearer "+key;key=None
            ws.send_request(request);del request
            messages=deque();fragments=bytearray()
            def feed(raw):
                nonlocal fragments
                for identity,data in stream.feed(raw):
                    if identity!=0:raise RuntimeError("connection")
                    ws.receive_data(data)
                    for event in ws.events_received():
                        if isinstance(event,Response):
                            record["http_status"]=event.status_code
                            if event.status_code!=101 or ws.handshake_exc is not None or ws.state is not State.OPEN:raise RuntimeError("upgrade")
                        elif isinstance(event,Frame):
                            if event.opcode in (Opcode.TEXT,Opcode.CONT):
                                fragments.extend(event.data)
                                if event.fin:
                                    messages.append(json.loads(fragments));fragments.clear()
                            elif event.opcode is Opcode.CLOSE:raise RuntimeError("cloud_close")
            def flush():
                nonlocal pending
                for data in ws.data_to_send():
                    if not data:continue
                    pending=True;control.clear()
                    bridge.send(f"AT+CIPSEND=0,{len(data)}\r\n".encode())
                    for wanted in (b">",b"\r\nSEND OK\r\n"):
                        deadline=time.monotonic()+30
                        while wanted not in control:
                            if time.monotonic()>deadline:raise RuntimeError("send_timeout")
                            feed(bridge.drain())
                            if len(control)>4096 or b"\r\nERROR\r\n" in control or b"\r\nSEND FAIL\r\n" in control:
                                raise RuntimeError("send")
                        if wanted==b">":
                            control.clear();bridge.send(data)
                    pending=False
            def wait_event(expected):
                deadline=time.monotonic()+60
                while time.monotonic()<deadline:
                    flush()
                    while messages:
                        event=messages.popleft();kind=event.get("type")
                        if kind not in ("session.created","session.updated"):raise RuntimeError("cloud_event")
                        record["events"].append(kind)
                        if kind==expected:return
                    feed(bridge.drain())
                raise RuntimeError("event_timeout")
            stage="session_created";wait_event("session.created")
            stage="session_updated"
            ws.send_text(json.dumps({"event_id":"prototype_session_config","type":"session.update",
                "session":{"translation":{"language":"zh"},"output_modalities":["text"]}}).encode())
            wait_event("session.updated")
            record["uart_errors"]=[bridge.read(32),bridge.read(36)]
            if record["uart_errors"]!=[0,0]:raise RuntimeError("uart_errors")
            record["status"]="verified"
        except (Exception,KeyboardInterrupt):record.update(status="failed",stopped_at=stage)
        finally:
            key=None
            if at and connected and not pending:
                try:
                    record["tcp_closed"]=control_command("AT+CIPCLOSE=0")
                except Exception:record["tcp_closed"]=False
                if not record["tcp_closed"]:record["status"]="failed"
            elif connected:record.update(status="failed",uart_state="unconfirmed_pending_payload")
            if at and mux_changed and not pending:
                try:
                    record["mux_restored"]=control_command("AT+CIPMUX=0")
                    if not record["mux_restored"]:record["status"]="failed"
                except Exception:record.update(status="failed",mux_restored=False)
            if bridge:bridge.close()
            display.save(folder/"wss.json",record);display.save(out/"wss.json",record)
    print(json.dumps(record,ensure_ascii=False));raise SystemExit(record["status"]!="verified")

if __name__=="__main__":main()
