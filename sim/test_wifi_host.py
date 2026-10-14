import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from wifi_link import IpdStream, at_escape, ATClient, tcp_roundtrip

class HostProtocolTests(unittest.TestCase):
    def test_fragmented_binary_utf8_and_notifications(self):
        payload=bytes(range(256))+"你好\r\nOK\r\n+IPD,:".encode()
        stream=b"0,CONNECT\r\n+IPD,0,"+str(len(payload)).encode()+b":"+payload+b"\r\n0,CLOSED\r\n"
        parser=IpdStream()
        frames=[]
        for byte in stream:
            frames.extend(parser.feed(bytes([byte])))
        self.assertEqual(frames,[(0,payload)])
    def test_multiple_frames(self):
        self.assertEqual(IpdStream().feed(b"+IPD,1,3:one+IPD,1,3:two"),[(1,b"one"),(1,b"two")])
    def test_invalid_or_oversized_frames(self):
        for frame in (b"+IPD,7,1:x",b"+IPD,0,99999:x",b"+IPD,0,no:x"):
            with self.assertRaises(RuntimeError):
                IpdStream().feed(frame)
    def test_credential_escaping_and_control_rejection(self):
        self.assertEqual(at_escape('a"b,c\\d'), 'a\\"b\\,c\\\\d')
        with self.assertRaises(ValueError):
            at_escape("bad\r\nAT")
    def test_send_waits_for_fragmented_prompt(self):
        class FragmentedBridge:
            def __init__(self):
                self.parts=[b"\r\nOK\r\n",b"\r\n>"]
            def drain(self):
                return self.parts.pop(0)
        bridge=FragmentedBridge()
        response,ok=ATClient(bridge).collect(1,prompt=True)
        self.assertTrue(ok)
        self.assertIn(b">",response)
        self.assertEqual(bridge.parts,[])
    def test_server_cleanup_after_enable_timeout(self):
        class FakeAT:
            def __init__(self): self.commands=[]
            def command(self,command,**kwargs):
                self.commands.append(command)
                if command.startswith("AT+CIPSERVER=1"):
                    raise RuntimeError("Simulated lost creation response")
                return b"OK\r\n",True
        at=FakeAT()
        with self.assertRaisesRegex(RuntimeError,"creation response"):
            tcp_roundtrip(at,"192.168.31.10","192.168.31.0/24")
        self.assertEqual(at.commands[-1],"AT+CIPSERVER=0")
    def test_server_shutdown_failure_is_not_ignored(self):
        class FakeAT:
            def command(self,command,**kwargs):
                if command.startswith("AT+CIPSERVER="):
                    raise RuntimeError("Simulated server command timeout")
                return b"OK\r\n",True
        with self.assertRaisesRegex(RuntimeError,"shutdown was not confirmed"):
            tcp_roundtrip(FakeAT(),"192.168.31.10","192.168.31.0/24")

if __name__=="__main__":
    unittest.main()
