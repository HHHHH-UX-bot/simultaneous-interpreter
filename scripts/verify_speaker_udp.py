"""Numeric FPGA and offline enrollment/known/unknown test, using public audio."""
import ctypes as ct,hashlib,json,sys,time
from pathlib import Path
import numpy as np
from speaker_board_test import Board,ROOT
sys.path.insert(0,str(ROOT/'tmp/speaker_cnn/deps'))
import soundfile as sf
model=np.fromfile(ROOT/'output/speaker_cnn/full100_bn/export/model.bin',dtype='<u4')
mfcc_lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/mfcc_reference.dll'));mfcc_fn=mfcc_lib.speaker_mfcc_frames
mfcc_fn.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint32,ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
cnn_lib=ct.CDLL(str(ROOT/'tmp/speaker_cnn/cnn_reference.dll'));cnn_fn=cnn_lib.speaker_cnn_core
cnn_fn.argtypes=[ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_int16),ct.POINTER(ct.c_uint32)]
def reference(pcm):
 n=(len(pcm)-400)//160+1;features=np.zeros((n,16),np.int16);energy=np.zeros(n,np.uint32)
 assert mfcc_fn(pcm.ctypes.data_as(ct.POINTER(ct.c_int16)),len(pcm),features.ctypes.data_as(ct.POINTER(ct.c_int16)),energy.ctypes.data_as(ct.POINTER(ct.c_uint32)))==n
 embeddings=[]
 for at in range(0,n-127,64):
  if np.count_nonzero(energy[at:at+128]>2000)<64:continue
  v=np.ascontiguousarray(features[at:at+128]);out=np.zeros(64,np.int16);status=ct.c_uint32()
  cnn_fn(v.ctypes.data_as(ct.POINTER(ct.c_int16)),model.ctypes.data_as(ct.POINTER(ct.c_uint32)),out.ctypes.data_as(ct.POINTER(ct.c_int16)),ct.byref(status));assert not status.value;embeddings.append(out)
 return features,energy,np.array(embeddings)
def mean(raw):
 v=raw.astype(np.float64);v/=np.maximum(np.linalg.norm(v,axis=1,keepdims=True),1e-9);m=v.mean(0);return m/np.linalg.norm(m)
def main():
 fixture=ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech/train-clean-5/1088/134315/1088-134315-0000.flac';pcm,rate=sf.read(str(fixture),frames=20720,dtype='int16');pcm=np.ascontiguousarray(pcm);assert rate==16000
 features,energy,raw=reference(pcm);assert len(raw)==1
 repeated=np.tile(pcm,7);_,_,enroll_ref=reference(repeated);enrollment_mean=mean(enroll_ref[:8])
 unknown=None;unknown_file=None;cosine=None
 for file in sorted((ROOT/'tmp/speaker_cnn/corpus/audio/LibriSpeech/dev-clean').rglob('*.flac')):
  v,rate=sf.read(str(file),frames=20720,dtype='int16')
  if rate!=16000 or len(v)!=20720:continue
  v=np.tile(np.ascontiguousarray(v),3);_,_,e=reference(v)
  if len(e)<3:continue
  score=float(mean(e[-3:])@enrollment_mean)
  if score<.1:unknown=v;unknown_file=file;cosine=score;break
 assert unknown is not None,'Public unknown-speaker fixture unavailable'
 board=Board();record=dict(scope='Actual board FPGA and C enrollment with public injected audio; no room/microphone/human-identity accuracy claim',elf_sha256=board.load['elf_sha256'],bit_sha256=board.load['bit_sha256'],model_sha256=hashlib.sha256(model.tobytes()).hexdigest(),fixture=fixture.relative_to(ROOT).as_posix(),unknown_fixture=unknown_file.relative_to(ROOT).as_posix(),unknown_selection_reference_cosine=cosine)
 try:
  initial=board.observe();assert initial['OBS_DIAG'][16]==0,'Only an empty test registry may be used'
  board.control(2);board.control(28);board.control(20);board.mode(7)
  record['numeric_feed']=board.feed(pcm);time.sleep(.2)
  values=board.run('puts "OBS_EMBEDDING [mrd -force -value 0x43c40100 64]"\nputs "OBS_FEATURES [mrd -force -value 0x43c41000 2048]"\nputs "OBS_COUNTERS [mrd -force -value 0x43c4002c 14]"')
  embedding=np.array(values['OBS_EMBEDDING'],np.uint32).astype(np.uint16).view(np.int16);got=np.array(values['OBS_FEATURES'],np.uint32).astype(np.uint16).view(np.int16).reshape(128,16)
  assert np.array_equal(embedding,raw[0]) and np.array_equal(got,features);assert values['OBS_COUNTERS'][:7]==[20720,128,1,0,0,0,0]
  record['numeric_counters']=values['OBS_COUNTERS'];print('BOARD_UDP_NUMERIC_BIT_EXACT',flush=True)
  board.mode(5)
  record['open_settings']=board.click(920,554);record['open_ai']=board.click(760,460)
  record['enroll_command']=board.control(31,arm_pcm=True);assert record['enroll_command']['OBS_COMMAND']==[1]
  pending=board.observe();assert pending['OBS_DIAG'][12:14]==[1,0] and pending['OBS_DIAG'][14:16]==[0,0],pending
  record['enroll_feed']=board.feed(repeated,stop_on_reset=True);time.sleep(.3);enrolled=board.observe();assert enrolled['OBS_DIAG'][12]==0 and enrolled['OBS_DIAG'][16]==1,enrolled
  record['enrolled']=enrolled;board.picture('board_enrolled.png');print('BOARD_LOCAL_ENROLLMENT_COMPLETED',flush=True)
  board.mode(7);record['known_feed']=board.feed(np.tile(pcm,3));time.sleep(.3);known=board.observe();assert known['OBS_DIAG'][17]==1,known
  record['known']=known;board.picture('board_known_speaker.png');print('BOARD_KNOWN_SPEAKER_1',flush=True)
  board.mode(7);record['unknown_feed']=board.feed(unknown);time.sleep(.3);other=board.observe();assert other['OBS_DIAG'][17]==0 and other['OBS_DIAG'][16]==1,other
  record['unknown']=other;board.picture('board_unknown_speaker.png');print('BOARD_UNKNOWN_REJECTED',flush=True)
  record['erase_click']=board.click(360,450);assert record['erase_click']['command']==36
  time.sleep(.3);assert board.observe()['OBS_DIAG'][16]==0
  record['passed']=True
 finally:
  board.control(35);board.control(36);board.mode(5);board.close()
  record['test_mode_disabled']=True
  (ROOT/'output/speaker_cnn/board_functional.json').write_text(json.dumps(record,indent=2)+'\n')
 print('PASS_BOARD_SPEAKER_FPGA_NUMERIC_ENROLL_KNOWN_UNKNOWN_ERASE',flush=True)
if __name__=='__main__':main()
