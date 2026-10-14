"""Local framing tests: no Wi-Fi, JTAG or real credentials."""
import io,json,struct,sys,unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from network_demo_client import Peer,model_text,model_history,history_entry,TRANSLATION,SECOND,LAST,SOURCE
from network_connect import payload,check_ready,read_credentials_line

class Wire:
    def __init__(self,ack,chunk=3):self.ack=ack;self.chunk=chunk;self.sent=[]
    def sendall(self,p):self.sent.append(p)
    def recv(self,n):p=self.ack[:min(n,self.chunk)];self.ack=self.ack[len(p):];return p
def ack(sequence=1,type=0,code=0):return struct.pack('<8I',0x314b4341,1,sequence,type,code,2,1,1)
class Tests(unittest.TestCase):
    def test_fragmented_ack_and_request(self):
        w=Wire(ack());p=Peer(w);self.assertEqual(p.send(0)[5],2)
        self.assertEqual(struct.unpack('<9I',w.sent[0]),(0x31425553,1,1,0,0,0,0,0,0))
    def test_chinese_payload_byte_length(self):
        w=Wire(ack(type=5));p=Peer(w);p.epoch=2;p.send(5,1,2,1,TRANSLATION)
        self.assertEqual(struct.unpack_from('<I',w.sent[0],32)[0],len(TRANSLATION.encode()))
        self.assertEqual(w.sent[0][36:].decode(),TRANSLATION)
    def test_incomplete_or_mismatched_ack(self):
        for data in (ack()[:20],ack(sequence=2),ack(type=1),ack(code=3),b'\0'*32):
            with self.assertRaises(RuntimeError):Peer(Wire(data)).send(0)
    def test_duplicate_and_stale(self):
        w=Wire(ack(sequence=3,code=7));p=Peer(w);p.request=3;p.epoch=2
        p.send(0,request=3,code=7,epoch=1);self.assertEqual(p.request,3)
        self.assertEqual(struct.unpack_from('<I',w.sent[0],16)[0],1)
    def test_ack_epoch_requires_expected_change(self):
        changed=bytearray(ack(type=3));struct.pack_into('<I',changed,20,99)
        p=Peer(Wire(bytes(changed)));p.epoch=2
        with self.assertRaises(RuntimeError):p.send(3,1,1,0,'Network test')
        resumed=bytearray(ack(type=1));struct.pack_into('<I',resumed,20,3)
        p=Peer(Wire(bytes(resumed)));p.epoch=2;self.assertEqual(p.send(1,2)[5],3)
    def test_ack_journal_survives_active_connection_failure(self):
        events=[];saved=[];p=Peer(Wire(ack()),events,lambda:saved.append(list(events)))
        p.send(0)
        with self.assertRaises(RuntimeError):p.send(0)
        self.assertEqual(len(events),1);self.assertEqual(len(saved[0]),1)
    def test_retained_history_contents(self):
        b=bytearray(32+11384+32);struct.pack_into('<I',b,32+10324,1)
        struct.pack_into('<4I',b,32+2068,2,1,1,2)
        for offset,text in ((2068+16,SOURCE),(2068+1040,TRANSLATION)):
            struct.pack_into('<'+str(len(text))+'I',b,32+offset,*map(ord,text))
        self.assertEqual(model_history(b),[history_entry(2,1,SOURCE,TRANSLATION)])
        b[32+2068:32+2068+2064]=bytes(2064)
        self.assertNotEqual(model_history(b),[history_entry(2,1,SOURCE,TRANSLATION)])
    def test_bound_and_subset(self):
        with self.assertRaises(ValueError):Peer(Wire(b'')).send(3,text='a'*256)
        m=json.loads((Path(__file__).resolve().parents[1]/'output/subtitle_ui/assets_manifest.json').read_text(encoding='utf-8'))
        known={int(s[2:],16) for s in m['codepoints']}
        self.assertTrue(all(ord(c) in known for c in TRANSLATION+SECOND+LAST+SOURCE+'Network test reconnect 0123456789abcdef'))
    def test_snapshot_text_offset(self):
        b=bytearray(32+11384+32)
        for offset,text in ((20,SOURCE),(1044,TRANSLATION)):
            struct.pack_into('<'+str(len(text))+'I',b,32+offset,*map(ord,text))
            self.assertEqual(model_text(b,offset),text)
    def test_private_layout_without_real_credentials(self):
        b=bytes.fromhex(payload({'ssid':'TEST-NET','password':'unit-pass'}).replace(' ',''))
        words=[int(s,16) for s in payload({'ssid':'TEST-NET','password':'unit-pass'}).split()]
        raw=struct.pack('<32I',*words)
        self.assertEqual(len(b),128);self.assertEqual(struct.unpack_from('<5I',raw),(0x3146434e,0,1,8,9))
        self.assertEqual(raw[20:28],b'TEST-NET');self.assertEqual(raw[53:62],b'unit-pass')
        for config in ({'ssid':'','password':'unit-pass'},{'ssid':'test','password':'short'}):
            with self.assertRaises(ValueError):payload(config)
    def test_private_console_no_echo(self):
        fake='{"ssid":"TEST-NET","password":"unit-pass"}'
        with patch('network_connect.sys.stdin.isatty',return_value=True),patch('network_connect.msvcrt.getwch',side_effect=list(fake+'\r')),patch('network_connect.sys.stdout',new_callable=io.StringIO) as output:
            self.assertEqual(read_credentials_line(),fake)
            self.assertNotIn('TEST-NET',output.getvalue());self.assertNotIn('unit-pass',output.getvalue())
        with patch('network_connect.sys.stdin',io.StringIO(fake+'\n')):
            self.assertEqual(read_credentials_line(),fake+'\n')
    def test_station_ready_and_scope(self):
        fields=dict(Phase=3,Error=0,EchoVerified=1,Overflow=0,Framing=0,Client=0xffffffff,StationIP=0xc0a81f2a)
        self.assertEqual(check_ready(fields),'192.168.31.42')
        for k,v in (('Phase',4),('EchoVerified',0),('Overflow',1),('StationIP',0xcb007101)):
            with self.assertRaises(RuntimeError):check_ready({**fields,k:v})
if __name__=='__main__':unittest.main()
