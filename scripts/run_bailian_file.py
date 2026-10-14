"""Paced 16 kHz PCM to Bailian's text-only real-time translator; stdin credentials."""
import argparse
import asyncio
import base64
import hashlib
import json
import re
import ssl
import time
import uuid
import wave
from datetime import datetime,timezone
from pathlib import Path
import websockets
from network_connect import read_credentials_line

ROOT=Path(__file__).resolve().parents[1]
MODEL="qwen3.8-livetranslate-flash-realtime"


async def translate(credentials,pcm,bind,record):
    uri=f"wss://{credentials['workspace']}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model={MODEL}"
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
                                  "session":{"translation":{"language":"zh"},"output_modalities":["text"]}}))
        updated=await receive()
        if updated["type"]!="session.updated":raise RuntimeError("Session configuration not acknowledged")
        start=time.monotonic();source={};target={};task=None

        async def send_audio():
            for offset in range(0,len(pcm),1024):
                await ws.send(json.dumps({"event_id":uuid.uuid4().hex,"type":"input_audio_buffer.append",
                                          "audio":base64.b64encode(pcm[offset:offset+1024]).decode("ascii")}))
                await asyncio.sleep(max(0,start+min(offset+1024,len(pcm))/32000-time.monotonic()))
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
    parser.add_argument("--bind-ip",required=True);args=parser.parse_args()
    with wave.open(str(args.audio),"rb") as audio:
        if (audio.getframerate(),audio.getnchannels(),audio.getsampwidth(),audio.getcomptype())!=(16000,1,2,"NONE"):
            parser.error("Audio must be 16 kHz mono 16-bit PCM WAV")
        pcm=audio.readframes(audio.getnframes())
    credentials=json.loads(read_credentials_line())
    if not re.fullmatch(r"ws-[a-z0-9]+",credentials["workspace"]):raise SystemExit("Invalid business workspace")
    folder=ROOT/"output/cloud_speech/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record={"status":"pending","checked_utc":datetime.now(timezone.utc).isoformat(),"model":MODEL,
            "audio_sha256":hashlib.sha256(args.audio.read_bytes()).hexdigest(),"audio_seconds":len(pcm)/32000,
            "source_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"events":[],"responses":[],
            "scope":"Computer to cloud, paced controlled WAV; no microphone or board cloud integration"}
    try:
        asyncio.run(translate(credentials,pcm,args.bind_ip,record));record["status"]="passed"
    except Exception as error:
        record.update(status="failed",error_type=type(error).__name__)
    credentials.clear();display=json.dumps(record,ensure_ascii=False,indent=2)+"\n"
    (folder/"result.json").write_text(display,encoding="utf-8")
    (ROOT/"output/cloud_speech/file_result.json").write_text(display,encoding="utf-8")
    print(json.dumps({k:v for k,v in record.items() if k in ("status","transcript","translation","first_translation_seconds","finished_seconds","error_type","cloud_error_code")},ensure_ascii=False))
    raise SystemExit(record["status"]!="passed")


if __name__=="__main__":main()
