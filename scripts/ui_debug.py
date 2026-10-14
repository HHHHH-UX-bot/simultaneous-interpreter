"""Bounded observation/control of the simulated UI; physical touch is separate."""
import argparse,json,msvcrt,re,struct,sys,uuid
from pathlib import Path
from PIL import Image
import run_framebuffer as fb
import run_ps_bringup as runner
import run_subtitle_ui as app

ROOT=fb.ROOT;OUT=app.OUT
INPUTS=(*fb.HARDWARE_INPUTS,'scripts/ui_debug.py','scripts/ps_subtitle_ui.tcl','scripts/run_subtitle_ui.py')
COMMANDS={'start':1,'pause':2,'resume':3,'clear':4,'font-minus':5,'font-plus':6,'source-toggle':7}
def values(text,marker,size):
    matches=re.findall(r'^'+marker+r' (.+)$',text,re.M)
    if len(matches)!=1: raise RuntimeError('Missing or ambiguous '+marker)
    data=[int(v,0) for v in matches[0].split()]
    if len(data)!=size: raise RuntimeError('Invalid '+marker+' layout')
    return data
def execute(args):
    build=app.checked_ui_build();pl=fb.checked_build();fb.hardware_gate()
    active=json.loads((fb.OUT/'active_app.json').read_text(encoding='utf-8'))
    if active['Status']!='ready': raise RuntimeError('Current successful application launch required')
    prior=json.loads((fb.OUT/'attempts'/active['RunAttempt']/'result.json').read_text(encoding='utf-8'))
    programmed=json.loads((fb.OUT/'program.json').read_text(encoding='utf-8'))
    if prior['Status']!='passed' or prior['ELFSHA256']!=build['ELFSHA256'] or \
       prior['BitstreamSHA256']!=pl['BitstreamSHA256'] or programmed['Status']!='passed' or \
       programmed['BitstreamSHA256']!=pl['BitstreamSHA256'] or \
       programmed['Attempt']!=active['ProgramAttempt'] or prior['ProgramAttempt']!=active['ProgramAttempt']:
        raise RuntimeError('The active application is not the current simulated UI')
    for filename,digest in (('framebuffer_app.elf',build['ELFSHA256']),
                            ('framebuffer_top.bit',pl['BitstreamSHA256'])):
        if prior['SnapshotHashes'][filename]!=digest or \
           fb.sha(fb.OUT/'attempts'/active['RunAttempt']/filename)!=digest:
            raise RuntimeError('The active run snapshot has changed')
    attempt=fb.now().replace(':','').replace('.','_')+'_'+uuid.uuid4().hex[:6]
    folder=OUT/'hardware'/attempt;folder.mkdir(parents=True,exist_ok=False)
    record={'Status':'pending','Action':args.action,'Attempt':attempt,'StartedAtUTC':fb.now(),
            'ResultFile':str((folder/'result.json').relative_to(ROOT)).replace('\\','/'),
            'SourceHashes':fb.hashes(INPUTS),'ApplicationELFSHA256':build['ELFSHA256'],
            'BitstreamSHA256':pl['BitstreamSHA256'],'RunAttempt':active['RunAttempt'],
            'ProgramAttempt':active['ProgramAttempt']}
    runner.OUT=OUT;runner.TEMP=ROOT/'tmp/subtitle_ui';runner.TEMP.mkdir(exist_ok=True)
    hardware_started=False
    try:
        fb.require_framebuffer(fb.probe_live(fb.vivado_for(args),'ui_'+attempt))
        def pending(pid=None):
            fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':pid,
                'Status':'pending_ui_'+args.action,'Attempt':attempt})
        # A surviving read-only XSDB process also blocks later hardware actions.
        hardware_started=True;pending()
        text=runner.command([args.xsdb,ROOT/'scripts/ps_subtitle_ui.tcl',args.action,folder,
            COMMANDS.get(args.command,0),'' if args.expected_epoch is None else args.expected_epoch],
            'hardware/'+attempt+'/operation.log','PASS_SUBTITLE_'+args.action.upper(),
            timeout=180 if args.action=='capture' else 35,on_start=pending)
        ui=values(text,'UI_RESULT',16);display=fb.display_result(text)
        if ui[0]!=0x55493131 or ui[1]&1 or ui[2]!=0x01b00000 or \
           ui[3]!=build['ModelBytes']+64 or ui[4]!=build['ModelBytes'] or ui[5]>1 or \
           ui[8]>ui[9] or not ui[10] or ui[11]>4 or ui[11]>ui[12] or ui[15]!=3 or \
           display['Magic']!=0x46504231 or display['Sequence']&1 or display['Phase']!=3 or \
           display['State']!=0x600d600d or display['CurrentBuffer']>1 or \
           display['LastFramePixels']!=614400 or display['CPUSCTLR']&0x3805 or \
           display['Underflows'] or display['FormatErrors'] or \
           display['VDMAStatus']&0xf0 or display['ErrorCode']:
            raise RuntimeError('Subtitle UI/VDMA/guard verification failed')
        record.update(UIResult=ui,DisplayResult=display)
        if args.action=='command':
            record.update(Command=args.command,CommandResult=values(text,'UI_COMMAND_RESULT',3))
        if args.action=='capture':
            raw=(folder/'framebuffer.bin').read_bytes();snapshot=(folder/'snapshot.bin').read_bytes()
            if len(raw)!=1024*600*4 or len(snapshot)!=ui[3]: raise RuntimeError('Capture byte count mismatch')
            header=list(struct.unpack_from('<8I',snapshot));model_size=header[3]
            if model_size!=build['ModelBytes'] or len(snapshot)!=32+model_size+32:
                raise RuntimeError('Snapshot ABI mismatch')
            glyph_stats=list(struct.unpack_from('<8I',snapshot,32+model_size))
            if glyph_stats[0] or glyph_stats[1]: raise RuntimeError('Board-rendered glyph loss')
            Image.frombytes('RGB',(1024,600),raw,'raw','BGRX').save(folder/'framebuffer.png')
            record.update(Capture=values(text,'UI_CAPTURE_RESULT',3),SnapshotHeader=header,
                GlyphStats=glyph_stats,PixelSHA256=fb.sha(folder/'framebuffer.bin'),
                SnapshotSHA256=fb.sha(folder/'snapshot.bin'),
                FramebufferPNG=str((folder/'framebuffer.png').relative_to(ROOT)).replace('\\','/'))
        if fb.hashes(INPUTS)!=record['SourceHashes']: raise RuntimeError('UI operation scripts changed')
        if fb.checked_build()['BitstreamSHA256']!=record['BitstreamSHA256'] or \
           app.checked_ui_build()['ELFSHA256']!=record['ApplicationELFSHA256']:
            raise RuntimeError('UI application or PL output changed during hardware operation')
        fb.save('failure_state.json',{'CPUState':'running_animation','OwnTreeStopped':True,
                                     'Status':'completed','Attempt':attempt})
        record.update(Status='passed',VerifiedAtUTC=fb.now());print('PASS_SUBTITLE_'+args.action.upper(),flush=True)
    except (Exception,KeyboardInterrupt) as error:
        if hardware_started:
            state=runner.cleanup(args.xsdb,'hardware/'+attempt+'/failed') if getattr(error,'own_tree_stopped',True) else \
                {'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':getattr(error,'process_id',None)}
            fb.save('failure_state.json',{**state,'Status':'failed_ui_'+args.action,'Attempt':attempt})
            fb.save('active_app.json',{'Status':'invalidated_after_ui_failure','Attempt':attempt})
            record['FailureCleanup']=state
        record.update(Status='failed',Error=str(error),FailedAtUTC=fb.now());print(error,flush=True)
    app.write('latest_'+args.action+'.json',record)
    (folder/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if record['Status']=='passed' else 1
def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=('sample','command','capture'))
    parser.add_argument('--xsdb',type=Path,required=True);parser.add_argument('--vivado',type=Path)
    parser.add_argument('--command',choices=tuple(COMMANDS));parser.add_argument('--expected-epoch',type=int)
    args=parser.parse_args()
    if args.action=='command' and args.command is None: parser.error('--command required')
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try: msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError: print('Another PS operation is active.',flush=True);return 1
        try: return execute(args)
        except (Exception,KeyboardInterrupt) as error:
            print('UI debug operation stopped: '+str(error),flush=True);return 1
        finally:
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__': raise SystemExit(main())
