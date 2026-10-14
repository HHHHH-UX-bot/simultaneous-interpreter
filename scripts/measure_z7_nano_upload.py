"""Receive exact synthetic blocks from the board and measure real TCP upload."""
import argparse
import json
import socket
import struct
import subprocess
import time
from datetime import datetime,timezone
from pathlib import Path
import run_z7_nano_network as network
import run_z7_nano_display as display


def control(serial,action,duration,folder):
    result=folder/(action+".json")
    done=subprocess.run([str(display.base.XSDB),str(display.ROOT/"scripts/z7_nano_upload.tcl"),serial,
                         action,str(duration),str(result)],capture_output=True,text=True,timeout=15)
    (folder/(action+".log")).write_text(done.stdout+done.stderr,encoding="utf-8")
    done.check_returncode()
    marker="PASS_Z7_UPLOAD_STOP" if action=="stop" else "PASS_Z7_UPLOAD_CONTROL"
    if marker not in done.stdout:raise RuntimeError("Upload control failed; see "+str(folder/(action+".log")))
    return json.loads(result.read_text()) if action!="stop" else None


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--seconds",type=int,default=60)
    parser.add_argument("--local-ip",required=True);args=parser.parse_args()
    if not 1<=args.seconds<=3600:parser.error("Duration must be 1..3600 seconds")
    network.configure();app=display.checked_app()
    program=json.loads((display.OUT/"program.json").read_text())
    active=json.loads((display.OUT/"active_app.json").read_text())
    connection=json.loads((display.OUT/"connection.json").read_text())
    if active.get("status")!="ready" or active.get("elf_sha256")!=app["elf_sha256"] or \
       active.get("program_attempt")!=program["attempt"] or connection.get("status")!="passed" or \
       connection.get("program_attempt")!=program["attempt"] or connection.get("elf_sha256")!=app["elf_sha256"]:
        raise RuntimeError("Matching current upload application and association required")
    folder=display.ROOT/"output/z7_nano/upload/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record={"status":"pending","program_attempt":program["attempt"],"elf_sha256":app["elf_sha256"],
            "attempt":folder.relative_to(display.ROOT).as_posix(),"requested_seconds":args.seconds,
            "scope":"Synthetic board-to-computer TCP payload at 921600; no audio or TLS throughput"}
    packets=0;bins={};first=last=None;device_first=device_last=None;max_gap=0;serial=program["cable_serial"]
    with display.base.hardware_lock():
        try:
            record["before"]=control(serial,"enable",args.seconds*1000,folder)
            with socket.create_connection((connection["station_address"],8266),timeout=5,
                                          source_address=(args.local_ip,0)) as stream:
                stream.settimeout(5)
                while True:
                    block=bytearray()
                    try:
                        while len(block)<1536:
                            chunk=stream.recv(1536-len(block))
                            if not chunk:raise RuntimeError("Upload socket closed before timed completion")
                            block.extend(chunk)
                    except socket.timeout:
                        if first is None or time.monotonic()-first<args.seconds-0.1 or block:
                            raise RuntimeError("Upload stalled or ended with a partial block") from None
                        break
                    magic,sequence,size,device_ms=struct.unpack_from("<4I",block)
                    if magic!=0x31444e42 or sequence!=packets or size!=1536 or \
                       block[16:]!=bytes((packets*17+i*13)&255 for i in range(16,1536)):
                        raise RuntimeError("Upload block ordering or contents differ")
                    now=time.monotonic();first=now if first is None else first
                    if last is not None:max_gap=max(max_gap,now-last)
                    last=now;device_first=device_ms if device_first is None else device_first;device_last=device_ms
                    second=int(now-first);bins[second]=bins.get(second,0)+1536;packets+=1
            record["after"]=control(serial,"sample",0,folder)
            elapsed=last-first
            if record["after"]["duration_ms"]!=0 or elapsed<args.seconds-0.25:
                raise RuntimeError("Timed upload did not cover the requested active span")
            net=record["after"]["network"];wifi=record["after"]["wifi"]
            rate=(packets-1)*1536/elapsed
            record["status"]="passed" if rate>=48000 and not(net[3] or net[12] or net[13] or wifi[10] or wifi[11]) else "insufficient"
        except Exception as error:
            record.update(status="failed",error=str(error))
        finally:
            record["blocks"]=packets
            if packets>1:
                elapsed=last-first
                record.update(bytes=packets*1536,active_seconds=round(elapsed,3),
                              bytes_per_second=round((packets-1)*1536/elapsed,1),
                              device_active_seconds=((device_last-device_first)&0xffffffff)/1000,
                              max_packet_gap_seconds=round(max_gap,3),
                              whole_second_bytes=[bins.get(i,0) for i in range(1,max(bins))])
            try:control(serial,"stop",0,folder)
            except Exception as error:record.update(status="failed",cleanup_error=str(error))
            display.save(folder/"result.json",record)
            display.save(display.ROOT/"output/z7_nano/upload/latest.json",record)
    print(json.dumps({k:v for k,v in record.items() if k in ("status","blocks","bytes_per_second","active_seconds","error")}))
    raise SystemExit(record["status"]!="passed")


if __name__=="__main__":main()
