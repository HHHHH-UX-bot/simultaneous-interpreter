"""Export all official Qwen2 matrices to true tied-embedding Q4/Q6 group64.

Only each selected embedding row is decoded at runtime; the entire embedding
matrix is never expanded to FP32 on the 512 MiB target.
"""
import argparse,hashlib,json,struct,time
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
MODELS=ROOT/'models';OUT=ROOT/'output';OUT.mkdir(exist_ok=True)
GROUP=64;HEADER_BYTES=8192

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bits',type=int,choices=(4,6),default=6)
    args=parser.parse_args();bits=args.bits;maximum=(1<<(bits-1))-1;zero=1<<(bits-1)
    source=MODELS/'model.safetensors';config=json.loads((MODELS/'config.json').read_text())
    with source.open('rb') as f:
        n=struct.unpack('<Q',f.read(8))[0];header=json.loads(f.read(n));data_offset=8+n
    def values(name):
        d=header[name];assert d['dtype']=='BF16',d
        raw=np.memmap(source,mode='r',dtype='<u2',offset=data_offset+d['data_offsets'][0],shape=tuple(d['shape']))
        return raw
    def f32(a):return (np.asarray(a,dtype=np.uint32)<<16).view(np.float32)
    tensors=[(0,0xffffffff,'model.embed_tokens.weight')]
    for layer in range(config['num_hidden_layers']):
        prefix=f'model.layers.{layer}.'
        names=[(1,'input_layernorm.weight'),(2,'self_attn.q_proj.weight'),(3,'self_attn.k_proj.weight'),
            (4,'self_attn.v_proj.weight'),(5,'self_attn.o_proj.weight'),(6,'post_attention_layernorm.weight'),
            (7,'mlp.gate_proj.weight'),(8,'mlp.up_proj.weight'),(9,'mlp.down_proj.weight'),
            (10,'self_attn.q_proj.bias'),(11,'self_attn.k_proj.bias'),(12,'self_attn.v_proj.bias')]
        tensors.extend((kind,layer,prefix+name) for kind,name in names)
    tensors.append((13,0xffffffff,'model.norm.weight'))
    dest=OUT/('qwen2.5-0.5b-instruct-g64-q6.tqw' if bits==6 else 'qwen2.5-0.5b-instruct-g64.tqw');descriptors=[];stats=[];started=time.monotonic()
    with dest.open('wb+') as output:
        output.write(b'\0'*HEADER_BYTES)
        for kind,layer,name in tensors:
            offset=(output.tell()+63)//64*64;output.seek(offset)
            a=values(name)
            if a.ndim==1:
                output.write(f32(a).astype('<f4').tobytes());rows=len(a);cols=0
            else:
                rows,cols=a.shape;assert cols%GROUP==0
                scale_offset=offset+rows*cols*bits//8
                mean_error=total_count=0
                for begin in range(0,rows,256):
                    count=min(256,rows-begin);w=f32(a[begin:begin+count]).reshape(count,cols//GROUP,GROUP)
                    scales=np.max(np.abs(w),axis=-1)/maximum
                    scales=np.maximum(scales,np.finfo(np.float32).tiny)
                    quant=np.clip(np.rint(w/scales[...,None]),-maximum,maximum).astype(np.int8)
                    unsigned=(quant.astype(np.int16)+zero).astype(np.uint8)
                    low=(unsigned[...,:32]&15)|((unsigned[...,32:]&15)<<4)
                    if bits==6:
                        high=(unsigned[...,:16]>>4)|((unsigned[...,16:32]>>4)<<2)|((unsigned[...,32:48]>>4)<<4)|((unsigned[...,48:64]>>4)<<6)
                        packed=np.concatenate((low,high),axis=-1)
                    else:packed=low
                    output.seek(offset+begin*cols*bits//8);output.write(packed.tobytes())
                    output.seek(scale_offset+begin*(cols//GROUP)*4);output.write(scales.astype('<f4').tobytes())
                    error=np.abs(w-quant.astype(np.float32)*scales[...,None]);mean_error+=float(error.sum());total_count+=error.size
                output.seek(scale_offset+rows*(cols//GROUP)*4)
                stats.append(dict(name=name,parameters=rows*cols,mean_abs_quantization_error=mean_error/total_count))
            descriptors.append((kind,layer,offset,rows,cols))
            if kind in (0,12,13):print('EXPORTED',name,round(output.tell()/1024/1024,2),'MiB',flush=True)
        size=output.tell();words=[0]*64
        words[:15]=[0x31575154,1,HEADER_BYTES,size,config['hidden_size'],config['intermediate_size'],
            config['num_hidden_layers'],config['num_attention_heads'],config['num_key_value_heads'],config['vocab_size'],512,GROUP,
            struct.unpack('<I',struct.pack('<f',config['rms_norm_eps']))[0],
            struct.unpack('<I',struct.pack('<f',config['rope_theta']))[0],len(descriptors)]
        words[15]=bits
        assert 256+20*len(descriptors)<=HEADER_BYTES
        output.seek(0);output.write(struct.pack('<64I',*words))
        for d in descriptors:output.write(struct.pack('<5I',*d))
    digest=hashlib.sha256(dest.read_bytes()).hexdigest()
    manifest=dict(status='passed',architecture='Qwen2ForCausalLM',kind=f'all large matrices Q{bits} group64 with FP32 per-group scale and tied embedding',bits=bits,
        source_manifest=json.loads((MODELS/'source_manifest.json').read_text()),bytes=size,sha256=digest,
        matrix_parameters=sum(x['parameters'] for x in stats),matrix_quantization=stats,
        elapsed_seconds=time.monotonic()-started,config=config,tensors=[dict(kind=k,layer=l,offset=o,rows=r,cols=c) for k,l,o,r,c in descriptors])
    (OUT/'qwen_model_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUT/f'qwen_model_q{bits}_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'PASS_QWEN_ALL_MATRIX_Q{bits}_EXPORT',size,digest,flush=True)
if __name__=='__main__':main()
