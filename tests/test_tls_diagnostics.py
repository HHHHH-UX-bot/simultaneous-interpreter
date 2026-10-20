"""Offline observer contracts: fragmentation, corruption and unreceived tails."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/repair'))
from tls_record_relay import RecordHeaders
from compare_tls_trace import compare

def record(kind,data):return bytes((kind,3,3))+len(data).to_bytes(2,'big')+data

class DiagnosticTests(unittest.TestCase):
    def test_fragmented_headers_and_nonce_gap(self):
        info={};parser=RecordHeaders(info)
        wire=record(22,b'hello')+record(20,b'\1')
        for nonce in (0,1,3):wire+=record(23,nonce.to_bytes(8,'big')+b'x'*24)
        for offset in range(0,len(wire),3):parser.feed(wire[offset:offset+3])
        self.assertEqual(info['nonce_records'],3)
        self.assertEqual(info['partial_record_bytes'],0)
        self.assertEqual(info['nonce_discontinuities'],[{'offset':5+5+6+2*(5+32),'previous':1,'actual':3}])
    def test_invalid_framing_only_disables_observer(self):
        info={};parser=RecordHeaders(info);parser.feed(b'not TLS')
        self.assertEqual(info['framing_error_offset'],0)
        parser.feed(b'forwarding can continue')
        self.assertEqual(len(parser.buffer),0)
    def test_common_prefix_and_incomplete_tail(self):
        trace=[0x31545854,1,600,0,1,123,2,456]+[0]*252
        board={'elf_sha256':'test','counters':{'arm_tls_tx_trace':trace}}
        relay={'session':1,'uplink_bytes':300,'blocks':[[1,123]]}
        result=compare(board,relay)
        self.assertEqual(result['status'],'matched')
        self.assertEqual(result['compared_blocks'],1)
        self.assertEqual(result['unreceived_tail_bytes'],300)
        relay.update(uplink_bytes=512,blocks=[[1,123],[2,457]])
        self.assertEqual(compare(board,relay)['mismatched_blocks'],[2])
        relay.update(uplink_bytes=0,blocks=[])
        with self.assertRaises(ValueError):compare(board,relay)
        relay['session']=2
        with self.assertRaises(ValueError):compare(board,relay)
    def test_selects_the_requested_board_boundary(self):
        accepted=[0x31545854,3,512,0,1,123,2,456]+[0]*252
        uart=accepted.copy();uart[7]=457
        board={'elf_sha256':'test','counters':{'arm_tls_tx_trace':accepted,
               'network_tx_queue_trace':accepted,'network_tx_uart_trace':uart}}
        relay={'session':3,'uplink_bytes':512,'blocks':[[1,123],[2,457]]}
        self.assertEqual(compare(board,relay,'network_tx_queue_trace')['mismatched_blocks'],[2])
        self.assertEqual(compare(board,relay,'network_tx_uart_trace')['status'],'matched')
        board['counters']['network_tx_wire_trace']=uart+[1,99]
        self.assertEqual(compare(board,relay,'network_tx_wire_trace')['compared_blocks'],2)
    def test_identity_survives_wrap_and_rejects_same_number_in_another_run(self):
        trace=[0x31545854,1,256*129,0,129,321]+[0]*254
        board={'elf_sha256':'test','counters':{'arm_tls_tx_trace':trace,'arm_tls_tx_identity':[1,123]}}
        relay={'session':1,'uplink_bytes':256*129,'blocks':[[1,123],[129,321]]}
        self.assertEqual(compare(board,relay)['status'],'matched')
        relay['blocks'][0][1]=124
        with self.assertRaises(ValueError):compare(board,relay)
        del board['counters']['arm_tls_tx_identity']
        with self.assertRaises(ValueError):compare(board,relay)
if __name__=='__main__':unittest.main()
