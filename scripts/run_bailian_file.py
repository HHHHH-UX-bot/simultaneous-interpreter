"""Paced 16 kHz PCM/Opus to Bailian's text-only translator; stdin credentials."""
import argparse
import asyncio
import base64
import hashlib
import io
import json
import re
import ssl
import time
import uuid
import wave
from datetime import datetime,timezone
from fractions import Fraction
from pathlib import Path
import sys
import websockets
from network_connect import read_credentials_line

ROOT=Path(__file__).resolve().parents[1]
MODEL="qwen3.8-livetranslate-flash-realtime"
OPUS_MODEL="qwen3.5-livetranslate-flash-realtime"


def audio_chunks(pcm,audio_format):
    """20 ms PCM chunks or Ogg/Opus pages, paced by their sample timeline."""
    if audio_format=="pcm":
        return [(pcm[i:i+640],min(i+640,len(pcm))/32000) for i in range(0,len(pcm),640)]
    sys.path.insert(0,str(ROOT/"output/model_backend/runtime/site-packages"))
    import av
    buffer=io.BytesIO()
    output=av.open(buffer,"w",format="ogg",options={"page_duration":"20000"})
    stream=output.add_stream("libopus",rate=16000);codec=stream.codec_context
    codec.layout="mono";codec.format="s16";codec.bit_rate=24000
    codec.options={"application":"voip","frame_duration":"20","vbr":"off"}
    for i in range(0,len(pcm),640):
        samples=min(320,(len(pcm)-i)//2)
        frame=av.AudioFrame(format="s16",layout="mono",samples=samples)
        frame.sample_rate=16000;frame.time_base=Fraction(1,16000);frame.pts=i//2
        frame.planes[0].update(pcm[i:i+samples*2])
        for packet in stream.encode(frame):output.mux(packet)
    for packet in stream.encode(None):output.mux(packet)
    delay=int.from_bytes(codec.extradata[10:12],"little")/48000
    output.close();data=buffer.getvalue();chunks=[];offset=0
    while offset<len(data):
        segments=data[offset+26];size=27+segments+sum(data[offset+27:offset+27+segments])
        granule=int.from_bytes(data[offset+6:offset+14],"little")
        chunks.append((data[offset:offset+size],max(0,granule/48000-delay)))
        offset+=size
    return chunks


def session_config(model,audio_format):
    session={"translation":{"language":"zh"}}
    if model==MODEL:
        session["output_modalities"]=["text"]
    else:
        session.update(modalities=["text"],sample_rate=16000,input_audio_format=audio_format,
                       input_audio_transcription={"model":"qwen3-asr-flash-realtime","language":"en"})
    return session


async def translate(credentials,pcm,bind,record,*,model=MODEL,audio_format="pcm",chunks=None):
    if chunks is None:chunks=audio_chunks(pcm,audio_format)
    record["upload"]={"audio_bytes":0,"base64_bytes":0,"append_json_bytes":0,"websocket_bytes":0,"messages":0}
    uri=f"wss://{credentials['workspace']}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model={model}"
    async with websockets.connect(uri,additional_headers={"Authorization":"Bearer "+credentials["key"]},
                                  ssl=ssl.create_default_context(),proxy=None,compression=None,
                                  local_addr=(bind,0),open_timeout=15,close_timeout=3) as ws:
        async def receive():
            event=json.loads(await asyncio.wait_for(ws.recv(),20))
            if event["type"] in ("error","conversation.item.input_audio_transcription.failed"):
                record["cloud_error_code"]=event["error"]["code"]
                raise RuntimeError("cloud_error")
            return event

        created=await receive()
        if created["type"]!="session.created":raise RuntimeError("Unexpected first event")
        await ws.send(json.dumps({"event_id":uuid.uuid4().hex,"type":"session.update",
                                  "session":session_config(model,audio_format)}))
        updated=await receive()
        if updated["type"]!="session.updated":raise RuntimeError("Session configuration not acknowledged")
        configuration=updated["session"]
        record["acknowledged_input_format"]=(configuration["audio"]["input"]["format"]["type"] if model==MODEL
                                              else configuration["input_audio_format"])
        start=time.monotonic();source={};target={};task=None

        async def send_audio():
            for data,deadline in chunks:
                encoded=base64.b64encode(data).decode("ascii")
                message=json.dumps({"event_id":uuid.uuid4().hex,"type":"input_audio_buffer.append","audio":encoded},separators=(",",":"))
                await ws.send(message)
                stats=record["upload"];length=len(message)
                stats["audio_bytes"]+=len(data);stats["base64_bytes"]+=len(encoded)
                stats["append_json_bytes"]+=length;stats["websocket_bytes"]+=length+(6 if length<126 else 8 if length<65536 else 14)
                stats["messages"]+=1
                await asyncio.sleep(max(0,start+deadline-time.monotonic()))
            record["input_finished_seconds"]=round(time.monotonic()-start,3)
            await ws.send(json.dumps({"event_id":uuid.uuid4().hex,"type":"session.finish"}))

        task=asyncio.create_task(send_audio())
        try:
            while True:
                event=await receive();kind=event["type"];at=round(time.monotonic()-start,3)
                record["events"].append({"type":kind,"seconds":at})
                if kind=="conversation.item.input_audio_transcription.delta":
                    ident=event["item_id"];source[ident]=source.get(ident,"")+event["delta"]
                elif kind=="conversation.item.input_audio_transcription.completed":
                    source[event["item_id"]]=event["transcript"]
                elif kind=="response.text.delta":
                    record.setdefault("first_translation_seconds",at)
                    ident=event["response_id"];target[ident]=target.get(ident,"")+event["delta"]
                elif kind=="response.text.text":
                    if event["text"] or event["stash"]:record.setdefault("first_translation_seconds",at)
                    ident=event["response_id"];target[ident]=target.get(ident,"")+event["text"]
                elif kind=="response.text.done":
                    target[event["response_id"]]=event["text"]
                elif kind=="response.done":
                    response=event["response"]
                    if response["status"]!="completed":raise RuntimeError("Translation response not completed")
                    record["responses"].append({"seconds":at,"text":target[response["id"]],"usage":response.get("usage")})
                elif kind=="session.finished":break
            await task
        finally:
            if not task.done():
                task.cancel()
            await asyncio.gather(task,return_exceptions=True)
        record.update(transcript=" ".join(source.values()),translation="".join(value["text"] for value in record["responses"]),
                      finished_seconds=round(time.monotonic()-start,3))
        if not record["responses"] or not record["translation"]:raise RuntimeError("No completed translation")


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--audio",type=Path,required=True)
    parser.add_argument("--bind-ip",required=True)
    parser.add_argument("--model",choices=(MODEL,OPUS_MODEL),default=MODEL)
    parser.add_argument("--format",choices=("pcm","opus"),default="pcm");args=parser.parse_args()
    if args.format=="opus" and args.model!=OPUS_MODEL:parser.error("Opus is documented only for qwen3.5")
    with wave.open(str(args.audio),"rb") as audio:
        if (audio.getframerate(),audio.getnchannels(),audio.getsampwidth(),audio.getcomptype())!=(16000,1,2,"NONE"):
            parser.error("Audio must be 16 kHz mono 16-bit PCM WAV")
        pcm=audio.readframes(audio.getnframes())
    chunks=audio_chunks(pcm,args.format)
    credentials=json.loads(read_credentials_line())
    if not re.fullmatch(r"ws-[a-z0-9]+",credentials["workspace"]):raise SystemExit("Invalid business workspace")
    folder=ROOT/"output/cloud_speech/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record={"status":"pending","checked_utc":datetime.now(timezone.utc).isoformat(),"model":args.model,"input_format":args.format,
            "audio_sha256":hashlib.sha256(args.audio.read_bytes()).hexdigest(),"audio_seconds":len(pcm)/32000,
            "encoded_playout_seconds":chunks[-1][1],"packet_audio_sizes":sorted(set(len(data) for data,_ in chunks)),
            "packetization":"Ogg/Opus, one page per append, 20 ms page target" if args.format=="opus" else "20 ms PCM per append",
            "upload_metric_scope":"audio append JSON and masked WebSocket frames; excludes setup, finish, TCP and TLS overhead",
            "source_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"events":[],"responses":[],
            "scope":"Computer to cloud, paced controlled WAV; no microphone or board cloud integration"}
    try:
        asyncio.run(translate(credentials,pcm,args.bind_ip,record,model=args.model,audio_format=args.format,chunks=chunks));record["status"]="passed"
    except Exception as error:
        record.update(status="failed",error_type=type(error).__name__)
    credentials.clear();display=json.dumps(record,ensure_ascii=False,indent=2)+"\n"
    (folder/"result.json").write_text(display,encoding="utf-8")
    (ROOT/"output/cloud_speech/file_result.json").write_text(display,encoding="utf-8")
    print(json.dumps({k:v for k,v in record.items() if k in ("status","transcript","translation","first_translation_seconds","finished_seconds","error_type","cloud_error_code")},ensure_ascii=False))
    raise SystemExit(record["status"]!="passed")


if __name__=="__main__":main()
