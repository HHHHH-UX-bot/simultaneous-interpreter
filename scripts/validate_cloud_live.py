"""Exercise real C framing, final-event matching and UI; no board or API calls."""
import ctypes,hashlib,json,os,re,subprocess,sys
from pathlib import Path
from websockets.frames import Frame,Opcode
from PIL import Image
from validate_subtitle_ui import Stats
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_cloud_stream import pair
SOURCES=('software/network_ui/cloud_frame.c','software/network_ui/cloud_wire.c',
    'software/network_ui/cloud_events.c','software/network_ui/cloud_live.c','software/network_ui/protocol.c',
    'software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c','tests/cloud_live_host.c')
HEADERS=('software/network_ui/cloud_wire.h','software/network_ui/cloud_events.h','software/network_ui/cloud_live.h',
    'software/network_ui/stream_pilot.h','software/network_ui/network.h','software/network_ui/protocol.h',
    'software/subtitle_ui/ui_model.h','software/subtitle_ui/ui_assets.h')
def main():
    folder=ROOT/'tmp/z7_nano/cloud_live';folder.mkdir(parents=True,exist_ok=True)
    out=ROOT/'output/z7_nano/cloud_live';out.mkdir(parents=True,exist_ok=True)
    dll=folder/'cloud_live.dll';gcc=Path('D:/cenvironment/mingw64/bin/gcc.exe')
    built=subprocess.run([str(gcc),'-shared','-std=c11','-Wall','-Wextra','-Werror','-O2',
        '-I',str(ROOT/'third_party/wslay/lib/includes'),'-I',str(ROOT/'software/subtitle_ui'),
        *[str(ROOT/p) for p in SOURCES],'-o',str(dll)],capture_output=True,timeout=30)
    (folder/'build.log').write_bytes(built.stdout+built.stderr);built.check_returncode()
    directory=os.add_dll_directory(str(gcc.parent));lib=ctypes.CDLL(str(dll))
    lib.cloud_host_feed.argtypes=[ctypes.c_void_p,ctypes.c_uint32];lib.cloud_host_sample.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
    for name in ('source','translation'):getattr(lib,'cloud_host_'+name).restype=ctypes.POINTER(ctypes.c_uint32)
    lib.cloud_host_reply.restype=ctypes.POINTER(ctypes.c_uint8);lib.cloud_host_reply.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
    checks=0
    def check(value,message):
        nonlocal checks;checks+=1
        if not value:raise AssertionError(message)
    def sample():
        values=(ctypes.c_uint32*16)();lib.cloud_host_sample(values);return list(values)
    def feed(data,chunk=17):
        for i in range(0,len(data),chunk):
            block=data[i:i+chunk];lib.cloud_host_feed(ctypes.c_char_p(block),len(block))
    def event(value):feed(Frame(Opcode.TEXT,json.dumps(value,ensure_ascii=False).encode()).serialize(mask=False))
    def text(name):
        p=getattr(lib,'cloud_host_'+name)();return ''.join(chr(p[i]) for i in range(256) if p[i])
    events=[{'type':'conversation.item.created','item':{'id':'T1'},'previous_item_id':'S1'},
        {'type':'response.text.done','item_id':'T1','response_id':'R1','text':'开始。'},
        {'type':'conversation.item.input_audio_transcription.completed','item_id':'S1','transcript':'Hello.'},
        {'type':'response.done','response':{'id':'R1','status':'completed'}}, {'type':'session.finished'}]
    lib.cloud_host_start()
    for e in events[:3]:event(e)
    check(lib.cloud_host_final_count()==0,'text.done alone cannot finalize')
    event(events[3]);check(lib.cloud_host_final_count()==1,'matched complete pair commits once')
    event(events[3]);check(lib.cloud_host_final_count()==1,'duplicate response.done cannot duplicate history')
    event(events[4]);check(sample()[4:6]==[2,0],'finish succeeds after all pending pairs commit')
    check(text('translation')=='开始。' and text('source')=='Hello.','ID matching preserves real source/translation')
    lib.cloud_host_start()
    for e in events[:4]:event(e)
    event(events[2]);event(events[1]);event(events[0]);event(events[4])
    check(sample()[4:6]==[2,0] and lib.cloud_host_final_count()==1,'replayed source/target/link IDs do not reopen a committed pair')
    lib.cloud_host_start();lib.cloud_host_control(1)
    for e in events:event(e)
    check(lib.cloud_host_final_count()==0 and not text('translation'),'pause does not let cloud results backfill')
    lib.cloud_host_start();lib.cloud_host_control(3)
    for e in events:event(e)
    check(lib.cloud_host_final_count()==0 and sample()[5]==0,'clear drops old epoch without resuming')
    lib.cloud_host_start()
    payload=json.dumps(events[0]).encode();at=len(payload)//2
    feed(Frame(Opcode.TEXT,payload[:at],fin=False).serialize(mask=False),1)
    feed(Frame(Opcode.PING,b'ping').serialize(mask=False),1)
    feed(Frame(Opcode.CONT,payload[at:]).serialize(mask=False),1)
    for e in events[1:]:event(e)
    n=ctypes.c_uint32();p=lib.cloud_host_reply(ctypes.byref(n));wire=bytes(p[:n.value]);_,server=pair();server.receive_data(wire)
    frames=server.events_received();check(any(isinstance(f,Frame) and f.opcode is Opcode.PONG and f.data==b'ping' for f in frames),'interleaved ping gets masked pong')
    check(sample()[9]==1 and sample()[12]==1,'fragmented JSON survives interleaved control')
    lib.cloud_host_start()
    feed(Frame(Opcode.PING,b'first').serialize(mask=False)+Frame(Opcode.PING,b'last').serialize(mask=False),1024)
    state=sample();p=lib.cloud_host_reply(ctypes.byref(n));_,server=pair();server.receive_data(bytes(p[:n.value]))
    frames=server.events_received()
    check(state[5]==0 and state[12]==2 and any(isinstance(f,Frame) and f.opcode is Opcode.PONG and f.data==b'last' for f in frames),'coalesced pings reply to latest without a queue or protocol fault')
    lib.cloud_host_start()
    for e in events:event(e)
    feed(Frame(Opcode.PING,b'pending').serialize(mask=False)+Frame(Opcode.CLOSE,b'\x03\xe8').serialize(mask=False),1024)
    state=sample();p=lib.cloud_host_reply(ctypes.byref(n));_,server=pair();server.receive_data(bytes(p[:n.value]))
    check(state[5]==0 and state[13]==1 and any(isinstance(f,Frame) and f.opcode is Opcode.CLOSE for f in server.events_received()),'close replaces unsent pong after completed session')
    lib.cloud_host_start();feed(b'\x81\x7e\x10\x01',1)
    check(sample()[5]==0,'incomplete frame waits without allocating its declared payload')
    feed(b'x',1);check(sample()[5]!=0,'oversize frame fails before copying its first payload byte')
    for bad in (b'\xc1\x00',Frame(Opcode.TEXT,b'{}').serialize(mask=True),b'\x80\x00',b'\x89\x7e\x00\x7e'):
        lib.cloud_host_start();feed(bad,1);check(sample()[5]!=0,'wire violation must fail')
    lib.cloud_host_start()
    for e in events[:3]:event(e)
    event({'type':'response.done','response':{'id':'R1','status':'incomplete'}})
    # The service cancels an in-flight response when the speaker starts again:
    # the partial sentence is dropped and the session keeps listening (cloud_live.c).
    check(lib.cloud_host_final_count()==0 and sample()[5]==0,'cancelled response is dropped without failing the session')
    lib.cloud_host_request(2);check(sample()[3:6]==[2,0,0],'explicit stop clears prior error and detaches consumer')
    lib.cloud_host_request(1);check(sample()[3:6]==[1,1,0],'next cloud session starts without a board reload')
    lib.cloud_host_start()
    for e in events[:3]:event(dict(e,text='\U0001f600') if e['type']=='response.text.done' else e)
    event(events[3]);state=sample()
    # A character outside the font subset is shown as a space and the sentence
    # still commits, instead of failing the session (cloud_live.c cloud_commit).
    check(state[5]==0 and lib.cloud_host_final_count()==1 and text('translation')==' ','missing glyph commits as a space without failing the session')
    fixture=ROOT/'output/z7_nano/cloud_events/linked_minute_events.jsonl'
    replay=[]
    if fixture.exists():
        replay=[json.loads(line) for line in fixture.read_text(encoding='utf-8').splitlines()]
        lib.cloud_host_start()
        for e in replay:event(e)
        state=sample();expected=sum(e['type']=='response.done' for e in replay)
        check(state[4:6]==[2,0] and state[9]==expected,'entire real minute message stream commits every pair')
        check(lib.cloud_host_final_count()==expected and lib.cloud_host_history_count()==min(4,expected),'UI final/history accounting')
        check(text('translation')==[e['text'] for e in replay if e['type']=='response.text.done'][-1],'last real translation exact')
    else:
        lib.cloud_host_start()
        for e in events:event(e)
    host=json.loads((ROOT/'output/subtitle_ui/host_validation.json').read_text(encoding='utf-8'))
    check(host['Status']=='passed' and all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in host['SourceHashes'].items()),'current native renderer inputs')
    renderer_path=ROOT/host['NativeLibrary']
    check(hashlib.sha256(renderer_path.read_bytes()).hexdigest()==host['NativeLibrarySHA256'],'native renderer library identity')
    renderer=ctypes.CDLL(str(renderer_path));lib.cloud_host_model.restype=ctypes.c_void_p
    renderer.ui_host_from_state.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Stats)]
    pixels=(ctypes.c_uint32*614400)();stats=Stats()
    check(renderer.ui_host_from_state(lib.cloud_host_model(),host['ModelBytes'],60000,0,pixels,ctypes.byref(stats))==1,'actual cloud model renders through the native UI')
    check(stats.missing_glyphs==0 and stats.clipped_glyph_pixels==0,'cloud final caption and history have no glyph loss')
    raw=bytes(pixels);preview=out/'native_cloud_caption.png';Image.frombytes('RGB',(1024,600),raw,'raw','BGRX').save(preview)
    names=(*SOURCES,*HEADERS,'scripts/validate_cloud_live.py','scripts/validate_subtitle_ui.py',
        'tests/test_cloud_stream.py','third_party/wslay/source.json',
        'third_party/wslay/lib/wslay_frame.c','third_party/wslay/lib/wslay_frame.h','third_party/wslay/lib/wslay_net.h','third_party/wslay/lib/includes/wslay/wslay.h')
    record={'status':'passed','checks':checks,'replayed_events':len(replay),'completed_pairs':lib.cloud_host_final_count(),
        'native_caption':{'source':text('source'),'translation':text('translation'),
            'frame_sha256':hashlib.sha256(raw).hexdigest(),'preview':preview.relative_to(ROOT).as_posix(),
            'missing_glyphs':stats.missing_glyphs,'clipped_glyph_pixels':stats.clipped_glyph_pixels,
            'renderer_validation_sha256':hashlib.sha256((ROOT/'output/subtitle_ui/host_validation.json').read_bytes()).hexdigest()},
        'input_hashes':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names},
        'scope':'Actual C frame/JSON/ID-to-UI pipeline on native host, not AT/TLS or ARM execution'}
    for name in ('test_cloud_rpc.py','test_cloud_stream.py'):
        tested=subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'tests'/name)],capture_output=True,timeout=30)
        (folder/(name+'.log')).write_bytes(tested.stdout+tested.stderr);tested.check_returncode()
        record[name]=int(re.search(rb'Ran (\d+) tests',tested.stderr)[1])
    record['input_hashes'].update({name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
        'tests/test_cloud_rpc.py','scripts/pilot_z7_nano_cloud_file.py','scripts/z7_nano_uart_rpc.tcl')})
    if replay:record['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest()
    (out/'host_validation.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('status','checks','replayed_events','scope')}))
if __name__=='__main__':main()
