"""Compare a stopped or TLS-API-fault-frozen trace with its relay connection.

Network-first failures are not latched by this diagnostic. Stop that run before
automatic reconnection can replace it. Connection numbers alone are insufficient;
the first complete BIO block must also identify the relay stream.
"""
import argparse
import json
from pathlib import Path

def compare(board,relay,trace_name='arm_tls_tx_trace'):
    t=board['counters'][trace_name]
    if t[0]!=0x31545854 or t[1]!=relay['session']:raise ValueError('Trace/relay connection identity differs')
    remote=dict(relay['blocks'])
    origin=board['counters']['arm_tls_tx_trace']
    identity=board['counters'].get('arm_tls_tx_identity')
    if identity is None:
        # Older short traces still retain block 1; wrapped older traces cannot
        # establish independent identity and must not produce a corruption claim.
        first=dict(zip(origin[4::2],origin[5::2])).get(1)
        identity=[origin[1],first]
    if origin[1]!=t[1] or identity[0]!=t[1] or identity[1] is None or remote.get(1)!=identity[1]:
        raise ValueError('First BIO block does not establish the same relay connection')
    blocks=[(t[i],t[i+1]) for i in range(4,260,2) if t[i] and t[i]*256<=relay['uplink_bytes']]
    mismatch=[index for index,fnv in blocks if remote.get(index)!=fnv]
    return {'status':('matched' if not mismatch else 'mismatch') if blocks else 'inconclusive',
        'scope':'FNV-1a fingerprints of common 256-byte ciphertext blocks; no payload capture or model-quality claim',
        'elf_sha256':board['elf_sha256'],'connection':t[1],'board_trace':trace_name,'first_block_identity':'matched',
        'board_accepted_bytes':t[2],'relay_received_bytes':relay['uplink_bytes'],
        'compared_blocks':len(blocks),'compared_block_range':[min((i for i,_ in blocks),default=0),max((i for i,_ in blocks),default=0)],
        'mismatched_blocks':mismatch,'unreceived_tail_bytes':max(0,t[2]-relay['uplink_bytes'])}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board',type=Path,required=True);p.add_argument('--relay',type=Path,required=True)
    p.add_argument('--trace',choices=('arm_tls_tx_trace','network_tx_queue_trace','network_tx_uart_trace','network_tx_wire_trace'),default='arm_tls_tx_trace')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=compare(json.loads(a.board.read_text()),json.loads(a.relay.read_text()),a.trace)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result));return 0 if result['status']=='matched' else 1
if __name__=='__main__':raise SystemExit(main())
