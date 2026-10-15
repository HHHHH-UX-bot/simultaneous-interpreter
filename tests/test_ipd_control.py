"""AT completion markers are control bytes, never bytes inside +IPD payloads."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from wifi_link import IpdStream

class IPDControl(unittest.TestCase):
    def test_fragmented_payload_does_not_forge_send_completion(self):
        payload=b"frame\r\nSEND OK\r\nOK\r\n>inside"
        before=b"\r\nOK\r\n>";after=b"\r\nSEND OK\r\n"
        wire=before+b"+IPD,0,"+str(len(payload)).encode()+b":"+payload+after
        for size in (1,2,17,512):
            with self.subTest(size=size):
                control=bytearray();parser=IpdStream(control.extend);frames=[]
                for offset in range(0,len(wire),size):frames.extend(parser.feed(wire[offset:offset+size]))
                self.assertEqual(frames,[(0,payload)])
                self.assertEqual(control,before+after)
    def test_no_sink_preserves_existing_frame_api(self):
        parser=IpdStream()
        self.assertEqual(parser.feed(b"noise+IP"),[])
        self.assertEqual(parser.feed(b"D,0,3:abc\r\nOK\r\n"),[(0,b"abc")])
    def test_unfinished_ipd_prefix_is_retained(self):
        control=bytearray();parser=IpdStream(control.extend)
        self.assertEqual(parser.feed(b"\r\nSEND OK\r\n+IPD,"),[])
        self.assertEqual(control,b"\r\nSEND OK\r\n")
        self.assertEqual(parser.feed(b"0,1:x"),[(0,b"x")])

if __name__=="__main__":unittest.main()
