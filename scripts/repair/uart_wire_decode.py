"""Decode captured Combo8 CH0 and compare every byte with a known stimulus."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

PREFIX=bytes([85]*4)+b'Z7WIRE'+bytes([0,255,170,85,195,60])

def payload(length):
    data=bytearray(PREFIX+bytes(range(256))+bytes(reversed(range(256))))
    state=0x12345678
    while len(data)<length:
        state^=(state<<13)&0xffffffff
        state^=state>>17
        state^=(state<<5)&0xffffffff
        data.append(state&255)
    return bytes(data[:length])

def unpack(raw,channel=0):
    words=np.frombuffer(raw,dtype=np.uint8)
    return ((words[:,None]>>(np.arange(4,dtype=np.uint8)*2+channel))&1).reshape(-1)

def decode(samples,rate,baud):
    falling=np.flatnonzero((samples[:-1]==1)&(samples[1:]==0))+1
    period=rate/baud
    values=bytearray();starts=[];errors=0;cursor=0
    for edge in falling:
        if edge<cursor:continue
        positions=np.rint(edge+period*(np.arange(8)+1.5)).astype(np.int64)
        stop=int(round(edge+period*9.5))
        if stop>=len(samples):break
        if samples[int(round(edge+period*.5))]!=0:continue
        if samples[stop]!=1:errors+=1
        value=int(np.dot(samples[positions],1<<np.arange(8)))
        values.append(value);starts.append(int(edge))
        cursor=edge+period*9.8
    return bytes(values),starts,errors

def analyze(raw,expected,rate=20_000_000,baud=1_000_000):
    samples=unpack(raw)
    decoded,starts,errors=decode(samples,rate,baud)
    offset=decoded.find(PREFIX)
    result={'status':'failed','sample_rate_hz':rate,'uart_baud':baud,
            'decoded_bytes':len(decoded),'framing_errors':errors,'prefix_offset':offset,
            'expected_bytes':len(expected),'expected_sha256':hashlib.sha256(expected).hexdigest(),
            'high_fraction':round(float(samples.mean()),6)}
    if offset>=0:
        actual=decoded[offset:offset+len(expected)]
        mismatches=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
        result.update({'matched_length':len(actual),'byte_mismatches':len(mismatches),
                       'first_mismatch_offsets':mismatches[:8],
                       'actual_sha256':hashlib.sha256(actual).hexdigest(),
                       'first_byte_sample':starts[offset]})
        first=starts[offset];last=min(len(samples),first+int(rate/baud*40))
        transitions=np.flatnonzero(samples[first:last-1]!=samples[first+1:last])
        widths=np.diff(transitions)
        if len(widths):
            result['prefix_edge_width_median_samples']=float(np.median(widths))
            result['estimated_prefix_baud']=float(rate/np.median(widths))
        if not errors and not mismatches and offset==0 and len(decoded)==len(expected):result['status']='passed'
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path);p.add_argument('expected',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();meta=json.loads((a.capture/'capture.json').read_text())
    raw=(a.capture/'samples_2ch.bin').read_bytes()
    if len(raw)!=meta['bytes'] or hashlib.sha256(raw).hexdigest()!=meta['sha256']:
        raise RuntimeError('Capture length/hash does not match its completed manifest')
    result=analyze(raw,a.expected.read_bytes(),meta['sample_rate_hz'])
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
    raise SystemExit(0 if result['status']=='passed' else 1)
