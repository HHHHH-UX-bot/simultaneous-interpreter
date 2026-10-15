"""PC-driven, key-free CYEA/TLS feasibility check on the idle network app."""
import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime,timezone
from pathlib import Path
from network_connect import read_credentials_line,payload
from wifi_link import at_escape
from probe_z7_nano_tls_inventory import public_fields
import run_z7_nano_network as network
import run_z7_nano_display as display

HOST="ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com"

def certificate_metadata(raw):
    """Decode certificate objects only. Never export raw partition/private key bytes."""
    import struct
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    if len(raw)<8:raise ValueError("Truncated public PKI header")
    magic,count,length=struct.unpack_from("<HHI",raw)
    if magic!=0xf1f1 or length+8>len(raw):raise ValueError("Unexpected public PKI header")
    offset=8;end=length+8;certificates=[]
    for _ in range(count):
        if offset+4>end:raise ValueError("Truncated public PKI item header")
        kind,identity,size=struct.unpack_from("<BBH",raw,offset)
        next_offset=offset+4+((size+3)//4)*4
        if next_offset>end:raise ValueError("Truncated public PKI item")
        content=raw[offset+4:offset+4+size]
        if kind in (1,2):
            is_pem=content.startswith(b"-----BEGIN CERTIFICATE-----")
            cert=x509.load_pem_x509_certificate(content) if is_pem else x509.load_der_x509_certificate(content)
            canonical=cert.public_bytes(serialization.Encoding.PEM if is_pem else serialization.Encoding.DER)
            if is_pem:
                complete=b"".join(content.removesuffix(b"\x00").split())==b"".join(canonical.split())
            else:complete=content==canonical
            if not complete:raise ValueError("Non-certificate data inside public PKI item")
            certificates.append({"type":kind,"id":identity,"bytes":size,
                "subject":cert.subject.rfc4514_string(),"issuer":cert.issuer.rfc4514_string(),
                "not_before":cert.not_valid_before_utc.isoformat(),"not_after":cert.not_valid_after_utc.isoformat(),
                "sha256":hashlib.sha256(content).hexdigest()})
        offset=next_offset
    if offset!=end:raise ValueError("Public PKI item count/length mismatch")
    return certificates

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--credentials-stdin",action="store_true",required=True)
    parser.add_argument("--xsdb",type=Path,default=display.base.XSDB);args=parser.parse_args()
    credentials=json.loads(read_credentials_line());payload(credentials)
    private_join=('AT+CWJAP="'+at_escape(credentials["ssid"])+'","'+at_escape(credentials["password"])+'"').encode().hex()+"\n"
    del credentials
    network.configure();app=display.checked_app()
    program=json.loads((display.OUT/"program.json").read_text());active=json.loads((display.OUT/"active_app.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Matching active app required")
    folder=display.ROOT/"output/z7_nano/tls/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ");folder.mkdir(parents=True)
    record={"status":"pending","attempt":folder.relative_to(display.ROOT).as_posix(),"host":HOST,
        "program_attempt":program["attempt"],"elf_sha256":app["elf_sha256"],"checks":{},
        "input_hashes":display.hashes(["scripts/probe_z7_nano_tls.py","scripts/z7_nano_tls_preflight.tcl",
            "scripts/z7_nano_tls_uart.tcl","scripts/probe_z7_nano_tls_inventory.py"]),
        "scope":"PC-driven no-key TLS feasibility; SYSSTORE=0 Wi-Fi; no cloud authentication, WSS, audio, certificate partition or firmware writes; SNI/CN/SNTP persistence not verified"}
    with display.base.hardware_lock():
        process=subprocess.Popen([str(args.xsdb),str(display.ROOT/"scripts/z7_nano_tls_preflight.tcl"),program["cable_serial"],HOST],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",cwd=folder)
        try:
            output,_=process.communicate(input=private_join,timeout=350);private_join=None
            for name,result,encoded in re.findall(r"^TLS_INVENTORY (\w+) (OK|ERROR|timeout) ([0-9a-f]*)$",output,re.M):
                raw=bytes.fromhex(encoded);reply=raw.decode("latin1")
                record["checks"][name]={"result":result,**public_fields(name,reply)}
                if name in ("address","dns"):
                    record["checks"][name]["ipv4"]=re.findall(r'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])',reply)
            stopped=re.findall(r"^TLS_PREFLIGHT_STOP (\w+)$",output,re.M)
            if stopped:record["stopped_at"]=stopped[-1]
            counters=re.findall(r"^TLS_INVENTORY_COUNTERS (\d+) (\d+)$",output,re.M)
            if counters:record["uart_errors"]=[int(v) for v in counters[-1]]
            record["status"]="verified" if process.returncode==0 and "PASS_Z7_TLS_PREFLIGHT" in output and record.get("uart_errors")==[0,0] else "not_verified"
        except (Exception,KeyboardInterrupt):
            if process.poll() is None:
                subprocess.run(["taskkill","/PID",str(process.pid),"/T","/F"],capture_output=True,check=True,timeout=10);process.wait(timeout=10)
            record.update(status="interrupted",error="Preflight incomplete; inspect public module state before retry")
        finally:
            private_join=None;display.save(folder/"preflight.json",record);display.save(display.ROOT/"output/z7_nano/tls/preflight.json",record)
    print(json.dumps(record,ensure_ascii=False));raise SystemExit(record["status"]!="verified")

if __name__=="__main__":main()
