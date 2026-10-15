"""Private JTAG/UART bring-up. Credentials enter stdin/getpass, never files/argv."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import getpass
import hashlib
import ipaddress
import json
import queue
import re
import socket
import subprocess
import sys
import threading
import time
import uuid

import run_wifi

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/"output/wifi"
TEMP = ROOT/"tmp/wifi"

def utc_now():
    return datetime.now(timezone.utc).isoformat()

class Bridge:
    def __init__(self, vivado, *, command=None, cwd=None):
        self.process = subprocess.Popen(
            command or [str(vivado),"-mode","batch","-notrace","-nolog","-nojournal",
                        "-source",str(ROOT/"scripts/wifi_rpc.tcl")],
            cwd=cwd or TEMP,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding="utf-8",errors="replace",bufsize=1,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.responses=queue.Queue()
        self.serial=0
        self.startup_diagnostics=[]
        self.ready=False
        self.thread=threading.Thread(target=self._reader,daemon=True)
        self.thread.start()
        try:
            self._wait(0,60)
            self.ready=True
        except Exception:
            self.close()
            raise
    def _reader(self):
        for line in self.process.stdout:
            if line.startswith("@@WIFI "):
                self.responses.put(line.strip())
            elif not self.ready:
                # Startup has no UART payload/credentials. Keep only error
                # diagnostics in memory for a useful startup failure message.
                if "ERROR:" in line or "CRITICAL WARNING:" in line:
                    self.startup_diagnostics.append(line.strip())
        self.responses.put(None)
    def _wait(self, identity, timeout):
        deadline=time.monotonic()+timeout
        while True:
            remaining=deadline-time.monotonic()
            if remaining<=0:
                raise RuntimeError("JTAG bridge response timed out")
            try:
                line=self.responses.get(timeout=remaining)
            except queue.Empty:
                raise RuntimeError("JTAG bridge response timed out") from None
            if line is None:
                raise RuntimeError("JTAG bridge stopped before completing operation")
            parts=line.split(" ",3)
            if int(parts[1]) != identity:
                raise RuntimeError("JTAG bridge response ID mismatch")
            if parts[2]!="OK":
                detail="; ".join(self.startup_diagnostics[-3:]) if identity==0 else parts[-1]
                raise RuntimeError("JTAG bridge failed: "+detail)
            return parts[3] if len(parts)>3 else ""
    def rpc(self, operation, argument="", timeout=30):
        self.serial+=1
        self.process.stdin.write(f"{self.serial} {operation}"+(f" {argument}" if argument else "")+"\n")
        self.process.stdin.flush()
        return self._wait(self.serial,timeout)
    def read(self,address):
        return int(self.rpc("read",f"{address:x}"),16)
    def write(self,address,value):
        self.rpc("write",f"{address:x} {value:x}")
    def send(self,data):
        for offset in range(0,len(data),256):
            part=data[offset:offset+256]
            count=int(self.rpc("tx",part.hex(),60),16)
            if count!=len(part):
                raise RuntimeError("UART TX did not accept the expected length")
    def drain(self,limit=512):
        return bytes.fromhex(self.rpc("drain",f"{limit:x}",60))
    def close(self):
        if self.process.poll() is None:
            try:
                self.rpc("quit",timeout=5)
            except Exception:
                pass
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # Terminate only our Vivado process tree, never a user GUI/server.
                subprocess.run(["taskkill","/PID",str(self.process.pid),"/T","/F"],
                               stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if self.process.stdin:
            self.process.stdin.close()

class ATClient:
    def __init__(self,bridge):
        self.bridge=bridge
    def collect(self, timeout, prompt=False):
        data=bytearray()
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            part=self.bridge.drain()
            data.extend(part)
            if len(data)>8192:
                raise RuntimeError("AT response exceeded bounded buffer")
            if prompt and b">" in data:
                return bytes(data),True
            if not prompt and re.search(rb"(?:^|\r\n)OK\r\n",data):
                return bytes(data),True
            if re.search(rb"(?:^|\r\n)(?:ERROR|FAIL|SEND FAIL)\r\n",data):
                return bytes(data),False
            if not prompt and b"\r\nSEND OK\r\n" in data:
                return bytes(data),True
            if not part:
                time.sleep(0.05)
        return bytes(data),False
    def command(self, command, timeout=4, required=True, prompt=False):
        self.bridge.send(command.encode("utf-8")+b"\r\n")
        data,ok=self.collect(timeout,prompt=prompt)
        if required and not ok:
            # Never include arguments, echoed credentials, or raw AT responses.
            name=command.split("=",1)[0]
            raise RuntimeError(f"{name} did not return the expected success response")
        return data,ok

class IpdStream:
    """Length-based binary parser for CIPMUX=1 +IPD,id,length:payload."""
    def __init__(self,control_sink=None):
        self.buffer=bytearray()
        self.control_sink=control_sink
    def feed(self,data):
        self.buffer.extend(data)
        if len(self.buffer)>16384:
            raise RuntimeError("IPD parser buffer exceeded limit")
        frames=[]
        while True:
            start=self.buffer.find(b"+IPD,")
            if start<0:
                keep=0
                for size in range(min(4,len(self.buffer)),0,-1):
                    if b"+IPD,".startswith(self.buffer[-size:]):keep=size;break
                end=len(self.buffer)-keep
                if self.control_sink:self.control_sink(self.buffer[:end])
                del self.buffer[:end]
                break
            if start:
                if self.control_sink:self.control_sink(self.buffer[:start])
                del self.buffer[:start]
            colon=self.buffer.find(b":")
            if colon<0:
                if len(self.buffer)>64:
                    raise RuntimeError("IPD header exceeded limit")
                break
            header=bytes(self.buffer[:colon+1])
            match=re.fullmatch(rb"\+IPD,([0-4]),([0-9]{1,5}):",header)
            if not match:
                raise RuntimeError("Unexpected IPD header")
            identity,length=map(int,match.groups())
            if length>4096:
                raise RuntimeError("IPD payload exceeded limit")
            if len(self.buffer)<colon+1+length:
                break
            frames.append((identity,bytes(self.buffer[colon+1:colon+1+length])))
            del self.buffer[:colon+1+length]
        return frames

def at_escape(value):
    if any(ord(character)<32 or ord(character)==127 for character in value):
        raise ValueError("AT credential fields must not contain control characters")
    return value.replace("\\","\\\\").replace('"','\\"').replace(",","\\,")

def current_build():
    build=run_wifi.read("build.json")
    programmed=run_wifi.read("hardware/program_result.json")
    if build["Status"]!="passed" or programmed["Status"]!="passed":
        raise RuntimeError("Successful build and programming evidence required")
    run_wifi.check(build["LogicHashes"],run_wifi.BUILD_INPUTS)
    bit_hash=run_wifi.sha(OUT/"wifi_ui_top.bit")
    if build["BitstreamSHA256"]!=bit_hash or programmed["BitstreamSHA256"]!=bit_hash:
        raise RuntimeError("Programmed bitstream evidence differs from current build")
    return bit_hash

def detect_at(bridge, attempts=None):
    if attempts is None:
        attempts=[]
    if bridge.read(0)!=0x57494631:
        raise RuntimeError("Unexpected UART mailbox")
    for baud in (115200,9600,57600,38400,19200,74880,230400,460800,921600):
        print(f"Checking UART {baud} baud",flush=True)
        bridge.write(12,15)
        time.sleep(0.05)
        divider=round(50_000_000/baud)
        bridge.write(8,divider)
        if bridge.read(8)!=divider:
            attempts.append({"Baud":baud,"DividerAccepted":False,"StatusWord":bridge.read(4)})
            continue
        at=ATClient(bridge)
        for trial in range(3 if baud==115200 else 2):
            if trial==2:
                # Reversible exit from an existing transparent session; no
                # flash/config rewrite. Both guard times exceed one second.
                time.sleep(1.2)
                bridge.send(b"+++")
                time.sleep(1.2)
                bridge.drain()
            _,ok=at.command("AT",timeout=1.5,required=False)
            if ok:
                at.command("ATE0")
                response,ok=at.command("AT")
                if re.search(rb"(?:^|\r\n)AT\r\n",response):
                    raise RuntimeError("AT command echo could not be disabled")
                version,_=at.command("AT+GMR",required=False)
                version_lines=[line for line in version.decode("utf-8","replace").splitlines()
                               if line and line not in ("OK","ERROR","AT+GMR")]
                attempts.append({"Baud":baud,"ATResponse":True,"ExitedTransparentMode":trial==2})
                return at,baud,version_lines[:10]
        attempts.append({"Baud":baud,"ATResponse":False,"StatusWord":bridge.read(4),
                         "TXStarted":bridge.read(0x28),"RXReceived":bridge.read(0x2c),
                         "FramingErrors":bridge.read(0x24),"RXOverflow":bridge.read(0x20)})
    raise RuntimeError("No AT response at tested UART rates; check module firmware/enable pins and wiring")

def tcp_roundtrip(at, module_ip, subnet):
    address=ipaddress.ip_address(module_ip)
    if address not in ipaddress.ip_network(subnet):
        raise RuntimeError("Module address is outside the explicitly selected local subnet")
    at.command("AT+CIPMUX=1")
    # Suppress optional peer-address fields if firmware supports this setting.
    at.command("AT+CIPDINFO=0",required=False)
    port=8266
    connection=None
    proof=None
    try:
        at.command(f"AT+CIPSERVER=1,{port}")
        connection=socket.create_connection((module_ip,port),timeout=6)
        connection.settimeout(20)
        payload=b"ZYNQ_WIFI_TEST|"+uuid.uuid4().hex.encode()+b"|"+bytes(range(256))+"|你好，网络测试。".encode()
        connection.sendall(payload)
        received=bytearray()
        parser=IpdStream()
        channel=None
        deadline=time.monotonic()+30
        while len(received)<len(payload) and time.monotonic()<deadline:
            part=at.bridge.drain(1024)
            for identity,data in parser.feed(part):
                if channel is not None and channel!=identity:
                    raise RuntimeError("Multiple TCP peers during controlled test")
                channel=identity
                received.extend(data)
            if not part:
                time.sleep(0.05)
        if bytes(received)!=payload:
            raise RuntimeError("PC-to-module TCP/UART payload mismatch")
        at.command(f"AT+CIPSEND={channel},{len(payload)}",prompt=True)
        at.bridge.send(payload)
        _,ok=at.collect(10)
        if not ok:
            raise RuntimeError("Module did not confirm TCP send")
        echoed=bytearray()
        while len(echoed)<len(payload):
            part=connection.recv(len(payload)-len(echoed))
            if not part:
                break
            echoed.extend(part)
        if bytes(echoed)!=payload:
            raise RuntimeError("Module-to-PC TCP payload mismatch")
        proof={"Status":"passed","BytesEachDirection":len(payload),
                "PayloadSHA256":hashlib.sha256(payload).hexdigest(),
                "IncludesAllByteValues":True,"IncludesChineseUTF8":True,
                "Topology":"PC TCP client -> ESP temporary TCP server -> UART/JTAG bridge and return"}
        return proof
    finally:
        if connection:
            connection.close()
        try:
            at.command("AT+CIPSERVER=0")
        except Exception as error:
            raise RuntimeError("Temporary ESP TCP server shutdown was not confirmed") from error
        if proof is not None:
            proof["ServerShutdownConfirmed"]=True

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=("probe","connect"))
    parser.add_argument("--vivado",required=True,type=Path)
    parser.add_argument("--credentials-stdin",action="store_true")
    parser.add_argument("--subnet",default="192.168.31.0/24")
    args=parser.parse_args()
    result={"Status":"pending","StartedAtUTC":utc_now(),"Action":args.action}
    name=f"hardware/{args.action}_link.json"
    run_wifi.save(name,result)
    bridge=None
    try:
        result["BitstreamSHA256"]=current_build()
        print("Opening private JTAG/UART bridge (UART data is not logged)",flush=True)
        bridge=Bridge(args.vivado)
        at,baud,version=detect_at(bridge,result.setdefault("UARTProbeAttempts",[]))
        result.update(UART={"Status":"passed","Baud":baud,"Firmware":version})
        print(f"AT response verified at {baud} baud; echo disabled",flush=True)
        if args.action=="connect":
            if args.credentials_stdin:
                print("READY_FOR_WIFI_CREDENTIALS_STDIN",flush=True)
                # Pipes do not echo; an interactive console must disable echo
                # before accepting the JSON so credentials stay out of output.
                credential_line=getpass.getpass("") if sys.stdin.isatty() else sys.stdin.readline()
                credentials=json.loads(credential_line)
                credential_line=None
                ssid,password=credentials["ssid"],credentials["password"]
            else:
                ssid=input("SSID: ")
                password=getpass.getpass("Wi-Fi password: ")
            if not 1<=len(ssid.encode())<=32 or not 8<=len(password.encode())<=64:
                raise ValueError("Unsupported Wi-Fi credential field length")
            quoted_ssid,quoted_password=at_escape(ssid),at_escape(password)
            _,legacy=at.command("AT+CWMODE_CUR=1",required=False)
            if legacy:
                join_command="AT+CWJAP_CUR"
            else:
                at.command("AT+SYSSTORE=0")
                at.command("AT+CWMODE=1")
                join_command="AT+CWJAP"
            print("Joining the supplied Wi-Fi network using volatile settings",flush=True)
            at.command(f'{join_command}="{quoted_ssid}","{quoted_password}"',timeout=50)
            # Drop references immediately after use; no credentials in results.
            password=quoted_password=None
            if args.credentials_stdin:
                credentials.clear()
            response,_=at.command("AT+CIFSR")
            match=re.search(rb'\+CIFSR:STAIP,"([0-9.]+)"',response)
            if not match:
                raise RuntimeError("Station IP was not reported by the module")
            module_ip=match.group(1).decode()
            if module_ip=="0.0.0.0":
                raise RuntimeError("Station did not obtain an IP address")
            response,_=at.command("AT+CWJAP?")
            if ('"'+quoted_ssid+'"').encode() not in response:
                raise RuntimeError("Reported association does not match the requested SSID")
            result["WiFi"]={"Status":"passed","StationIP":module_ip,"RequestedSSIDMatched":True,"Settings":"volatile"}
            run_wifi.save(name,result)
            print(f"Wi-Fi association and station IP verified: {module_ip}",flush=True)
            result["TCP"]=tcp_roundtrip(at,module_ip,args.subnet)
            print("TCP round trip verified, including binary bytes and Chinese UTF-8",flush=True)
        result["Counters"]={"RXOverflow":bridge.read(0x20),"FramingErrors":bridge.read(0x24),
                            "TXStarted":bridge.read(0x28),"RXReceived":bridge.read(0x2c)}
        if result["Counters"]["RXOverflow"] or result["Counters"]["FramingErrors"]:
            raise RuntimeError("UART overflow or framing error observed")
        result.update(Status="passed",VerifiedAtUTC=utc_now())
        run_wifi.save(name,result)
        print(json.dumps(result,ensure_ascii=False),flush=True)
    except Exception as error:
        result.update(Status="failed",FailedAtUTC=utc_now(),Error=str(error))
        run_wifi.save(name,result)
        print("Wi-Fi stage stopped: "+str(error),flush=True)
        return 1
    finally:
        if bridge:
            bridge.close()
    return 0

if __name__=="__main__":
    raise SystemExit(main())
