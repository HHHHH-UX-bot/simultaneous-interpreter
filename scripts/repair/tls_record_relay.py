"""Forward opaque cloud TLS and record block fingerprints, never payloads.

Diagnostic use only: the board still authenticates the real cloud hostname.
Stop the board session before stopping this relay.
"""
import argparse
import asyncio
import json
import time
from pathlib import Path

HOST='ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com'

class RecordHeaders:
    """Only TLS 1.2 record framing and the explicit, public GCM nonce."""
    def __init__(self,report):
        self.buffer=bytearray();self.offset=0;self.encrypted=False;self.report=report;self.disabled=False
        report.update(tls_records=0,nonce_records=0,last_nonce=None,nonce_discontinuities=[])
    def feed(self,data):
        if self.disabled:return
        self.buffer.extend(data)
        while len(self.buffer)>=5:
            kind=self.buffer[0];size=int.from_bytes(self.buffer[3:5],'big')
            if kind not in (20,21,22,23) or size>18432:
                self.report['framing_error_offset']=self.offset;self.disabled=True;self.buffer.clear();return
            if len(self.buffer)<5+size:break
            self.report['tls_records']+=1
            if kind==20:self.encrypted=True
            elif self.encrypted and size>=24:
                nonce=int.from_bytes(self.buffer[5:13],'big');previous=self.report['last_nonce']
                if previous is not None and nonce!=previous+1:
                    self.report['nonce_discontinuities'].append({'offset':self.offset,'previous':previous,'actual':nonce})
                self.report['last_nonce']=nonce;self.report['nonce_records']+=1
            del self.buffer[:5+size];self.offset+=5+size
        self.report['partial_record_bytes']=len(self.buffer)

async def serve(bind,port,output,seconds):
    sessions=0
    async def connection(reader,writer):
        nonlocal sessions
        sessions+=1;number=sessions
        report={'session':number,'started':time.time(),'uplink_bytes':0,'downlink_bytes':0,
                'blocks':[],'partial_fnv':2166136261,'status':'connected'}
        path=output/('connection_%02d.json'%number)
        records=RecordHeaders(report)
        def save():
            temp=path.with_suffix('.tmp');temp.write_text(json.dumps(report,indent=2)+'\n');temp.replace(path)
        upstream=None
        try:
            remote,upstream=await asyncio.open_connection(HOST,443)
            save();print('RELAY_CONNECTED',number,flush=True)
            async def forward(source,destination,uplink):
                while data:=await source.read(8192):
                    if uplink:
                        records.feed(data)
                        for byte in data:
                            report['partial_fnv']=((report['partial_fnv']^byte)*16777619)&0xffffffff
                            report['uplink_bytes']+=1
                            if report['uplink_bytes']%256==0:
                                report['blocks'].append([report['uplink_bytes']//256,report['partial_fnv']])
                                report['partial_fnv']=2166136261
                    else:report['downlink_bytes']+=len(data)
                    destination.write(data);await destination.drain();save()
            jobs=[asyncio.create_task(forward(reader,upstream,True)),asyncio.create_task(forward(remote,writer,False))]
            done,pending=await asyncio.wait(jobs,return_when=asyncio.FIRST_COMPLETED)
            for job in pending:job.cancel()
            await asyncio.gather(*pending,return_exceptions=True)
            for job in done:job.result()
            report['status']='closed'
        except (OSError,asyncio.TimeoutError,ValueError) as error:
            report['status']=type(error).__name__+': '+str(error)
        finally:
            if upstream:upstream.close()
            writer.close();report['ended']=time.time();save()
            print('RELAY_CLOSED',number,report['uplink_bytes'],report['status'],flush=True)
    server=await asyncio.start_server(connection,bind,port)
    print('RELAY_LISTENING',bind,port,flush=True)
    async with server:
        try:await asyncio.wait_for(server.serve_forever(),seconds)
        except asyncio.TimeoutError:pass

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bind',required=True,help='LAN IPv4 of this computer')
    p.add_argument('--port',type=int,default=18443)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=900)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    asyncio.run(serve(a.bind,a.port,a.output,a.seconds))
if __name__=='__main__':main()
