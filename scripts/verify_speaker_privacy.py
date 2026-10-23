"""Active ASR -> close -> local enrollment -> resume, numeric counters only."""
import json,time
from speaker_board_test import Board,ROOT
def snapshot(board,delay=0):
 s=board.symbols
 read=f'''puts "OBS_NET [mrd -force -value 0x1e140 16]"
puts "OBS_TLS [mrd -force -value {s['arm_tls_diag']} 24]"
puts "OBS_UPLOAD [mrd -force -value {s['upload_diag']} 7]"
puts "OBS_DIAG [mrd -force -value {s['speaker_diag']} 32]"
puts "OBS_VOICE [mrd -force -value {s['local_voice_diag']} 24]"'''
 return board.run((f'after {delay}\n' if delay else '')+read)
def wait_ready(board):
 end=time.monotonic()+45
 while time.monotonic()<end:
  record=snapshot(board)
  if record['OBS_NET'][2]==4 and record['OBS_TLS'][1]==2 and record['OBS_VOICE'][2]==1:return record
  time.sleep(.5)
 raise TimeoutError('ASR did not return to ready')
def main():
 board=Board();result=dict(scope='Real configured ASR/TLS transport paused for enrollment. No waveform/transcript/template export; controlled silent test mode holds the registration open for eight seconds.',elf_sha256=board.load['elf_sha256'],bit_sha256=board.load['bit_sha256'])
 try:
  board.control(3);result['before']=wait_ready(board);assert result['before']['OBS_DIAG'][16]==0
  result['enroll_control']=board.control(34,arm_pcm=True);assert result['enroll_control']['OBS_COMMAND']==[1]
  a=snapshot(board);b=snapshot(board,8000)
  for value in (a,b):assert value['OBS_NET'][2]==0 and value['OBS_TLS'][1]==0 and value['OBS_DIAG'][12:14]==[4,0],value
  assert a['OBS_TLS'][5]==b['OBS_TLS'][5] and a['OBS_TLS'][12]==b['OBS_TLS'][12]
  assert a['OBS_UPLOAD'][2]==b['OBS_UPLOAD'][2]
  assert a['OBS_DIAG'][14:16]==b['OBS_DIAG'][14:16]==[0,0]
  result.update(enrollment_start=a,enrollment_end=b,enrollment_tls_tx_delta=0,enrollment_asr_page_delta=0)
 finally:
  board.control(35);board.mode(5)
 result['resumed']=wait_ready(board);assert result['resumed']['OBS_DIAG'][16]==0
 try:board.client.request(9,data=bytes(32),retries=1)
 except RuntimeError as error:assert 'board error 8:' in str(error)
 else:raise AssertionError('Production microphone mode accepted test PCM')
 result.update(passed=True,test_pcm_rejected_in_production=True,physical_microphone_restored=True)
 (ROOT/'output/speaker_cnn/board_privacy.json').write_text(json.dumps(result,indent=2)+'\n');board.close();print('PASS_SPEAKER_ACTIVE_CLOUD_CLOSED_ZERO_UPLOAD_RESUMED_LIVE_MIC')
if __name__=='__main__':main()
