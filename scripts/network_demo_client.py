"""Bounded, one-request-at-a-time LAN pilot; all subtitles here are simulated."""
import argparse,ipaddress,json,msvcrt,socket,struct,uuid
from pathlib import Path
import run_network_ui as app
import network_ui_debug as observer
import compare_subtitle_capture as pixels
from network_connect import check_ready

INPUTS=(*observer.INPUTS,'scripts/network_connect.py','scripts/network_demo_client.py',
        'scripts/compare_subtitle_capture.py','software/network_ui/protocol.h')
TRANSLATION='请确认每个人都能清楚地听到发言。'
SOURCE='Please make sure that everyone can hear the speaker clearly.'
SECOND='屏幕上的内容仅用于演示，并非实时翻译。'
LAST='这台原型在处理下一句话时，会保留上一句完整译文。'

class Peer:
    def __init__(self,connection,events=None,on_ack=None,session=1):
        self.connection=connection;self.request=0;self.epoch=0
        self.events=events if events is not None else [];self.on_ack=on_ack;self.session=session
    def receive(self,n):
        parts=[]
        while n:
            part=self.connection.recv(n)
            if not part:raise RuntimeError('TCP closed before the complete acknowledgement')
            parts.append(part);n-=len(part)
        return b''.join(parts)
    def send(self,type,segment=0,revision=0,source_revision=0,text='',code=0,epoch=None,request=None):
        body=text.encode('utf-8')
        if len(body)>768 or len(text)>255:raise ValueError('Pilot text is too long')
        if request is None:self.request+=1;request=self.request
        frame=struct.pack('<9I',0x31425553,1,request,type,self.epoch if epoch is None else epoch,
                          segment,revision,source_revision,len(body))+body
        self.connection.sendall(frame)
        ack=struct.unpack('<8I',self.receive(32))
        if ack[:4]!=(0x314b4341,1,request,type) or ack[4]!=code or not ack[5] or ack[6]>4 or ack[6]>ack[7]:
            raise RuntimeError('Unexpected or mismatched pilot acknowledgement: '+str(ack))
        expected_epoch=self.epoch+(1 if type==1 and segment in (2,3) and code==0 else 0)
        if not(type==0 and code==0) and ack[5]!=expected_epoch:
            raise RuntimeError('Acknowledgement belongs to an unexpected epoch')
        self.epoch=ack[5]
        self.events.append({'Session':self.session,'Request':request,'Type':type,'ExpectedCode':code,'ACK':list(ack),
                            'Payload':text,'Bytes':len(frame)})
        if self.on_ack:self.on_ack()
        return ack
    def final(self,segment,source,translated):
        self.send(2,segment,1,0,source[:max(1,len(source)//2)])
        self.send(3,segment,2,0,source)
        self.send(4,segment,1,2)
        return self.send(5,segment,1,2,translated)

def model_text(snapshot,offset):
    values=struct.unpack_from('<256I',snapshot,32+offset)
    return ''.join(chr(v) for v in values[:values.index(0)])
def model_history(snapshot):
    count=struct.unpack_from('<I',snapshot,32+10324)[0]
    if count>4:raise RuntimeError('Invalid captured history count')
    items=[]
    for i in range(count):
        offset=2068+i*2064
        epoch,segment,revision,source_revision=struct.unpack_from('<4I',snapshot,32+offset)
        items.append(dict(Epoch=epoch,Segment=segment,Revision=revision,SourceRevision=source_revision,
                          Source=model_text(snapshot,offset+16),Translated=model_text(snapshot,offset+1040)))
    return items
def history_entry(epoch,segment,source,translated):
    return dict(Epoch=epoch,Segment=segment,Revision=1,SourceRevision=2,Source=source,Translated=translated)
def capture(a,record,label,source,translated,history):
    args=argparse.Namespace(action='capture',xsdb=a.xsdb,vivado=a.vivado,command=None,expected_epoch=None)
    if observer.execute(args):raise RuntimeError('Board capture failed')
    r=json.loads((app.OUT/'latest_capture.json').read_text(encoding='utf-8'))
    folder=(app.ROOT/r['ResultFile']).parent;snapshot=(folder/'snapshot.bin').read_bytes()
    if model_text(snapshot,20)!=source or model_text(snapshot,1044)!=translated:
        raise RuntimeError('Actual immutable board model differs from the sent network text')
    if model_history(snapshot)!=history or struct.unpack_from('<I',snapshot,32+10328)[0]!=len(history):
        raise RuntimeError('Actual retained history content differs from accepted network finals')
    if pixels.compare(folder,a.gcc_directory,app.OUT,app.ROOT/'output/subtitle_ui/host_validation.json',app.OUT/'build-app.json'):
        raise RuntimeError('Actual network frame did not match the exact native rendering')
    record.setdefault('Captures',[]).append({'Label':label,'Evidence':r['ResultFile'],
        'Comparison':str((folder/'pixel_comparison.json').relative_to(app.ROOT)).replace('\\','/'),
        'Source':source,'Translated':translated,'History':model_history(snapshot),
        'SnapshotSHA256':r['SnapshotSHA256'],'PixelSHA256':r['PixelSHA256']})
    return snapshot
def sample(a):
    args=argparse.Namespace(action='sample',xsdb=a.xsdb,vivado=a.vivado,command=None,expected_epoch=None)
    if observer.execute(args):raise RuntimeError('Public network observation failed')
    return json.loads((app.OUT/'latest_sample.json').read_text(encoding='utf-8'))
def open_peer(address,events=None,on_ack=None,session=1):
    connection=socket.create_connection((address,8266),timeout=8);connection.settimeout(8)
    return connection,Peer(connection,events,on_ack,session)

def main():
    p=argparse.ArgumentParser();p.add_argument('--xsdb',type=Path,required=True);p.add_argument('--vivado',type=Path)
    p.add_argument('--gcc-directory',type=Path,required=True);a=p.parse_args()
    with (app.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        attempt=app.net.fb.now().replace(':','').replace('.','_')+'_'+uuid.uuid4().hex[:6]
        folder=app.OUT/'pilot'/attempt;folder.mkdir(parents=True,exist_ok=False)
        record={'Status':'pending','Attempt':attempt,'SourceHashes':app.net.fb.hashes(INPUTS),
                'StartedAtUTC':app.net.fb.now(),'Scope':'Known simulated LAN text/ACK/model/pixels; no audio, ASR, MT or touch'}
        connection=None;events=[];history=[];record['Requests']=events;record['ExpectedHistory']=history
        def progress():
            app.save('pilot.json',record)
            (folder/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
        progress()
        try:
            observer.prepare();before=sample(a);address=check_ready(before['NetworkResult'])
            association=json.loads((app.OUT/'connection.json').read_text(encoding='utf-8'))
            if association['Status']!='passed' or association['StationAddress']!=address or \
               association['RunAttempt']!=before['RunAttempt'] or association['ApplicationELFSHA256']!=before['ApplicationELFSHA256']:
                raise RuntimeError('Current application association evidence required')
            record.update(StationAddress=address,RunAttempt=before['RunAttempt'],ProgramAttempt=before['ProgramAttempt'],
                          ApplicationELFSHA256=before['ApplicationELFSHA256'],BitstreamSHA256=before['BitstreamSHA256'],
                          AssociationSHA256=app.net.fb.sha(app.OUT/'connection.json'))
            connection,peer=open_peer(address,events,progress);hello=peer.send(0)
            if hello[7]!=0:raise RuntimeError('Fresh network session with no prior finals required')
            final=peer.final(1,SOURCE,TRANSLATION)
            if final[6:8]!=(1,1):raise RuntimeError('First network final did not enter history once')
            history.append(history_entry(peer.epoch,1,SOURCE,TRANSLATION))
            initial=capture(a,record,'first_network_final',SOURCE,TRANSLATION,history)
            peer.send(3,2,1,0,'\U0001f600',code=4)
            peer.send(3,2,1,0,'Network test',code=5,epoch=peer.epoch-1)
            peer.send(0,code=7,request=peer.request)
            peer.send(5,1,2,2,TRANSLATION,code=6)
            rejected=capture(a,record,'rejected_input_preserves_final',SOURCE,TRANSLATION,history)
            # Only rejected_events may change in the model after the one model-level rejection.
            old=initial[32:32+11392];new=rejected[32:32+11392]
            rejected_offset=20+2*256*4+4*(4*4+2*256*4)+2*4
            if old[:rejected_offset]!=new[:rejected_offset] or old[rejected_offset+4:]!=new[rejected_offset+4:]:
                raise RuntimeError('Rejected requests changed visible model state')
            peer.send(1,1);peer.send(3,2,1,0,'Network test',code=6)
            old_epoch=peer.epoch;peer.send(1,2)
            if peer.epoch!=old_epoch+1:raise RuntimeError('Resume did not change epoch')
            nonce=uuid.uuid4().hex[:12];source='Network test '+nonce;record['UniqueSource']=source
            final=peer.final(2,source,SECOND)
            if final[6:8]!=(2,2):raise RuntimeError('Second network final history mismatch')
            history.append(history_entry(peer.epoch,2,source,SECOND))
            peer.send(1,5);capture(a,record,'unique_network_text_and_font',source,SECOND,history)
            connection.close();connection=None
            disconnected=sample(a)
            if disconnected['NetworkResult']['Phase']!=3 or disconnected['NetworkResult']['Client']!=0xffffffff:
                raise RuntimeError('Normal disconnect did not release the client')
            record['DisconnectedEvidence']=disconnected['ResultFile']
            connection,peer=open_peer(address,events,progress,2);hello=peer.send(0)
            if hello[6:8]!=(2,2):raise RuntimeError('Disconnect/reconnect did not retain finals')
            peer.send(1,2);final=peer.final(1,'Network reconnect '+nonce,LAST)
            if final[6:8]!=(3,3):raise RuntimeError('Reconnected peer did not make exactly one new final')
            history.append(history_entry(peer.epoch,1,'Network reconnect '+nonce,LAST))
            capture(a,record,'reconnected_network_final','Network reconnect '+nonce,LAST,history)
            connection.close();connection=None
            after=sample(a);public=after['NetworkResult']
            if public['Phase']!=3 or public['Error'] or public['Overflow'] or public['Framing'] or public['Rejected']!=5:
                raise RuntimeError('Final autonomous network counters did not pass')
            if app.net.fb.hashes(INPUTS)!=record['SourceHashes']:raise RuntimeError('Pilot helpers changed')
            record.update(Status='passed',VerifiedAtUTC=app.net.fb.now(),Requests=events,Network=public,
                          FinalEvidence=after['ResultFile'],FinalCount=3,PixelCaptures=len(record['Captures']))
            print('PASS_NETWORK_SIMULATED_SUBTITLES finals=3 captures='+str(len(record['Captures'])),flush=True);result=0
        except (Exception,KeyboardInterrupt) as e:
            record.update(Status='failed',FailedAtUTC=app.net.fb.now(),Error=str(e),Requests=events);result=1
            print('LAN subtitle pilot stopped: '+str(e),flush=True)
        finally:
            if connection is not None:connection.close()
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
        app.save('pilot.json',record)
        (folder/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
        return result
if __name__=='__main__':raise SystemExit(main())
