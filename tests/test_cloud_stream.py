"""Offline wire/ownership checks; no credentials, sockets or board operations."""
import base64,json,re,struct,sys,unittest,uuid
from collections import deque
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/"scripts"))
from websockets.client import ClientProtocol
from websockets.server import ServerProtocol
from websockets.uri import parse_uri
from websockets.http11 import Request
from websockets.frames import Frame,Opcode
from z7_nano_wss_link import EspWss
from pilot_z7_nano_cloud_file import make_file,cloud_wait,CLOUD_FIELDS
from run_bailian_file import audio_chunks

def pair():
    c=ClientProtocol(parse_uri("wss://example.test/session"));s=ServerProtocol()
    request=c.connect();request.headers["Authorization"]="Bearer PUBLIC-TEST"
    c.send_request(request)
    for data in c.data_to_send():s.receive_data(data)
    for event in s.events_received():
        if isinstance(event,Request):s.send_response(s.accept(event))
    for data in s.data_to_send():c.receive_data(data)
    c.events_received();return c,s

class FakeBridge:
    def __init__(self,server,fail=False):self.server=server;self.fail=fail;self.parts=deque();self.sent=[];self.closed=False
    def send(self,data):
        self.sent.append(data)
        if data.startswith(b"AT+CIPSEND="):
            self.server.send_text(b'{"type":"note","text":"SEND OK > ERROR"}')
            wire=b"".join(self.server.data_to_send())
            ipd=f"\r\n+IPD,0,{len(wire)}:".encode()+wire
            self.parts.extend([ipd[:5],ipd[5:15],ipd[15:],b"\r\nOK\r\n",b"> "])
        else:self.parts.append(b"\r\nERROR\r\n" if self.fail else b"\r\nSEND OK\r\n")
    def drain(self):return self.parts.popleft() if self.parts else b""
    def close(self):self.closed=True

class StreamTests(unittest.TestCase):
    def test_cloud_stop_waits_for_its_ack_before_checking_old_error(self):
        def state(phase,error,ack):
            values=dict(phase=phase,error=error,ack=ack)
            return ' '.join(format(values.get(key,0),'x') for key in CLOUD_FIELDS)
        replies=deque((state(6,102,1),state(0,0,2)))
        class Link:
            class bridge:
                @staticmethod
                def rpc(operation):return replies.popleft()
        self.assertEqual(cloud_wait(Link(),0,2)['phase'],0)
        replies.append(state(6,102,1))
        with self.assertRaisesRegex(RuntimeError,'ARM_cloud_error'):cloud_wait(Link(),1,1)
    def setUp(self):
        self.marker=ROOT/"tmp/z7_nano/stream/test_owners"/(uuid.uuid4().hex+".json")
    def link(self,fail=False):
        c,s=pair();bridge=FakeBridge(s,fail)
        with patch("z7_nano_wss_link.Bridge",return_value=bridge),patch("z7_nano_wss_link.OWNER",self.marker):
            link=EspWss("mock",ROOT,{"status":"pending","events":[]})
        link.ws=c;return link,bridge
    def test_file_is_masked_audio_only_and_exact(self):
        c,s=pair();chunks=audio_chunks(b"\0"*2240,"opus");blob,records=make_file(chunks)
        magic,count,size=struct.unpack_from("<III",blob)
        self.assertEqual((magic,count,size),(0x31465453,len(records),len(blob)))
        self.assertNotIn(b"Authorization",blob);self.assertNotIn(b"PUBLIC-TEST",blob)
        at=12;events=[];times=[]
        while at<len(blob):
            due,n=struct.unpack_from("<II",blob,at);wire=blob[at+8:at+8+n];at+=8+n
            self.assertTrue(wire[1]&128);self.assertLessEqual(n,2048);times.append(due)
            s.receive_data(wire)
            events.extend(json.loads(event.data) for event in s.events_received() if isinstance(event,Frame))
        self.assertEqual(times,sorted(times));self.assertEqual(times[-1],70)
        self.assertEqual(events[-1]["type"],"session.finish")
        self.assertEqual(b"".join(base64.b64decode(e["audio"]) for e in events[:-1]),b"".join(data for data,_ in chunks))
    def test_large_packet_is_rejected(self):
        c,_=pair()
        with self.assertRaisesRegex(ValueError,"capacity"):make_file([(b"x"*2000,0.02)])
    def test_eight_page_group_preserves_full_audio_and_finish(self):
        chunks=audio_chunks(b"\0"*32000,"opus");c,s=pair()
        blob,records=make_file(chunks,8);events=[]
        for _,wire in records:
            self.assertLessEqual(len(wire),2048);s.receive_data(wire)
            events.extend(json.loads(e.data) for e in s.events_received() if isinstance(e,Frame))
        self.assertEqual(events[-1]["type"],"session.finish")
        self.assertEqual(b"".join(base64.b64decode(e["audio"]) for e in events[:-1]),b"".join(data for data,_ in chunks))
        self.assertEqual(records[-1][0],1000)
        self.assertEqual(len(records),(len(chunks)+7)//8+1)
    def test_ipd_tokens_cannot_replace_prompt_or_send_ok(self):
        link,bridge=self.link();link.ws.send_text(b'{"type":"test"}');link.flush()
        self.assertEqual(len(bridge.sent),2);self.assertFalse(link.pending)
        self.assertEqual(link.messages.popleft()["text"],"SEND OK > ERROR")
    def test_error_preserves_pending_and_blocks_cleanup_commands(self):
        link,bridge=self.link(True);link.connected=True;link.ws.send_text(b"test")
        with self.assertRaises(RuntimeError):link.flush()
        self.assertTrue(link.pending);before=len(bridge.sent);link.close()
        self.assertEqual(len(bridge.sent),before);self.assertEqual(link.record["status"],"failed");self.assertTrue(bridge.closed)
        self.assertTrue(self.marker.exists())
        with patch("z7_nano_wss_link.OWNER",self.marker),patch("z7_nano_wss_link.Bridge") as reopened:
            with self.assertRaisesRegex(RuntimeError,"recovery"):EspWss("mock",ROOT,{})
            reopened.assert_not_called()
    def test_PS_owner_blocks_direct_uart(self):
        link,bridge=self.link();link.ps_owner=True;link.connected=True
        with self.assertRaisesRegex(RuntimeError,"owns UART"):link.flush()
        link.close();self.assertEqual(bridge.sent,[])
    def test_partial_ipd_finishes_before_handoff(self):
        link,bridge=self.link();bridge.server.send_text(b'{"type":"note"}')
        wire=b"".join(bridge.server.data_to_send());raw=f"+IPD,0,{len(wire)}:".encode()+wire
        link.feed_uart(raw[:3]);bridge.parts.append(raw[3:]);link.settle_ipd()
        self.assertFalse(link.stream.buffer);self.assertEqual(link.messages.popleft()["type"],"note")

if __name__=="__main__":unittest.main()
