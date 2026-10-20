import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/repair'))
from uart_wire_decode import payload,unpack,decode,analyze

def pack(samples,second=0):
    data=np.pad(np.asarray(samples,dtype=np.uint8),(0,(-len(samples))%4),constant_values=1)
    pairs=data|(second<<1)
    return np.bitwise_or.reduce(pairs.reshape(-1,4)<<(2*np.arange(4,dtype=np.uint8)),axis=1).tobytes()

def waveform(data,stop=1):
    bits=[1]*10
    for byte in data:bits.extend([0]+[(byte>>bit)&1 for bit in range(8)]+[stop])
    return np.repeat(np.array(bits+[1]*10,dtype=np.uint8),20)

class UARTWireDecodeTest(unittest.TestCase):
    def test_all_values_and_ring_boundaries(self):
        expected=payload(33041);result=analyze(pack(waveform(expected),second=1),expected)
        self.assertEqual(result['status'],'passed')
        self.assertEqual(result['decoded_bytes'],33041)
        self.assertEqual(result['estimated_prefix_baud'],1000000)
    def test_stop_error_is_not_success(self):
        expected=payload(600);samples=waveform(expected)
        samples[200+int(9.5*20)]=0
        self.assertNotEqual(analyze(pack(samples),expected)['status'],'passed')
    def test_missing_or_mutated_byte_is_rejected(self):
        expected=payload(700)
        for wrong in (expected[:-1],expected[:600]+expected[601:],expected[:600]+b'!'+expected[601:],expected+b'!',b'!'+expected):
            self.assertEqual(analyze(pack(waveform(wrong)),expected)['status'],'failed')
    def test_idle_and_truncated_capture(self):
        expected=payload(600)
        self.assertEqual(analyze(pack(np.ones(1000,dtype=np.uint8)),expected)['status'],'failed')
        got,_,_=decode(waveform(b'A')[:300],20000000,1000000)
        self.assertEqual(got,b'')
    def test_two_channel_packing(self):
        first=np.array([0,1,0,1,1,0,1,0],dtype=np.uint8)
        raw=pack(first,second=1)
        np.testing.assert_array_equal(unpack(raw,0),first)
        np.testing.assert_array_equal(unpack(raw,1),np.ones(8,dtype=np.uint8))

if __name__=='__main__':unittest.main()
