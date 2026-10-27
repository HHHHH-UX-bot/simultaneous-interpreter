"""Native JSON event oracle plus ARM object build; no board or cloud access."""
import hashlib,json,re,subprocess,sys
from collections import Counter
from pathlib import Path
from run_z7_nano import GCC
ROOT=Path(__file__).resolve().parents[1]
INPUTS=("software/network_ui/cloud_events.c","software/network_ui/cloud_events.h",
        "third_party/jsmn/jsmn.h","third_party/jsmn/LICENSE","third_party/jsmn/source.json",
        "tests/cloud_events_test.c","tests/fixtures/cloud_events_reference.jsonl","scripts/validate_cloud_events.py")

def joined(event,*keys):
    """Streaming text: concatenated string fields, keeping the latest 255 code points like the C buffer."""
    return "".join(event[k] for k in keys if isinstance(event.get(k),str))[-255:]

def expected(event):
    code=kind=done=0;item=response=error=text=previous="";t=event["type"]
    if t=="conversation.item.input_audio_transcription.completed":kind=1;item=event["item_id"];text=event["transcript"]
    elif t=="conversation.item.input_audio_transcription.text":
        kind=11
        if "item_id" in event:item=event["item_id"];text=joined(event,"text","stash")
        else:code=2  # CLOUD_SCHEMA: a preview must name its item
    elif t in ("response.text.text","response.audio_transcript.text"):
        kind=9;item=event.get("item_id","");response=event.get("response_id","");text=joined(event,"text","stash")
    elif t=="response.text.delta":kind=9;item=event.get("item_id","");response=event.get("response_id","");text=event.get("delta","")
    elif t=="input_audio_buffer.speech_started":kind=10
    elif t=="input_audio_buffer.speech_stopped":kind=12
    elif t=="session.created":kind=7
    elif t=="session.updated":kind=8
    elif t=="response.audio.done":kind=14
    elif t=="response.text.done":kind=2;item=event["item_id"];response=event["response_id"];text=event["text"]
    elif t=="response.done":kind=3;response=event["response"]["id"];done=int(event["response"]["status"]=="completed")
    elif t=="session.finished":kind=4
    elif t=="conversation.item.created" and event.get("previous_item_id"):kind=6;item=event["item"]["id"];previous=event["previous_item_id"]
    elif t in ("error","conversation.item.input_audio_transcription.failed"):kind=5;error=event["error"]["code"]
    return [code,kind,done,*[list(map(ord,s)) for s in (item,response,error,previous,text.replace("\n"," ").replace("\r"," ").replace("\t"," "))]]

def run(command,log,**kwargs):
    result=subprocess.run([str(v) for v in command],capture_output=True,timeout=30,**kwargs)
    log.write_bytes(result.stdout+result.stderr);result.check_returncode();return result.stdout

def main():
    temp=ROOT/"tmp/z7_nano/cloud_events";out=ROOT/"output/z7_nano/cloud_events"
    temp.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
    exe=temp/"cloud_events_test.exe";source=ROOT/INPUTS[0]
    run(["D:/cenvironment/mingw64/bin/gcc.exe","-std=c11","-Wall","-Wextra","-Werror","-O2",source,
        ROOT/"tests/cloud_events_test.c","-o",exe],temp/"native_build.log")
    units=run([exe,"--unit"],out/"units.log").decode()
    fixture=ROOT/"tests/fixtures/cloud_events_reference.jsonl"
    events=[json.loads(line) for line in fixture.read_text(encoding="utf-8").splitlines()]
    full=out/"linked_minute_events.jsonl"
    if full.exists():events.extend(json.loads(line) for line in full.read_text(encoding="utf-8").splitlines())
    cases=[]
    for event in events:
        cases.append(json.dumps(event,ensure_ascii=False,separators=(",",":")))
        escaped=json.dumps(dict(reversed(list(event.items()))),ensure_ascii=True)
        cases.append(escaped)
        cases.append(re.sub(r'"(type|text|transcript|response|status|id|item_id|response_id|error|code|completed|session.finished)"',
            lambda m:'"\\u%04x%s"'%(ord(m[1][0]),m[1][1:]),escaped))
    oracle=run([exe],out/"oracle.log",input=("\n".join(cases)+"\n").encode())
    actual=[json.loads(line) for line in oracle.decode().splitlines()]
    assert len(actual)==len(cases)
    for line,result in zip(cases,actual):assert result==expected(json.loads(line)),"C event differs from Python oracle"
    obj=temp/"cloud_events.o"
    run([GCC,"-mcpu=cortex-a9","-marm","-mfloat-abi=soft","-O2","-ffreestanding","-fno-builtin",
        "-Wall","-Wextra","-Werror","-fstack-usage","-c",source,"-o",obj],temp/"arm_build.log")
    nm=GCC.with_name(GCC.name.replace("gcc.exe","nm.exe"));size=GCC.with_name(GCC.name.replace("gcc.exe","size.exe"))
    undefined=run([nm,"-u",obj],temp/"undefined.log").decode().split()
    assert not any(name in undefined for name in ("malloc","calloc","realloc","free"))
    arm_size=run([size,obj],temp/"size.log").decode().splitlines()[-1].split()
    record={"status":"passed","native_checks":int(re.findall(r"checks=(\d+)",units)[-1]),"oracle_cases":len(cases),
        "fixture_event_types":dict(Counter(e["type"] for e in events)),
        "arm_object":{"text_bytes":int(arm_size[0]),"data_bytes":int(arm_size[1]),"bss_bytes":int(arm_size[2]),
            "stack_usage":[line.replace(str(ROOT)+"\\","") for line in obj.with_suffix(".su").read_text().splitlines()],
            "undefined_symbols":undefined},
        "input_hashes":{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS},
        "scope":"C event decode against public API replies and Unicode/boundary cases; ARM object compiled only, not linked or downloaded; not WebSocket, UI mapping or full JSON conformance validation"}
    if full.exists():record["minute_fixture_sha256"]=hashlib.sha256(full.read_bytes()).hexdigest()
    (out/"validation.json").write_text(json.dumps(record,indent=2)+"\n")
    print(json.dumps(record))
if __name__=="__main__":main()
