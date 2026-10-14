import argparse
from datetime import datetime, timezone
import hashlib
import json
import msvcrt
from pathlib import Path
import re
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/ps_bringup'
TEMP = ROOT / 'tmp/ps'
INPUTS = ('software/ps_bringup/start.S','software/ps_bringup/main.c','software/ps_bringup/link.ld',
          'hardware/zynq_mini_revb/ps7_init.tcl','hardware/zynq_mini_revb/source.json',
          'scripts/ps_bringup.tcl','scripts/ps_debug_common.tcl',
          'scripts/ps_bringup_cleanup.tcl','scripts/run_ps_bringup.py')

def now(): return datetime.now(timezone.utc).isoformat()
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes(): return {s:sha(ROOT/s) for s in INPUTS}
def save(name, data):
    path=OUT/name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

class StageError(RuntimeError):
    def __init__(self,message,own_tree_stopped=True,process_id=None):
        super().__init__(message)
        self.own_tree_stopped=own_tree_stopped
        self.process_id=process_id

def command(args, name, marker=None, timeout=180, on_start=None):
    log=OUT/'logs'/name
    log.parent.mkdir(parents=True,exist_ok=True)
    print('Running '+name,flush=True)
    process=subprocess.Popen([str(a) for a in args],cwd=TEMP,stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
    try:
        if on_start: on_start(process.pid)
        text,_=process.communicate(timeout=timeout)
    except (Exception,KeyboardInterrupt) as error:
        partial=getattr(error,'output','') or ''
        if isinstance(partial,bytes): partial=partial.decode('utf-8',errors='replace')
        # Only this launcher's tree. Existing hw_server and GUI are not children.
        tree_stopped=False
        try:
            killed=subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],
                                  capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=10)
            kill_log=killed.stdout+killed.stderr
            tree_stopped=killed.returncode==0
        except (OSError,subprocess.TimeoutExpired,KeyboardInterrupt) as kill_error:
            kill_log=str(kill_error)
        try:
            remaining,_=process.communicate(timeout=10)
            text=remaining or partial
        except (subprocess.TimeoutExpired,KeyboardInterrupt):
            text=partial
            kill_log+='\nProcess tree termination could not be confirmed.'
            tree_stopped=False
        reason=('HOST_TIMEOUT' if isinstance(error,subprocess.TimeoutExpired) else
                'HOST_CANCELLED' if isinstance(error,KeyboardInterrupt) else 'HOST_PROCESS_ERROR')
        log.write_text(text+'\n'+reason+'\n'+kill_log,encoding='utf-8')
        raise StageError(reason+'; see '+str(log.relative_to(ROOT)),tree_stopped,process.pid) from error
    log.write_text(text,encoding='utf-8')
    if process.returncode or 'ERROR_PS_BRINGUP:' in text or (marker and marker not in text):
        raise StageError('Stage failed; see '+str(log.relative_to(ROOT)))
    return text

def cleanup(xsdb, attempt):
    try:
        command([xsdb,ROOT/'scripts/ps_bringup_cleanup.tcl'],attempt+'/cleanup.log',
                'PASS_PS_CLEANUP',timeout=30)
        return {'CPUState':'confirmed_stopped','OwnTreeStopped':True}
    except (Exception,KeyboardInterrupt) as error:
        return {'CPUState':'unknown','OwnTreeStopped':getattr(error,'own_tree_stopped',True),
                'OriginalProcessID':getattr(error,'process_id',None),'Error':str(error)}

def execute():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('build','probe','run','cleanup'))
    parser.add_argument('--gcc',type=Path)
    parser.add_argument('--xsdb',type=Path)
    args=parser.parse_args()
    TEMP.mkdir(parents=True,exist_ok=True)
    attempt=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
    record={'Status':'pending','StartedAtUTC':now(),'Action':args.action,'Attempt':attempt}
    hardware_started=False
    name=args.action+'.json'
    save(name,record)
    try:
        expected=hashes()
        source=json.loads((ROOT/'hardware/zynq_mini_revb/source.json').read_text(encoding='utf-8'))
        if source['Part']!='xc7z010clg400-1' or source['PSInitSHA256']!=expected['hardware/zynq_mini_revb/ps7_init.tcl']:
            raise RuntimeError('Board initialization provenance mismatch')
        if args.action=='cleanup':
            if args.xsdb is None: raise ValueError('--xsdb required')
            if (OUT/'failure_state.json').exists():
                previous=json.loads((OUT/'failure_state.json').read_text(encoding='utf-8'))
                if not previous.get('OwnTreeStopped',False):
                    raise StageError('Original execution tree exit remains unconfirmed; stopping CPUs alone cannot unlock another run')
            state=cleanup(args.xsdb,attempt)
            record['FailureCleanup']=state
            save('failure_state.json',{**state,'VerifiedAtUTC':now(),'Attempt':attempt})
            if state['CPUState']!='confirmed_stopped': raise StageError('Cleanup could not confirm CPU state')
        elif args.action=='build':
            if args.gcc is None: raise ValueError('--gcc required')
            version=command([args.gcc,'--version'],attempt+'/compiler_version.log').splitlines()[0]
            flags=['-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O2','-g','-ffreestanding',
                   '-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror','-nostdlib',
                   '-Wl,--build-id=none','-T',ROOT/'software/ps_bringup/link.ld',
                   '-Wl,-Map='+str(OUT/'ps_bringup.map')]
            command([args.gcc,*flags,ROOT/'software/ps_bringup/start.S',ROOT/'software/ps_bringup/main.c',
                     '-o',OUT/'ps_bringup.elf'],attempt+'/build.log')
            nm=args.gcc.with_name('arm-none-eabi-nm.exe')
            symbols=command([nm,'-n',OUT/'ps_bringup.elf'],attempt+'/symbols.log')
            def address(symbol):
                match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+re.escape(symbol)+r'$',symbols,re.M)
                if not match: raise RuntimeError('Missing ELF symbol '+symbol)
                return int(match[1],16)
            if address('_start')!=0x10000 or address('report')!=0x1e000:
                raise RuntimeError('Unexpected OCM layout')
            record.update(Compiler=version,DoneAddress=address('test_done'),ELFSHA256=sha(OUT/'ps_bringup.elf'))
        else:
            if args.xsdb is None: raise ValueError('--xsdb required')
            build=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
            if build['Status']!='passed' or build['SourceHashes']!=expected or build['ELFSHA256']!=sha(OUT/'ps_bringup.elf'):
                raise RuntimeError('Current successful build required')
            if args.action=='run' and (OUT/'failure_state.json').exists():
                previous=json.loads((OUT/'failure_state.json').read_text(encoding='utf-8'))
                if previous['CPUState']=='unknown' or not previous.get('OwnTreeStopped',False):
                    raise StageError('CPU state unknown after failure; explicit cleanup must pass before another run')
            snapshot=OUT/'attempts'/attempt/'ps_bringup.elf'
            snapshot.parent.mkdir(parents=True,exist_ok=False)
            shutil.copyfile(OUT/'ps_bringup.elf',snapshot)
            if sha(snapshot)!=build['ELFSHA256']: raise StageError('ELF snapshot hash mismatch')
            record['DownloadedELF']=str(snapshot.relative_to(ROOT)).replace('\\','/')
            marker='PASS_PS_PROBE' if args.action=='probe' else 'PASS_PS_DDR_RUN'
            hardware_started=args.action=='run'
            def pending_hardware(process_id=None):
                save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,
                     'OriginalProcessID':process_id,'Attempt':attempt,'Status':'pending',
                     'StartedAtUTC':record['StartedAtUTC']})
            if hardware_started: pending_hardware()
            text=command([args.xsdb,ROOT/'scripts/ps_bringup.tcl',args.action,snapshot],
                         attempt+'/'+args.action+'.log',marker,
                         on_start=pending_hardware if hardware_started else None)
            if sha(snapshot)!=build['ELFSHA256'] or sha(OUT/'ps_bringup.elf')!=build['ELFSHA256']:
                raise StageError('ELF changed during hardware stage')
            record['ELFSHA256']=build['ELFSHA256']
            if args.action=='run':
                match=re.search(r'^CPU_RESULT (.+)$',text,re.M)
                if not match: raise RuntimeError('Missing real CPU result')
                values=[int(v,0) for v in match[1].split()]
                keys=('Magic','State','Phase','WordsChecked','FailAddress','Expected','Actual','Checksum',
                      'TestBase','TestBytes','DDRControl','DDRStatus','CPUSCTLR')
                record['CPUResult']=dict(zip(keys,values))
                record['Scope']='4 MiB DDR patterns/address test, walking data bits and four sparse DDR-region probes; code/results/stack in OCM, no FPGA or persistent-storage programming'
                record['CPUStateAfterRun']='confirmed_stopped'
        if hashes()!=expected: raise RuntimeError('Inputs changed during stage')
        record.update(Status='passed',VerifiedAtUTC=now(),SourceHashes=expected)
        if hardware_started:
            save('failure_state.json',{'CPUState':'confirmed_stopped','OwnTreeStopped':True,
                 'Status':'completed','VerifiedAtUTC':now(),'Attempt':attempt})
        save(name,record)
        save('attempts/'+attempt+'/result.json',record)
        print('PASS_PS_'+args.action.upper(),flush=True)
    except (Exception,KeyboardInterrupt) as error:
        if hardware_started:
            if getattr(error,'own_tree_stopped',True):
                state=cleanup(args.xsdb,attempt)
            else:
                state={'CPUState':'unknown','OwnTreeStopped':False,
                       'OriginalProcessID':getattr(error,'process_id',None)}
            record['FailureCleanup']=state
            save('failure_state.json',{**state,'VerifiedAtUTC':now(),'Attempt':attempt})
        record.update(Status='failed',FailedAtUTC=now(),Error=str(error))
        save(name,record)
        save('attempts/'+attempt+'/result.json',record)
        print('PS stage stopped: '+str(error),flush=True)
        return 1
    return 0

def main():
    # Windows byte-range locks release automatically even if this driver dies.
    # Serialize build/probe/run/cleanup against another launch of this driver.
    TEMP.mkdir(parents=True,exist_ok=True)
    with (TEMP/'operation.lock').open('a+b') as lock:
        lock.seek(0,2)
        if lock.tell()==0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:
            print('Another PS action is active; no hardware action started.',flush=True)
            return 1
        try:
            return execute()
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)

if __name__=='__main__': raise SystemExit(main())
