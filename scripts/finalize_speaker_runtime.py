"""Leave an empty registry, physical microphone, local Qwen and live cloud ASR."""
import json,time
from datetime import datetime,timezone
from speaker_board_test import Board,ROOT
def main():
 for name in ('board_functional','board_privacy','translation_regression'):
  assert json.loads((ROOT/'output/speaker_cnn'/(name+'.json')).read_text(encoding='utf-8'))['passed']
 board=Board();board.control(2);board.control(28);board.control(20)
 result=dict(at_utc=datetime.now(timezone.utc).isoformat(),elf_sha256=board.load['elf_sha256'],bit_sha256=board.load['bit_sha256'])
 result['open_settings']=board.click(920,554);result['open_ai']=board.click(760,460)
 result['cloud_click']=board.click(120,168);assert result['cloud_click']['command']==29;board.picture('board_cloud_mode.png')
 result['local_click']=board.click(400,168);assert result['local_click']['command']==30;board.picture('board_local_mode.png')
 board.control(36);board.mode(5);board.control(3)
 end=time.monotonic()+45;s=board.symbols
 while time.monotonic()<end:
  actual=board.run(f'''puts "OBS_DIAG [mrd -force -value {s['speaker_diag']} 32]"
puts "OBS_NET [mrd -force -value 0x1e140 16]"
puts "OBS_TLS [mrd -force -value {s['arm_tls_diag']} 24]"
puts "OBS_VOICE [mrd -force -value {s['local_voice_diag']} 24]"
puts "OBS_CONTROL [r 0x43c40008]"
puts "OBS_USB_PP [expr {{([r 0xe0002184]>>12)&1}}]"''')
  if actual['OBS_NET'][2]==4 and actual['OBS_TLS'][1]==2 and actual['OBS_VOICE'][2]==1:break
  time.sleep(.5)
 else:raise TimeoutError('Final ASR readiness not confirmed')
 assert actual['OBS_DIAG'][1]==1 and actual['OBS_DIAG'][2]==0 and actual['OBS_DIAG'][12]==0 and actual['OBS_DIAG'][16]==0
 assert actual['OBS_CONTROL']==[1] and actual['OBS_USB_PP']==[1]
 initial=actual['OBS_DIAG'][20];time.sleep(2);later=board.observe();assert later['OBS_CONTROL']==[1] and later['OBS_DIAG'][20]>initial
 result.update(passed=True,scope='Current verified ELF/PL; real microphone advancing, empty speaker registry, live cloud ASR with board-local Qwen selected, USB power and PC mode controls. No subjective voice/physical touch claim.',state=actual,later=later)
 (ROOT/'output/speaker_cnn/final_ready.json').write_text(json.dumps(result,indent=2)+'\n');board.close();print('PASS_FINAL_SPEAKER_LIVE_MIC_EMPTY_REGISTRY_LOCAL_QWEN_ASR_USB')
if __name__=='__main__':main()
