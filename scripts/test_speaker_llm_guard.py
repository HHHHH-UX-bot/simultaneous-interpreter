"""Enrollment must block even the first PC text job. Model forward is mocked."""
import ctypes as ct,hashlib,json,struct,subprocess,zlib
import os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
files=['software/network_ui/tiny_board_llm.c','software/network_ui/tiny_tokenizer.c','tests/speaker_cnn/llm_stubs.c','tests/speaker_cnn/llm_config.h']
work=ROOT/'tmp/speaker_cnn/llm_guard';work.mkdir(parents=True,exist_ok=True);dll=work/'test.dll'
subprocess.run([os.environ.get('CC','gcc'),'-shared','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'software/network_ui'),'-I',str(ROOT/'third_party/mbedtls/include'),'-include',str(ROOT/files[3]),*[str(ROOT/n) for n in files[:3]],'-lm','-o',str(dll)],check=True,cwd=ROOT)
lib=ct.CDLL(str(dll));lib.tbl_handle_packet.argtypes=[ct.c_uint64,ct.c_char_p,ct.c_size_t,ct.c_void_p,ct.c_size_t];lib.tbl_handle_packet.restype=ct.c_size_t;lib.tbl_test_assets.argtypes=[ct.c_void_p]
tokenizer=ct.create_string_buffer((ROOT/'models/qwen2_0.5b.tokenizer.bin').read_bytes());lib.tbl_test_assets(tokenizer);lib.tbl_init();lib.tbl_set_assets_ready(1,1);lib.tbl_tick()
seq=0
def request(op,job=0,expected=0):
 global seq
 seq+=1;data=struct.pack('<2I',2,0) if op==2 else b'';head=struct.pack('<7I',0x314c4254,op,seq,job,0,8 if op==2 else 0,len(data));packet=head+struct.pack('<I',zlib.crc32(head+data)&0xffffffff)+data
 out=ct.create_string_buffer(1200);n=lib.tbl_handle_packet(123,packet,len(packet),out,len(out));assert n>=96
 h=struct.unpack_from('<8I',out);assert h[5]==expected,(h[5],expected)
 return struct.unpack_from('<16I',out,32)
assert request(1)[1]==1
lib.tbl_test_busy(1);assert request(2,1,4)[3]==0
lib.tbl_test_busy(0);assert request(2,1)[3]==1
lib.tbl_test_busy(1);request(2,1);assert request(2,2,4)[3]==1
lib.tbl_test_busy(0);request(6,1);request(7,1)
lib.tbl_test_busy(1);assert request(2,2,4)[3]==0
lib.tbl_test_busy(0);assert request(2,2)[3]==2
record=dict(passed=True,scope='Actual board packet handler with mocked LLM: UI busy rejects first/replacement jobs, retains idempotent BEGIN; no real model inference',source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files})
(ROOT/'output/speaker_cnn/llm_guard_test.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS_SPEAKER_LLM_FIRST_JOB_GUARD')
