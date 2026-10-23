"""Regression of PC text -> actual board Qwen -> cloud TTS -> DAC."""
import json,time,urllib.request
from speaker_board_test import Board,ROOT
def status():return json.load(urllib.request.urlopen('http://127.0.0.1:8790/api/status',timeout=5))
def main():
 board=Board();board.control(2);deadline=time.monotonic()+25
 while True:
  before=status()
  if before['state'] not in (2,3,4,5) and not before.get('flags',0)&128:break
  assert time.monotonic()<deadline,'Existing job did not quiesce';time.sleep(.25)
 text='请把窗户关上。';baseline=before['voice']['spoken_count'];start=time.monotonic();first=done=job=None;active=False;events=[]
 request=urllib.request.Request('http://127.0.0.1:8790/api/start',data=json.dumps(dict(text=text,direction='zh_en',max_tokens=32),ensure_ascii=False).encode(),headers={'Content-Type':'application/json','X-Board-LLM':'1'})
 json.load(urllib.request.urlopen(request,timeout=5))
 while time.monotonic()-start<180:
  s=status();elapsed=round((time.monotonic()-start)*1000)
  if s['job'] and s['job']!=before['job']:job=s['job']
  if job and s['job']==job and not s.get('pending') and s.get('phase')!='queued_local':
   if s['state'] in (3,4,5):active=True
   if active:
    if s['output'] and first is None:first=elapsed
    if s['state']==6 and done is None:done=elapsed
    key=(s['phase'],s['flags'],s['generated'])
    if not events or key!=tuple(events[-1][k] for k in ('phase','flags','generated')):events.append(dict(ms=elapsed,**s))
    assert not s['voice'].get('error'),s['voice']
    if done is not None and s['voice']['spoken_count']>baseline:
     voice=s['voice'];assert voice['pcm_received']==voice['pcm_played']>0 and voice['dac_underruns']==0
     assert voice['first_pcm_ms'] and voice['dac_done_ms']>=voice['first_pcm_ms']
     history=json.load(urllib.request.urlopen('http://127.0.0.1:8787/history?format=json',timeout=5));matching=[v for v in history['records'] if v['source']==text];assert matching
     board.control(2)
     result=dict(passed=True,scope='Controlled PC text, real board Qwen translation, cloud TTS PCM and actual DAC completion. Not a new controlled microphone-ASR accuracy or subjective listening test.',elf_sha256=board.load['elf_sha256'],bit_sha256=board.load['bit_sha256'],input=text,first_text_ms=first,model_done_ms=done,whole_voice_ms=elapsed,final_status=s,matching_history=matching,events=events,speaker_after=board.observe())
     (ROOT/'output/speaker_cnn/translation_regression.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');board.close();print(json.dumps({k:result[k] for k in ('passed','input','first_text_ms','model_done_ms','whole_voice_ms')},ensure_ascii=False));print('PASS_SPEAKER_QWEN_TTS_DAC_REGRESSION');return
    if s['state'] in (7,8):raise RuntimeError('Board model failed/cancelled')
  time.sleep(.25)
 raise TimeoutError('Translation/TTS regression did not finish')
if __name__=='__main__':main()
