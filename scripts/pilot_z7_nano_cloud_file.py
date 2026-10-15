"""PS-paced CIPSEND of a public Ogg file; PC controls WSS setup and parses replies."""
import argparse,base64,hashlib,json,struct,time,wave
from datetime import datetime,timezone
from pathlib import Path
from websockets.frames import Frame,Opcode
from network_connect import read_credentials_line
from run_bailian_file import audio_chunks,session_config,OPUS_MODEL
from z7_nano_wss_link import EspWss
import run_z7_nano_network as network
import run_z7_nano_display as display
FIELDS=("magic","sequence","phase","error","request","ack","frames","transmitted","received","max_lag_ms",
        "start_ms","sent_ms","busy","network_error","overflow","framing")

def sample(link):return dict(zip(FIELDS,(int(v,16) for v in link.bridge.rpc("sample").split())))
def make_file(chunks,pages_per_append=4):
    records=[];due=0
    for i in range(0,len(chunks),pages_per_append):
        part=chunks[i:i+pages_per_append]
        message=json.dumps({"event_id":f"audio_{i}","type":"input_audio_buffer.append",
            "audio":base64.b64encode(b"".join(data for data,_ in part)).decode()},separators=(",",":")).encode()
        records.append((round(due*1000),Frame(Opcode.TEXT,message).serialize(mask=True)))
        due=part[-1][1]
    records.append((round(due*1000),Frame(Opcode.TEXT,b'{"event_id":"finish","type":"session.finish"}').serialize(mask=True)))
    body=b"".join(struct.pack("<II",when,len(data))+data for when,data in records)
    if len(body)+12>0x80000 or any(not data or len(data)>2048 for _,data in records):raise ValueError("Pilot file capacity")
    return struct.pack("<III",0x31465453,len(records),len(body)+12)+body,records

def main():
    p=argparse.ArgumentParser();p.add_argument("--audio",type=Path,required=True)
    p.add_argument("--pages-per-append",type=int,choices=(4,8),default=4)
    p.add_argument("--defer-observation",action="store_true",help="Leave JTAG idle until five seconds after audio ends")
    p.add_argument("--credentials-stdin",required=True,action="store_true");a=p.parse_args()
    with wave.open(str(a.audio),"rb") as audio:
        if (audio.getframerate(),audio.getnchannels(),audio.getsampwidth(),audio.getcomptype())!=(16000,1,2,"NONE"):
            p.error("16 kHz mono S16 PCM WAV required")
        pcm=audio.readframes(audio.getnframes())
    if not 0<len(pcm)<=60*32000:p.error("Pilot file duration must be at most 60 seconds")
    chunks=audio_chunks(pcm,"opus");key=json.loads(read_credentials_line())["key"]
    if not isinstance(key,str) or not key.startswith("sk-") or not key.isascii() or any(c.isspace() for c in key) or len(key)>512:
        raise SystemExit("API input rejected")
    network.configure();app=display.checked_app()
    program=json.loads((display.OUT/"program.json").read_text());active=json.loads((display.OUT/"active_app.json").read_text())
    tls=json.loads((display.ROOT/"output/z7_nano/tls/preflight.json").read_text())
    if active["status"]!="ready" or active["elf_sha256"]!=app["elf_sha256"] or active["program_attempt"]!=program["attempt"] or tls["status"]!="verified":
        raise RuntimeError("Matching active app and trusted TLS reference required")
    out=display.ROOT/"output/z7_nano/stream"
    folder=out/"attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ");folder.mkdir(parents=True)
    record={"status":"pending","attempt":folder.relative_to(display.ROOT).as_posix(),"events":[],"model":OPUS_MODEL,
        "program_attempt":program["attempt"],"elf_sha256":app["elf_sha256"],"tls_reference":tls["attempt"],
        "audio_sha256":hashlib.sha256(a.audio.read_bytes()).hexdigest(),"audio_seconds":len(pcm)/32000,
        "pages_per_append":a.pages_per_append,
        "observation":"after_audio_plus_5s" if a.defer_observation else "during_audio",
        "timing_scope":"PC observation times; deferred mode cannot measure cloud response latency",
        "input_hashes":display.hashes(["scripts/pilot_z7_nano_cloud_file.py","scripts/z7_nano_wss_link.py",
            "scripts/run_bailian_file.py","scripts/z7_nano_uart_rpc.tcl","scripts/z7_nano_tls_uart.tcl"]),
        "scope":"PS independently schedules audio CIPSEND and receives TCP data; PC sets up WSS and interprets cloud events; not independent full cloud client, real microphone or translated HDMI display"}
    link=None;stage="uart";seen=0;source={};target={};final=[];finished=False;samples=[]
    with display.base.hardware_lock():
        try:
            link=EspWss(program["cable_serial"],folder,record)
            state=sample(link)
            if state["phase"]==3:
                link.bridge.rpc("pilot","3");limit=time.monotonic()+3
                while time.monotonic()<limit:
                    state=sample(link)
                    if state["phase"]==0 and state["ack"]==3:break
                else:raise RuntimeError("pilot_reset_timeout")
            if state["phase"]!=0:raise RuntimeError("pilot_not_idle")
            stage="preload";data,frames=make_file(chunks,a.pages_per_append)
            (folder/"stream_file.bin").write_bytes(data);preload_start=time.monotonic()
            link.bridge.rpc("load",f"{len(data):x}",timeout=300)
            record.update(file_bytes=len(data),file_sha256=hashlib.sha256(data).hexdigest(),frames=len(frames),
                          websocket_transmit_bytes=sum(len(data) for _,data in frames),
                          preload_seconds=round(time.monotonic()-preload_start,3))
            print("PASS_STREAM_PRELOAD",flush=True)
            stage="tls";link.connect(OPUS_MODEL,key);key=None
            link.wait_event("session.created")
            link.ws.send_text(json.dumps({"event_id":"session_config","type":"session.update","session":session_config(OPUS_MODEL,"opus")}).encode())
            configured=link.wait_event("session.updated")
            if configured["session"]["input_audio_format"]!="opus":raise RuntimeError("codec_not_acknowledged")
            link.flush();link.settle_ipd()
            stage="PS_audio";started=time.monotonic();link.claim_ps();link.bridge.rpc("pilot","1")
            print("STREAM_AUDIO_RUNNING",flush=True)
            deadline=started+len(pcm)/32000+35
            observe_after=started+len(pcm)/32000+5 if a.defer_observation else started
            while time.monotonic()<deadline:
                if time.monotonic()<observe_after:
                    time.sleep(0.1);continue
                state=sample(link);samples.append(state)
                if state["error"] or state["phase"]==6:raise RuntimeError("PS_stream_error")
                while seen<state["received"]:
                    tcp=bytes.fromhex(link.bridge.rpc("tcp",f"{state['received']:x}"));seen+=len(tcp);link.feed_tcp(tcp)
                while link.messages:
                    event=link.messages.popleft();kind=event["type"];at=round(time.monotonic()-started,3)
                    record["events"].append({"type":kind,"seconds":at})
                    if kind in ("error","conversation.item.input_audio_transcription.failed"):
                        record["cloud_error_code"]=event["error"]["code"];raise RuntimeError("cloud_error")
                    if kind=="conversation.item.input_audio_transcription.completed":source[event["item_id"]]=event["transcript"]
                    elif kind=="response.text.text":
                        if event["text"] or event["stash"]:record.setdefault("first_translation_observed_seconds",at)
                    elif kind=="response.text.done":target[event["response_id"]]=event["text"]
                    elif kind=="response.done":
                        if event["response"]["status"]!="completed":raise RuntimeError("incomplete_translation")
                        final.append(target[event["response"]["id"]])
                    elif kind=="session.finished":finished=True
                if finished and state["phase"]==2:break
                if record.get("cloud_closed"):raise RuntimeError("cloud_close")
                time.sleep(0.1)
            if not finished or state["phase"]!=2 or not "".join(final).strip() or not " ".join(source.values()).strip():
                raise RuntimeError("no_completed_translation")
            if state["frames"]!=len(frames) or state["transmitted"]!=record["websocket_transmit_bytes"]:
                raise RuntimeError("transmit_accounting")
            record.update(transcript=" ".join(source.values()),translation="".join(final),PS=state,
                          finished_observed_seconds=round(time.monotonic()-started,3),status="passed")
        except (Exception,KeyboardInterrupt) as error:record.update(status="failed",stopped_at=stage,error_type=type(error).__name__)
        finally:
            key=None
            if link:
                if link.ps_owner:
                    try:
                        link.bridge.rpc("pilot","2");limit=time.monotonic()+5
                        while time.monotonic()<limit:
                            state=sample(link)
                            if state["phase"]==3 and state["ack"]==2:link.release_ps();record["released"]=True;break
                    except Exception:record["released"]=False
                link.close()
            record["samples"]=samples
            display.save(folder/"result.json",record);display.save(out/"latest.json",record)
    print(json.dumps({k:v for k,v in record.items() if k not in ("samples","input_hashes","events")},ensure_ascii=False))
    raise SystemExit(record["status"]!="passed")
if __name__=="__main__":main()
