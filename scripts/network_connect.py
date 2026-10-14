"""Runtime-only credentials: stdin -> private XSDB pipe -> volatile ARM mailbox."""
import argparse,ipaddress,json,msvcrt,re,struct,subprocess,sys,time,uuid
from pathlib import Path
import run_network_ui as app
import network_ui_debug as observer
INPUTS=(*app.HW_INPUTS,'scripts/network_connect.py','scripts/ps_network_config.tcl')
def read_credentials_line():
    if not sys.stdin.isatty():return sys.stdin.readline()
    # Windows interactive tool sessions require console input. getwch consumes
    # each character without echoing it; no credential character is printed.
    print('PRIVATE_RUNTIME_INPUT_READY',flush=True)
    chars=[]
    while len(chars)<4096:
        char=msvcrt.getwch()
        if char in ('\r','\n'):return ''.join(chars)
        if char=='\x03':raise KeyboardInterrupt
        if char=='\b':
            if chars:chars.pop()
        elif ord(char)<32:raise ValueError('Unsupported console input')
        else:chars.append(char)
    raise ValueError('Private input is too long')
def payload(credentials,peer_ip=0,port=0):
    ssid=credentials['ssid'].encode('ascii');password=credentials['password'].encode('ascii')
    if not 1<=len(ssid)<=32 or not 8<=len(password)<=64 or any(c<32 or c>126 for c in ssid+password):
        raise ValueError('Unsupported Wi-Fi credential format')
    raw=bytearray(128);struct.pack_into('<5I',raw,0,0x3146434e,0,1,len(ssid),len(password))
    raw[20:20+len(ssid)]=ssid;raw[53:53+len(password)]=password
    struct.pack_into('<2I',raw,120,port,peer_ip)
    return ' '.join(f'{w:08x}' for w in struct.unpack('<32I',raw))+'\n'
def private_command(xsdb,data,folder):
    app.net.fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,'Status':'pending_private_config'})
    process=None;tree_stopped=True;communication_finished=False
    try:
        process=subprocess.Popen([str(xsdb),str(app.ROOT/'scripts/ps_network_config.tcl')],
            cwd=app.ROOT/'tmp/network_ui',stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8',errors='replace')
        app.net.fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,
            'Status':'pending_private_config','OriginalProcessID':process.pid})
        text,_=process.communicate(input=data,timeout=60)
        communication_finished=True
        # Keep only typed public fields, never arbitrary tool output or error details.
        matches=re.findall(r'^NETWORK_CONFIG_RESULT ([0-9a-fA-FxX ]+)$',text,re.M)
        if process.returncode or len(matches)!=1 or 'PASS_PRIVATE_NETWORK_CONFIG' not in text:
            stages=re.findall(r'^PRIVATE_CONFIG_FAILURE_STAGE (input|connection|cpu|public_status|content_write|commit|consumption)$',text,re.M)
            cpu_states=re.findall(r'^PRIVATE_CONFIG_FAILURE_CPU (running|stopped|unavailable)$',text,re.M)
            reasons=re.findall(r'^PRIVATE_CONFIG_FAILURE_REASON (unavailable|incoherent_public|wrong_application|no_ack)$',text,re.M)
            (folder/'public_failure.json').write_text(json.dumps({'Stage':stages[0] if len(stages)==1 else 'unavailable',
                  'CPUState':cpu_states[0] if len(cpu_states)==1 else 'unavailable',
                  'Reason':reasons[0] if len(reasons)==1 else 'unavailable',
                  'ExitCode':process.returncode,'Scope':'Typed stage only; private tool output discarded'},indent=2),encoding='utf-8')
            raise RuntimeError('Private configuration has no verified acknowledgement')
        values=[int(v,0) for v in matches[0].split()]
        if len(values)!=16 or values[0]!=0x3154454e or values[1]&1:
            raise RuntimeError('Invalid public configuration result')
        (folder/'public_config.log').write_text('NETWORK_CONFIG_RESULT '+matches[0]+'\n',encoding='utf-8')
        app.net.fb.save('failure_state.json',{'CPUState':'running_animation','OwnTreeStopped':True,'Status':'private_config_acknowledged'})
        return values
    except (Exception,KeyboardInterrupt) as e:
        if process is not None and not communication_finished:
            tree_stopped=False
            try:
                killed=subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,timeout=10)
                process.communicate(timeout=10);tree_stopped=killed.returncode==0
            except (Exception,KeyboardInterrupt):tree_stopped=False
        app.net.fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':tree_stopped,
            'OriginalProcessID':process.pid if process else None,'Status':'private_config_failed'})
        if tree_stopped:
            state=app.net.runner.cleanup(xsdb,'private_config_failed_'+folder.name)
            app.net.fb.save('failure_state.json',{**state,'Status':'private_config_failed_cleanup'})
        raise RuntimeError('Private configuration stopped; inspect public state and safety gate') from None
def check_ready(public):
    if public['Phase']!=3 or public['Error'] or public['EchoVerified']!=1 or public['Overflow'] or public['Framing'] or public['Client']!=0xffffffff:
        raise RuntimeError('Autonomous Wi-Fi/TCP server has not passed')
    address=ipaddress.IPv4Address(public['StationIP'])
    if address not in ipaddress.IPv4Network('192.168.31.0/24') or address.packed[-1] in (0,255):
        raise RuntimeError('Station address is outside the user-confirmed LAN')
    return str(address)
def main():
    p=argparse.ArgumentParser();p.add_argument('--credentials-stdin',action='store_true',required=True)
    p.add_argument('--xsdb',required=True,type=Path);p.add_argument('--vivado',type=Path);a=p.parse_args()
    # Validate private input before any external command; no files, argv or logs.
    try:
        data=payload(json.loads(read_credentials_line()))
    except (Exception,KeyboardInterrupt):
        print('Wi-Fi input rejected; no device action.',flush=True);return 1
    with (app.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        attempt=app.net.fb.now().replace(':','').replace('.','_')+'_'+uuid.uuid4().hex[:6]
        folder=app.OUT/'connection'/attempt;folder.mkdir(parents=True,exist_ok=False)
        record={'Status':'pending','Attempt':attempt,'StartedAtUTC':app.net.fb.now(),
                'SourceHashes':app.net.fb.hashes(INPUTS),'Scope':'Volatile PS-owned association/TCP server; no inference or audio'}
        try:
            observer.prepare()
            sample=argparse.Namespace(action='sample',xsdb=a.xsdb,vivado=a.vivado,command=None,expected_epoch=None)
            if observer.execute(sample):raise RuntimeError('Live application precheck failed')
            before=json.loads((app.OUT/'latest_sample.json').read_text(encoding='utf-8'))
            if before['NetworkResult']['Phase']!=0 or before['PSWiFiResult'][2:7]!=[2,0,1,1,1]:
                raise RuntimeError('Fresh application and independent AT probe required before configuration')
            record.update(ApplicationELFSHA256=before['ApplicationELFSHA256'],BitstreamSHA256=before['BitstreamSHA256'],
                          RunAttempt=before['RunAttempt'],ProgramAttempt=before['ProgramAttempt'],BeforeEvidence=before['ResultFile'])
            app.net.fb.hardware_gate();app.checked_app();app.checked_platform();app.require_owner()
            record['ConfigPublic']=private_command(a.xsdb,data,folder);data=None
            deadline=time.monotonic()+50;samples=[]
            while time.monotonic()<deadline:
                if observer.execute(sample):raise RuntimeError('Public association observation failed')
                current=json.loads((app.OUT/'latest_sample.json').read_text(encoding='utf-8'))
                public=current['NetworkResult'];samples.append(current['ResultFile'])
                if public['Phase']==6:raise RuntimeError('Network setup reported fault '+str(public['Error']))
                if public['Phase']==3:
                    record['StationAddress']=check_ready(public);break
            else:raise RuntimeError('Autonomous association/server deadline expired')
            if app.net.fb.hashes(INPUTS)!=record['SourceHashes']:raise RuntimeError('Connection helpers changed')
            record.update(Status='passed',VerifiedAtUTC=app.net.fb.now(),Samples=samples,Network=public,
                          Evidence=current['ResultFile'],PrivateStorage='stdin/RAM only, consumed mailbox wiped')
            print('PASS_AUTONOMOUS_WIFI_SERVER '+record['StationAddress'],flush=True);result=0
        except (Exception,KeyboardInterrupt) as e:
            # Only public errors are constructed here; private subprocess output is discarded.
            record.update(Status='failed',FailedAtUTC=app.net.fb.now(),Error=str(e));result=1
            print('Wi-Fi connection stopped: '+str(e),flush=True)
        finally:
            data=None;lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
        app.save('connection.json',record)
        (folder/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
        return result
if __name__=='__main__':raise SystemExit(main())
