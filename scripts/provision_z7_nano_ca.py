"""Back up/update only the public client_ca partition, preserving ESP firmware."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import threading
from datetime import datetime,timezone
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from probe_z7_nano_tls import certificate_metadata
import run_z7_nano_network as network
import run_z7_nano_display as display

ROOT_SHA256="cbb522d7b7f127ad6a0113865bdf1cd4102e7d0759af635a7cf4720dc963c53b"

def public_image(raw):
    certs=certificate_metadata(raw)
    _,count,length=struct.unpack_from("<HHI",raw)
    if count!=len(certs) or any(v!=255 for v in raw[8+length:]):
        raise ValueError("Only certificate objects and erased padding may be backed up/written")
    return certs

def candidate_image(pem):
    cert=x509.load_pem_x509_certificate(pem)
    der=cert.public_bytes(serialization.Encoding.DER)
    if hashlib.sha256(der).hexdigest()!=ROOT_SHA256:raise ValueError("Unexpected official GlobalSign R3 root")
    pem=cert.public_bytes(serialization.Encoding.PEM)
    item=struct.pack("<BBH",1,0,len(pem))+pem+b"\xff"*((-len(pem))%4)
    return struct.pack("<HHI",0xf1f1,1,len(item))+item

def main():
    p=argparse.ArgumentParser();p.add_argument("action",choices=("backup","install","restore"))
    p.add_argument("--xsdb",type=Path,default=display.base.XSDB);a=p.parse_args()
    network.configure();app=display.checked_app();out=display.ROOT/"output/z7_nano/tls"
    program=json.loads((display.OUT/"program.json").read_text());active=json.loads((display.OUT/"active_app.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Matching idle application required")
    folder=out/"attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ");folder.mkdir(parents=True)
    record={"status":"pending","action":a.action,"attempt":folder.relative_to(display.ROOT).as_posix(),
        "program_attempt":program["attempt"],"cable_serial":program["cable_serial"],"elf_sha256":app["elf_sha256"],
        "input_hashes":display.hashes(["scripts/provision_z7_nano_ca.py","scripts/z7_nano_ca.tcl",
            "scripts/z7_nano_tls_uart.tcl","scripts/probe_z7_nano_tls.py","hardware/esp8266/globalsign_r3.pem",
            "hardware/esp8266/source.json"]),"stages":[],
        "scope":"Public client_ca only, address0xfc000 size8192; no ESP firmware, private-key partition, Wi-Fi credential or cloud key writes"}
    backup_file=folder/"unused.bin";image_file=folder/"unused.bin"
    if a.action!="backup":
        backup=json.loads((out/"ca_backup.json").read_text())
        if backup["status"]!="passed" or backup["cable_serial"]!=program["cable_serial"]:raise RuntimeError("Verified same-board CA backup required")
        backup_file=display.ROOT/backup["attempt"]/"client_ca_backup.bin"
        raw=backup_file.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=backup["backup_sha256"]:raise RuntimeError("Backup file changed")
        public_image(raw)
        if a.action=="install":image=candidate_image((display.ROOT/"hardware/esp8266/globalsign_r3.pem").read_bytes())
        else:image=raw
        public_image(image);image_file=folder/"client_ca_image.bin";image_file.write_bytes(image)
        record.update(backup=backup["attempt"],image_sha256=hashlib.sha256(image).hexdigest(),image_bytes=len(image))
    with display.base.hardware_lock():
        process=subprocess.Popen([str(a.xsdb),str(display.ROOT/"scripts/z7_nano_ca.tcl"),program["cable_serial"],a.action,str(backup_file),str(image_file)],
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace",cwd=folder)
        lines=[]
        def read_output():
            for line in process.stdout:
                lines.append(line)
                m=re.fullmatch(r"CA_STAGE (read|erased|written|verified) (\d+)\n",line)
                if m:
                    record["stages"].append({"stage":m[1],"bytes":int(m[2])})
                    print(f"CA_STAGE {m[1]} {m[2]}",flush=True)
        reader=threading.Thread(target=read_output,daemon=True);reader.start()
        try:
            process.wait(timeout=600);reader.join(timeout=5);text="".join(lines)
            if process.returncode or "PASS_Z7_CA_OPERATION" not in text:raise RuntimeError("CA operation incomplete; inspect stage before any further UART command")
            counters=re.findall(r"^TLS_INVENTORY_COUNTERS (\d+) (\d+)$",text,re.M)
            if not counters or counters[-1]!=("0","0"):raise RuntimeError("UART errors during CA operation")
            if a.action=="backup":
                match=re.search(r"^CA_BACKUP ([0-9a-f]{16384})$",text,re.M)
                if not match:raise RuntimeError("Incomplete public CA backup")
                raw=bytes.fromhex(match[1]);record["certificates"]=public_image(raw)
                (folder/"client_ca_backup.bin").write_bytes(raw)
                record["backup_sha256"]=hashlib.sha256(raw).hexdigest()
            record["status"]="passed"
        except (Exception,KeyboardInterrupt) as error:
            if process.poll() is None:
                subprocess.run(["taskkill","/PID",str(process.pid),"/T","/F"],capture_output=True,check=True,timeout=10);process.wait(timeout=10)
            record.update(status="failed",error=str(error))
        finally:
            display.save(folder/"ca_operation.json",record)
            display.save(out/("ca_backup.json" if a.action=="backup" else "ca_operation.json"),record)
    print(json.dumps(record,ensure_ascii=False));raise SystemExit(record["status"]!="passed")

if __name__=="__main__":main()
