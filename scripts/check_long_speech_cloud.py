"""Direct verified-TLS timing control on a known synthetic multi-sentence input."""
import argparse,base64,difflib,hashlib,json,os,re,time
from pathlib import Path
from check_english_cloud import WebSocket
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--audio-pages',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    audio=json.loads(a.audio_pages.read_text());fixture=json.loads(a.fixture.read_text(encoding='utf-8'))
    expected=''.join(re.findall('[\u4e00-\u9fff]',''.join(s['text'] for s in fixture['sentences'])))
    host=re.search(r'#define HOST "([^"]+)"',(ROOT/'software/network_ui/arm_tls.c').read_text(encoding='utf-8')).group(1)
    session={'modalities':['text','audio'],'voice':'Tina','sample_rate':16000,'input_audio_format':'opus','output_audio_format':'pcm','input_audio_transcription':{'model':'qwen3-asr-flash-realtime','language':'zh'},'turn_detection':{'type':'server_vad','threshold':0.12,'silence_duration_ms':400,'create_response':True,'interrupt_response':False},'translation':{'language':'en','same_language_skip_options':{'skip_text':False,'skip_audio':False}}}
    ws=WebSocket(host,'/api-ws/v1/realtime?model=qwen3.5-livetranslate-flash-realtime',os.environ['Z7_CLOUD_KEY'])
    record=dict(scope='PC direct cloud control; synthetic fixture only; same fixed-point Opus as board, real-time upload',errors=[],events=[],streams={},max_upload_lateness_ms=0)
    start=None;cursor=0;deadline=time.monotonic()+20;last_event=0;asr=[]
    try:
        while time.monotonic()<deadline:
            if start is not None:
                elapsed=time.monotonic()-start
                while cursor<len(audio['pages']) and audio['pages'][cursor]['at']<=elapsed:
                    page=audio['pages'][cursor];record['max_upload_lateness_ms']=max(record['max_upload_lateness_ms'],round((elapsed-page['at'])*1000))
                    ws.send({'event_id':f'fixture_{cursor}','type':'input_audio_buffer.append','audio':base64.b64encode(bytes.fromhex(page['hex'])).decode()});cursor+=1
            e=ws.event()
            if not e:continue
            kind=e.get('type','')
            if kind=='session.created':ws.send({'event_id':'long_test_config','type':'session.update','session':session});continue
            if kind=='session.updated':start=time.monotonic();deadline=start+audio['seconds']+40;continue
            if kind=='error':record['errors'].append(e.get('error',{}).get('code','cloud_error'));break
            if start is None:continue
            ms=round((time.monotonic()-start)*1000);last_event=ms
            event=dict(type=kind,ms=ms,item=e.get('item_id',''),response=e.get('response_id',''))
            if kind=='conversation.item.input_audio_transcription.completed':
                event['text']=e.get('transcript','');asr.append(event['text'])
            elif kind in ('response.audio_transcript.done','response.text.done'):event['text']=e.get('transcript',e.get('text',''))
            if kind=='response.audio.delta':
                samples=len(base64.b64decode(e['delta']))//2;event['samples']=samples
                s=record['streams'].setdefault(e.get('response_id',''),dict(first_ms=ms,last_ms=ms,samples=0,chunks=0))
                s['last_ms']=ms;s['samples']+=samples;s['chunks']+=1
            if kind=='response.audio.done':
                s=record['streams'].get(e.get('response_id',''))
                if s:s['done_ms']=ms
            record['events'].append(event)
            normalized=''.join(re.findall('[\u4e00-\u9fff]',''.join(asr)))
            if cursor==len(audio['pages']) and ms>(audio['seconds']+10)*1000 and difflib.SequenceMatcher(None,expected,normalized,autojunk=False).ratio()>.90 and all('done_ms' in s for s in record['streams'].values()):break
    finally:ws.close()
    normalized=''.join(re.findall('[\u4e00-\u9fff]',''.join(asr)))
    record.update(asr_similarity=difflib.SequenceMatcher(None,expected,normalized,autojunk=False).ratio(),sent_pages=cursor,total_pages=len(audio['pages']),input_ms=fixture['speech_ms'],fixture_sha256=hashlib.sha256(a.fixture.read_bytes()).hexdigest(),asr_text=''.join(asr))
    for s in record['streams'].values():s.update(audio_ms=s['samples']/24,receive_span_ms=s['last_ms']-s['first_ms'])
    record['passed']=not record['errors'] and cursor==len(audio['pages']) and record['asr_similarity']>.90 and len(record['streams'])>=6
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in record.items() if k not in ('events','asr_text')},ensure_ascii=False))
    if not record['passed']:raise SystemExit(1)
if __name__=='__main__':main()
