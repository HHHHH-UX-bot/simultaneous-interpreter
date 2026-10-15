"""Public TLS readiness inventory through idle UART; no authentication/association."""
import argparse
import hashlib
import json
import re
import subprocess
import struct
from datetime import datetime,timezone
from pathlib import Path
import run_z7_nano_network as network
import run_z7_nano_display as display


def public_fields(name,reply):
    fields={}
    if name=="version":
        fields["response_sha256"]=hashlib.sha256(reply.encode()).hexdigest()
        for key,label in (("AT version:","at_version"),("SDK version:","sdk_version")):
            match=re.search(re.escape(key)+r"([^\r\n]{1,160})",reply)
            if match:fields[label]=match[1]
    elif name=="partitions":
        fields["partitions"]=[{"name":m[0],"type":int(m[1],0),"subtype":int(m[2],0),
                               "address":int(m[3],0),"size":int(m[4],0)} for m in re.findall(
            r'\+SYSFLASH:"?([a-zA-Z0-9_]+)"?,(0x[0-9a-fA-F]+|\d+),(0x[0-9a-fA-F]+|\d+),(0x[0-9a-fA-F]+|\d+),(0x[0-9a-fA-F]+|\d+)',reply)]
    elif name in ("store","heap","auth"):
        tag={"store":"SYSSTORE","heap":"SYSRAM","auth":"CIPSSLCCONF"}[name]
        match=re.search(r"\+"+tag+r":([0-9,]+)\r\n",reply)
        if match:fields["numeric_arguments"]=[int(v) for v in match[1].split(",")]
    elif name in ("sni","common_name"):
        tag="CIPSSLCSNI" if name=="sni" else "CIPSSLCCN"
        fields["has_parameter_line"]=bool(re.search(r"\+"+tag+r":[^\r\n]+\r\n",reply))
    elif name=="sntp":
        fields["has_parameter_line"]="+CIPSNTPCFG:" in reply
    elif name=="time":
        match=re.search(r"\+CIPSNTPTIME:([^\r\n]{1,80})",reply)
        if match:fields["reported_time"]=match[1]
    elif name=="ca_header":
        raw=reply.encode("latin1");start=raw.find(b"+SYSFLASH:12,")
        if start>=0 and len(raw)>=start+25:
            magic,count,length,kind,identity,size=struct.unpack("<HHIBBH",raw[start+13:start+25])
            fields.update(magic=magic,item_count=count,items_bytes=length,
                          first_type=kind,first_id=identity,first_bytes=size)
    return fields


def main():
    p=argparse.ArgumentParser();p.add_argument("--xsdb",type=Path,default=display.base.XSDB)
    p.add_argument("--transport-check",action="store_true",help="No-key auth=0 diagnostic; restores auth=2")
    a=p.parse_args()
    network.configure();app=display.checked_app()
    program=json.loads((display.OUT/"program.json").read_text());active=json.loads((display.OUT/"active_app.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Matching active application required")
    folder=display.ROOT/"output/z7_nano/tls/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ");folder.mkdir(parents=True)
    script="scripts/z7_nano_tls_transport.tcl" if a.transport_check else "scripts/z7_nano_tls_inventory.tcl"
    marker="PASS_Z7_TLS_TRANSPORT" if a.transport_check else "PASS_Z7_TLS_INVENTORY"
    record={"status":"pending","attempt":folder.relative_to(display.ROOT).as_posix(),"program_attempt":program["attempt"],
            "elf_sha256":app["elf_sha256"],"queries":{},"input_hashes":display.hashes(
                ["scripts/probe_z7_nano_tls_inventory.py",script,"scripts/z7_nano_tls_uart.tcl"]),
            "scope":("No-key unauthenticated TLS transport diagnostic; restores auth=2; no HTTP, WSS, audio or trusted TLS claim" if a.transport_check else
                "PC-driven fixed public queries while PS network is idle; no association, TLS connection, cloud key or Flash writes")}
    with display.base.hardware_lock():
        process=subprocess.Popen([str(a.xsdb),str(display.ROOT/script),program["cable_serial"]],
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",cwd=folder)
        try:
            output,_=process.communicate(timeout=300)
            for name,result,encoded in re.findall(r"^TLS_INVENTORY (\w+) (OK|ERROR|timeout) ([0-9a-f]*)$",output,re.M):
                reply=bytes.fromhex(encoded).decode("latin1")
                record["queries"][name]={"result":result,**public_fields(name,reply)}
            counters=re.findall(r"^TLS_INVENTORY_COUNTERS (\d+) (\d+)$",output,re.M)
            if counters:record["uart_errors"]=[int(v) for v in counters[-1]]
            if process.returncode or marker not in output:raise RuntimeError("Probe did not complete; raw output discarded")
            if record.get("uart_errors")!=[0,0]:raise RuntimeError("UART errors observed")
            record["status"]="observed"
        except (Exception,KeyboardInterrupt) as error:
            if process.poll() is None:
                subprocess.run(["taskkill","/PID",str(process.pid),"/T","/F"],capture_output=True,check=True,timeout=10)
                process.wait(timeout=10)
            record.update(status="failed",error=str(error))
        finally:
            name="transport.json" if a.transport_check else "inventory.json"
            display.save(folder/name,record);display.save(display.ROOT/"output/z7_nano/tls"/name,record)
    print(json.dumps(record,ensure_ascii=False))
    raise SystemExit(record["status"]!="observed")


if __name__=="__main__":main()
