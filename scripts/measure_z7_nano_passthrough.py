"""Paced synthetic upload over one ESP TCP passthrough link; no cloud key/audio."""
import argparse
import ipaddress
import json
import socket
import struct
import time
from datetime import datetime,timezone
from pathlib import Path
from network_connect import payload,read_credentials_line
from connect_z7_nano_network import private_submit
from measure_z7_nano_upload import control
import run_z7_nano_network as network
import run_z7_nano_display as display
import inspect_z7_nano_network as observer


def read_block(stream,sequence):
    block=bytearray()
    while len(block)<1536:
        part=stream.recv(1536-len(block))
        if not part:
            if block:raise RuntimeError("Partial block at TCP close")
            return None
        block.extend(part)
    magic,number,size,device_ms=struct.unpack_from("<4I",block)
    expected=bytes((sequence*17+i*13)&255 for i in range(16,1536))
    if (magic,number,size)!=(0x31444e42,sequence,1536) or block[16:]!=expected:
        offset=next((i+16 for i,(a,b) in enumerate(zip(block[16:],expected)) if a!=b),None)
        raise RuntimeError(f"Block {sequence} differs: header={magic:08x}/{number}/{size}, first payload mismatch={offset}")
    return device_ms


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--seconds",type=int,default=60)
    parser.add_argument("--local-ip",type=ipaddress.IPv4Address,required=True)
    parser.add_argument("--port",type=int,default=8267)
    parser.add_argument("--credentials-stdin",action="store_true",required=True)
    parser.add_argument("--xsdb",type=Path,default=display.base.XSDB)
    parser.add_argument("--vivado",type=Path,default=display.base.VIVADO)
    args=parser.parse_args()
    if not 1<=args.seconds<=3600 or not 1<=args.port<=65535:parser.error("Invalid duration or port")
    try:data=payload(json.loads(read_credentials_line()),int(args.local_ip),args.port)
    except (Exception,KeyboardInterrupt):raise SystemExit("Wi-Fi input rejected; no device action") from None
    network.configure();app=display.checked_app()
    program=json.loads((display.OUT/"program.json").read_text())
    active=json.loads((display.OUT/"active_app.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"]:
        raise RuntimeError("Matching fresh network app required")
    folder=display.ROOT/"output/z7_nano/upload/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record={"status":"pending","attempt":folder.relative_to(display.ROOT).as_posix(),
            "elf_sha256":app["elf_sha256"],"program_attempt":program["attempt"],"requested_seconds":args.seconds,
            "scope":"25.6 kB/s paced fault-isolation TCP passthrough over four-wire 921600 UART; no audio/TLS",
            "local_ip":str(args.local_ip),"port":args.port}
    packets=0;bins={};first=last=None;device_first=device_last=None;max_gap=0;stream=None;armed=False
    with display.base.hardware_lock(),socket.socket() as listener:
        try:
            before=observer.sample(args,folder/"before")
            if before["NetworkResult"]["Phase"]!=0 or before["wifi"][2:7]!=[2,0,1,1,1]:
                raise RuntimeError("Fresh idle app and independent AT proof required")
            record["before"]=before["NetworkResult"]
            # Listen before committing the volatile connect request.
            listener.bind((str(args.local_ip),args.port));listener.listen(1);listener.settimeout(45)
            control(program["cable_serial"],"arm",args.seconds*1000,folder);armed=True
            record["config_ack"]=private_submit(args,data,folder,program["cable_serial"]);data=None
            stream,address=listener.accept();record["peer_ip"]=address[0];stream.settimeout(6)
            while True:
                device_ms=read_block(stream,packets)
                if device_ms is None:break
                now=time.monotonic();first=now if first is None else first
                if last is not None:max_gap=max(max_gap,now-last)
                last=now;device_first=device_ms if device_first is None else device_first;device_last=device_ms
                second=int(now-first);bins[second]=bins.get(second,0)+1536;packets+=1
            after=control(program["cable_serial"],"sample",0,folder);record["after"]=after
            elapsed=last-first if packets>1 else 0
            if elapsed<args.seconds-0.25 or after["duration_ms"]!=0 or after["network"][2]!=0:
                raise RuntimeError("Full duration and return to normal AT mode not confirmed")
            net=after["network"];wifi=after["wifi"]
            record["status"]="passed" if (packets-1)*1536/elapsed>=48000 and not(net[3] or net[12] or net[13] or wifi[10] or wifi[11]) else "insufficient"
        except (Exception,KeyboardInterrupt) as error:
            record.update(status="failed",error=str(error))
        finally:
            data=None;record["blocks"]=packets
            if packets>1:
                elapsed=last-first
                record.update(bytes=packets*1536,active_seconds=round(elapsed,3),
                    bytes_per_second=round((packets-1)*1536/elapsed,1),
                    device_active_seconds=((device_last-device_first)&0xffffffff)/1000,
                    max_packet_gap_seconds=round(max_gap,3),whole_second_bytes=[bins.get(i,0) for i in range(1,max(bins))])
            if armed:
                try:
                    control(program["cable_serial"],"stop",0,folder)
                    deadline=time.monotonic()+8
                    while True:
                        cleanup=control(program["cable_serial"],"sample",0,folder)
                        record["cleanup"]=cleanup
                        if cleanup["network"][2]==0 and not cleanup["network"][3]:break
                        if cleanup["network"][2]==6 or time.monotonic()>=deadline:
                            raise RuntimeError("Passthrough cleanup did not confirm normal AT mode")
                    record["cleanup"]=cleanup
                except Exception as error:record.update(status="failed",cleanup_error=str(error))
            if stream:stream.close()
            display.save(folder/"result.json",record)
            display.save(display.ROOT/"output/z7_nano/upload/passthrough_latest.json",record)
    print(json.dumps({k:v for k,v in record.items() if k in ("status","blocks","bytes_per_second","active_seconds","error","cleanup_error")}))
    raise SystemExit(record["status"]!="passed")


if __name__=="__main__":main()
