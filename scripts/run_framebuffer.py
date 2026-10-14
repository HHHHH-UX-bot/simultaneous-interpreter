"""Stage gates for the PS/DDR display; temporary JTAG configuration only."""
import argparse
from datetime import datetime,timezone
import hashlib,json
import msvcrt
from pathlib import Path
import re,sys,uuid
import shutil
import prepare_framebuffer
import run_ps_bringup as runner

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/framebuffer'
TEMP=ROOT/'tmp/framebuffer'
PS_FAILURE_STATE=ROOT/'output/ps_bringup/failure_state.json'
PL_INPUTS=('rtl/framebuffer_video.v','rtl/framebuffer_top.v','rtl/hdmi_clock.v',
           'rtl/async_rst_sync.v','rtl/dvi_encoder.v','rtl/dvi_transmitter_top.v','rtl/serializer_10_to_1.v',
           'constraints/hdmi.xdc','constraints/wifi_uart.xdc','hardware/zynq_mini_revb/source.json',
           'scripts/prepare_framebuffer.py','scripts/build_framebuffer.tcl')
APP_INPUTS=('software/framebuffer/main.c','software/ps_bringup/start.S','software/ps_bringup/link.ld',
            'output/framebuffer/framebuffer_hw.h','output/framebuffer/configuration_source.json')
HARDWARE_INPUTS=('scripts/hardware_framebuffer.tcl','scripts/ps_framebuffer.tcl',
                 'scripts/ps_quiesce_framebuffer.tcl','scripts/run_framebuffer.py',
                 'scripts/ps_debug_common.tcl','scripts/ps_bringup_cleanup.tcl','scripts/run_ps_bringup.py',
                 'scripts/probe_framebuffer.tcl')
DEFAULT_VIVADO=Path('D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat')

def now(): return datetime.now(timezone.utc).isoformat()
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes(files): return {p:sha(ROOT/p) for p in files}
def save(name,data):
    path=OUT/name; path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False),encoding='utf-8')

def checked_record(name,inputs):
    record=json.loads((OUT/name).read_text(encoding='utf-8'))
    if record['Status']!='passed' or record['SourceHashes']!=hashes(inputs):
        raise RuntimeError('Current successful '+name+' required')
    return record

def checked_build():
    build=checked_record('build.json',PL_INPUTS)
    for path,key in [('framebuffer_top.bit','BitstreamSHA256'),('ps_display.xsa','XSASHA256'),('ps7_init.tcl','PSInitSHA256')]:
        if sha(OUT/path)!=build[key]: raise RuntimeError('Hardware output changed: '+path)
    return build

def hardware_gate():
    for path in (OUT/'failure_state.json',PS_FAILURE_STATE):
        if path.exists():
            state=json.loads(path.read_text(encoding='utf-8'))
            if state['CPUState']=='unknown' or not state.get('OwnTreeStopped',False):
                raise RuntimeError('Unconfirmed hardware execution remains: '+str(path.relative_to(ROOT)))
    probe_path=OUT/'probe_operation.json'
    if probe_path.exists() and not json.loads(probe_path.read_text(encoding='utf-8')).get('OwnTreeStopped',False):
        raise RuntimeError('Unconfirmed live configuration probe process remains')

def vivado_for(args):
    # Keep existing run/sample --xsdb invocations compatible on this verified host.
    if args.vivado is not None: return args.vivado
    if DEFAULT_VIVADO.is_file(): return DEFAULT_VIVADO
    located=shutil.which('vivado.bat') or shutil.which('vivado')
    if located: return Path(located)
    raise ValueError('--vivado required for live configuration verification')

def fpga_result(text,marker):
    matches=re.findall(r'^'+re.escape(marker)+r' (.+)$',text,re.M)
    if len(matches)!=1: raise RuntimeError('Missing or ambiguous live FPGA status')
    values=[int(value,0) for value in matches[0].split()]
    if (len(values)!=4 or any(value not in (0,1) for value in values[:3])
            or not 0<=values[3]<=0xFFFFFFFF):
        raise RuntimeError('Invalid live FPGA status')
    return dict(zip(('DONE','EOS','CRCError','USRAccess'),values))

def require_framebuffer(live):
    if live!={'DONE':1,'EOS':1,'CRCError':0,'USRAccess':0x46564231}:
        raise RuntimeError('Live PL is not the configured framebuffer; program a verified image before PS/PL access')

def probe_live(vivado,attempt):
    def pending(pid=None):
        save('probe_operation.json',{'Status':'pending','Attempt':attempt,'OwnTreeStopped':False,
             'OriginalProcessID':pid,'StartedAtUTC':now(),
             'Scope':'Read-only JTAG probe; no CPU state is inferred'})
    pending()
    try:
        text=runner.command([vivado,'-mode','batch','-nojournal','-log',TEMP/'probe.log',
                             '-source',ROOT/'scripts/probe_framebuffer.tcl'],
                            attempt+'/probe.log','PASS_FRAMEBUFFER_PROBE',timeout=60,on_start=pending)
        live=fpga_result(text,'FPGA_PROBE_RESULT')
    except (Exception,KeyboardInterrupt) as error:
        save('probe_operation.json',{'Status':'failed','Attempt':attempt,
             'OwnTreeStopped':getattr(error,'own_tree_stopped',True),
             'OriginalProcessID':getattr(error,'process_id',None),'Error':str(error),'VerifiedAtUTC':now()})
        raise
    save('probe_operation.json',{'Status':'completed','Attempt':attempt,'OwnTreeStopped':True,
         'LiveFPGA':live,'VerifiedAtUTC':now(),'Scope':'Probe process exited; CPU state remains unverified by this probe'})
    return live

def display_result(text):
    match=re.search(r'^DISPLAY_RESULT (.+)$',text,re.M)
    if not match: raise RuntimeError('Missing real display report')
    values=[int(v,0) for v in match[1].split()]
    keys=('Magic','State','Sequence','Phase','Flips','CurrentBuffer','GoodFrames','Underflows',
          'FormatErrors','VDMAStatus','LastFramePixels','PixelsLow','PixelsHigh','RawFrames','CPUSCTLR','ErrorCode')
    if len(values)!=len(keys): raise RuntimeError('Unexpected report size')
    return dict(zip(keys,values))

def execute(args):
    attempt=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
    record={'Status':'pending','Action':args.action,'StartedAtUTC':now(),'Attempt':attempt}
    name=args.action+'.json'; save(name,record)
    runner.OUT=OUT; runner.TEMP=TEMP
    hardware_started=False
    probe_started=False
    try:
        if args.action=='build':
            if args.vivado is None: raise ValueError('--vivado required')
            checked_record('simulation.json',('rtl/framebuffer_video.v','sim/tb_framebuffer_video.v'))
            checked_record('reset_simulation.json',('scripts/build_framebuffer.tcl','rtl/framebuffer_video.v',
                'sim/tb_framebuffer_reset.v','scripts/simulate_framebuffer_reset.py','scripts/run_ps_bringup.py'))
            prepare_framebuffer.main()
            expected=hashes(PL_INPUTS)
            runner.command([args.vivado,'-mode','batch','-nojournal','-log',TEMP/'implementation.log',
                            '-source',ROOT/'scripts/build_framebuffer.tcl','-tclargs','build'],
                           attempt+'/build.log','PASS_FRAMEBUFFER_BUILD',timeout=1800)
            if hashes(PL_INPUTS)!=expected: raise RuntimeError('PL inputs changed during implementation')
            implementation=json.loads((OUT/'implementation.json').read_text(encoding='utf-8'))
            record.update(SourceHashes=expected,Implementation=implementation,
                          BitstreamSHA256=sha(OUT/'framebuffer_top.bit'),XSASHA256=sha(OUT/'ps_display.xsa'),
                          PSInitSHA256=sha(OUT/'ps7_init.tcl'),XSAIncludesBitstream=False)
        elif args.action=='build-app':
            if args.gcc is None: raise ValueError('--gcc required')
            expected=hashes(APP_INPUTS)
            flags=['-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O2','-g','-ffreestanding',
                   '-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror','-nostdlib',
                   '-Wl,--build-id=none','-T',ROOT/'software/ps_bringup/link.ld','-I',OUT,
                   '-Wl,-Map='+str(OUT/'framebuffer_app.map')]
            runner.command([args.gcc,*flags,ROOT/'software/ps_bringup/start.S',ROOT/'software/framebuffer/main.c',
                            '-o',OUT/'framebuffer_app.elf'],attempt+'/app_build.log')
            symbols=runner.command([args.gcc.with_name('arm-none-eabi-nm.exe'),'-n',OUT/'framebuffer_app.elf'],attempt+'/app_symbols.log')
            if not re.search(r'^00010000\s+\w\s+_start$',symbols,re.M) or not re.search(r'^0001e000\s+\w\s+report$',symbols,re.M):
                raise RuntimeError('Unexpected OCM layout')
            if hashes(APP_INPUTS)!=expected: raise RuntimeError('Application inputs changed')
            record.update(SourceHashes=expected,ELFSHA256=sha(OUT/'framebuffer_app.elf'),
                          Scope='CPU-drawn test pattern with two DDR buffers; no fonts, audio or touch')
        elif args.action=='cleanup':
            if args.xsdb is None: raise ValueError('--xsdb required')
            previous=json.loads((OUT/'failure_state.json').read_text(encoding='utf-8'))
            if not previous.get('OwnTreeStopped',False):
                raise RuntimeError('Original execution tree remains unconfirmed; CPU halt alone cannot unlock the stage')
            state=runner.cleanup(args.xsdb,attempt+'/explicit_cleanup')
            record.update(FailureCleanup=state,SourceHashes=hashes(HARDWARE_INPUTS))
            save('failure_state.json',{**state,'Attempt':attempt,'VerifiedAtUTC':now()})
            if state['CPUState']!='confirmed_stopped' or not state['OwnTreeStopped']:
                raise runner.StageError('CPU cleanup remains unconfirmed',state['OwnTreeStopped'],state.get('OriginalProcessID'))
        elif args.action=='probe':
            hardware_gate()
            vivado=vivado_for(args)
            expected=hashes(HARDWARE_INPUTS)
            probe_started=True
            record.update(SourceHashes=expected,LiveFPGA=probe_live(vivado,attempt),
                          VivadoPath=str(vivado),Scope='Read-only configuration registers; CPU stop is not inferred')
            if hashes(HARDWARE_INPUTS)!=expected: raise RuntimeError('Hardware scripts changed during probe')
        elif args.action in ('program','run','sample'):
            if args.xsdb is None: raise ValueError('--xsdb required')
            vivado=vivado_for(args)
            build=checked_build()
            app=checked_record('build-app.json',APP_INPUTS)
            if sha(OUT/'framebuffer_app.elf')!=app['ELFSHA256']: raise RuntimeError('ELF changed')
            expected=hashes(HARDWARE_INPUTS)
            record.update(SourceHashes=expected,BitstreamSHA256=build['BitstreamSHA256'],
                          ELFSHA256=app['ELFSHA256'],PSInitSHA256=build['PSInitSHA256'])
            folder=OUT/'attempts'/attempt
            folder.mkdir(parents=True,exist_ok=False)
            copied={}
            expected_outputs={'framebuffer_top.bit':build['BitstreamSHA256'],
                              'framebuffer_app.elf':app['ELFSHA256'],'ps7_init.tcl':build['PSInitSHA256']}
            for filename in ('framebuffer_top.bit','framebuffer_app.elf','ps7_init.tcl'):
                if sha(OUT/filename)!=expected_outputs[filename]: raise RuntimeError('Output changed after validation: '+filename)
                target=folder/filename
                shutil.copyfile(OUT/filename,target)
                copied[filename]=sha(target)
                if copied[filename]!=expected_outputs[filename] or sha(OUT/filename)!=expected_outputs[filename]:
                    raise RuntimeError('Snapshot mismatch')
            record['SnapshotHashes']=copied
            if args.action=='sample':
                active=json.loads((OUT/'active_app.json').read_text(encoding='utf-8'))
                if active['Status']!='ready': raise RuntimeError('Application start is not current')
                prior=json.loads((OUT/'attempts'/active['RunAttempt']/'result.json').read_text(encoding='utf-8'))
                if prior['Status']!='passed' or prior['BitstreamSHA256']!=build['BitstreamSHA256'] or prior['ELFSHA256']!=app['ELFSHA256']:
                    raise RuntimeError('Current successful application run required')
            if args.action in ('run','sample'):
                programmed=json.loads((OUT/'program.json').read_text(encoding='utf-8'))
                if programmed['Status']!='passed' or programmed['BitstreamSHA256']!=build['BitstreamSHA256']:
                    raise RuntimeError('Current successful FPGA programming required')
                if args.action=='run':
                    consumed_path=OUT/'program_consumption.json'
                    if consumed_path.exists():
                        consumed=json.loads(consumed_path.read_text(encoding='utf-8'))
                        if consumed['ProgramAttempt']==programmed['Attempt']:
                            raise RuntimeError('This FPGA program attempt was already consumed; program fresh PL before PS initialization')
                    hardware_gate()
                    record['ProgramAttempt']=programmed['Attempt']
            # Historical metadata cannot establish that PL survived a power cycle.
            # This live probe touches only JTAG configuration registers, before
            # any CPU halt, initialization, DMA control or display MMIO access.
            hardware_gate()
            probe_started=True
            live=probe_live(vivado,attempt)
            record.update(LiveFPGABeforeAction=live,VivadoPath=str(vivado))
            if args.action=='program':
                if live['CRCError']!=0: raise RuntimeError('Live FPGA CRC error; diagnose before programming')
                if live['DONE']==1: require_framebuffer(live)
            else:
                require_framebuffer(live)
            if args.action=='run':
                save('program_consumption.json',{'ProgramAttempt':programmed['Attempt'],
                     'RunAttempt':attempt,'Status':'consumed_before_launch','RecordedAtUTC':now()})
            if args.action in ('program','run'):
                hardware_gate()
                hardware_started=True
                save('active_app.json',{'Status':'invalidated_before_hardware_action','Attempt':attempt})
            def pending_hardware(pid=None):
                save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':pid,
                     'Status':'pending','Attempt':attempt,'StartedAtUTC':record['StartedAtUTC']})
            if hardware_started: pending_hardware()
            if args.action=='program':
                if live['DONE']==1:
                    runner.command([args.xsdb,ROOT/'scripts/ps_quiesce_framebuffer.tcl'],
                                   attempt+'/quiesce.log','PASS_FRAMEBUFFER_QUIESCE',timeout=30,on_start=pending_hardware)
                    record['DMAQuiescedBeforeProgramming']=True
                else:
                    # DONE=0 establishes that no configured PL DMA exists, but
                    # does not establish CPU state. Halt both cores separately.
                    stopped=runner.cleanup(args.xsdb,attempt+'/before_program')
                    if stopped['CPUState']!='confirmed_stopped' or not stopped['OwnTreeStopped']:
                        raise runner.StageError('CPU halt before programming failed',stopped['OwnTreeStopped'],stopped.get('OriginalProcessID'))
                    record['UnconfiguredPLBaseline']=True
                save('current_pl.json',{'Status':'quiesced_before_configuration','Attempt':attempt,
                     'Scope':'No CPU application launch is allowed until programming succeeds'})
                text=runner.command([vivado,'-mode','batch','-nojournal','-log',TEMP/'hardware.log',
                                     '-source',ROOT/'scripts/hardware_framebuffer.tcl','-tclargs',folder/'framebuffer_top.bit'],
                                    attempt+'/program.log','PASS_FRAMEBUFFER_PROGRAM',timeout=180,on_start=pending_hardware)
                record['FPGAResult']=fpga_result(text,'FPGA_RESULT')
                require_framebuffer(record['FPGAResult'])
                record['CPUStateAfterRun']='confirmed_stopped'
                save('current_pl.json',{'Status':'framebuffer','ProgramAttempt':attempt,
                     'BitstreamSHA256':build['BitstreamSHA256'],'VerifiedAtUTC':now(),
                     'Scope':'Historical configuration only; every action probes live JTAG configuration before PL MMIO'})
                save('failure_state.json',{'CPUState':'confirmed_stopped','OwnTreeStopped':True,'Status':'completed','Attempt':attempt})
            else:
                text=runner.command([args.xsdb,ROOT/'scripts/ps_framebuffer.tcl',args.action,
                                     folder/'framebuffer_app.elf',folder/'ps7_init.tcl'],
                                    attempt+'/'+args.action+'.log','PASS_FRAMEBUFFER_'+args.action.upper(),
                                    timeout=90,on_start=pending_hardware if hardware_started else None)
                record['DisplayResult']=display_result(text)
                progress_text=re.search(r'^DISPLAY_PROGRESS (.+)$',text,re.M)
                if not progress_text: raise RuntimeError('Missing actual animation progress')
                record['ProgressResult']=display_result('DISPLAY_RESULT '+progress_text[1])
                record['CPUStateAfterRun']='running_animation'
                if hardware_started:
                    save('failure_state.json',{'CPUState':'running_animation','OwnTreeStopped':True,'Status':'completed','Attempt':attempt})
            for filename,digest in copied.items():
                if sha(folder/filename)!=expected_outputs[filename] or sha(OUT/filename)!=expected_outputs[filename]:
                    raise RuntimeError('Hardware snapshot/output changed')
            if hashes(HARDWARE_INPUTS)!=expected: raise RuntimeError('Hardware scripts changed during action')
            record['Volatile']=True
        else:
            raise ValueError('Unsupported action')
        record.update(Status='passed',VerifiedAtUTC=now())
        save(name,record); save('attempts/'+attempt+'/result.json',record)
        if args.action=='run': save('active_app.json',{'Status':'ready','RunAttempt':attempt,'ProgramAttempt':record['ProgramAttempt']})
        print('PASS_FRAMEBUFFER_'+args.action.upper().replace('-','_'),flush=True)
    except (Exception,KeyboardInterrupt) as error:
        if hardware_started:
            if getattr(error,'own_tree_stopped',True):
                state=runner.cleanup(args.xsdb,attempt+'/failed')
            else:
                state={'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':getattr(error,'process_id',None)}
            record['FailureCleanup']=state
            save('failure_state.json',{**state,'Status':'failed','Attempt':attempt,'VerifiedAtUTC':now()})
        elif probe_started and not getattr(error,'own_tree_stopped',True):
            # A surviving probe process is still a hardware-operation gate. Do
            # not halt a healthy CPU or claim that read-only probing stopped it.
            state={'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':getattr(error,'process_id',None)}
            record['FailureCleanup']=state
            save('failure_state.json',{**state,'Status':'failed','Attempt':attempt,'VerifiedAtUTC':now()})
        record.update(Status='failed',Error=str(error),FailedAtUTC=now())
        save(name,record); save('attempts/'+attempt+'/result.json',record)
        print('Framebuffer stage stopped: '+str(error),flush=True)
        return 1
    return 0

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('build','build-app','probe','program','run','sample','cleanup'))
    parser.add_argument('--vivado',type=Path); parser.add_argument('--gcc',type=Path)
    parser.add_argument('--xsdb',type=Path)
    args=parser.parse_args()
    TEMP.mkdir(parents=True,exist_ok=True)
    # Share the PS operation lock with the existing bring-up driver.
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try: msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:
            print('Another PS operation is active.',flush=True); return 1
        try: return execute(args)
        finally:
            lock.seek(0); msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)

if __name__=='__main__': raise SystemExit(main())
