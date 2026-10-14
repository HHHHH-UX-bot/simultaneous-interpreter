"""Thirty real minutes of bounded board UI observation; no simulated-time shortcut."""
import argparse,json,msvcrt,time
from pathlib import Path
import ui_debug as ui

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--xsdb',type=Path,required=True)
    parser.add_argument('--vivado',type=Path,required=True);args=parser.parse_args()
    operation=argparse.Namespace(action='sample',xsdb=args.xsdb,vivado=args.vivado,
                                 command=None,expected_epoch=None)
    record={'Status':'pending','StartedAtUTC':ui.fb.now(),'RequiredRealSeconds':1800,'Samples':[],
            'SourceHashes':ui.fb.hashes((*ui.INPUTS,'scripts/soak_subtitle_ui.py')),
            'Scope':'Board timing/VDMA/errors/fixed memory guards only; no audio, network, touch or HDMI sensory claim'}
    first_tick=None;baseline=None;previous=None;last_final_tick=None
    try:
        while True:
            with (ui.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
                lock.seek(0)
                msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
                try:
                    if ui.execute(operation): raise RuntimeError('Board sample failed; inspect latest_sample.json')
                    latest=json.loads((ui.OUT/'latest_sample.json').read_text(encoding='utf-8'))
                    immutable=(ui.ROOT/latest['ResultFile']).resolve()
                    immutable.relative_to((ui.OUT/'hardware').resolve())
                    sample=json.loads(immutable.read_text(encoding='utf-8'))
                    if sample!=latest or sample['Status']!='passed' or sample['Action']!='sample' or \
                       immutable.parent.name!=sample['Attempt']:
                        raise RuntimeError('Observation does not match its immutable successful sample')
                finally:
                    lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
            info=sample['UIResult'];display=sample['DisplayResult'];tick=time.monotonic()
            if first_tick is None:
                first_tick=tick;last_final_tick=tick;baseline=sample
                record.update(ApplicationELFSHA256=sample['ApplicationELFSHA256'],
                    BitstreamSHA256=sample['BitstreamSHA256'],RunAttempt=sample['RunAttempt'],
                    ObservationStartedAtUTC=sample['VerifiedAtUTC'])
            else:
                if (sample['RunAttempt']!=baseline['RunAttempt'] or
                    sample['ApplicationELFSHA256']!=baseline['ApplicationELFSHA256'] or
                    sample['BitstreamSHA256']!=baseline['BitstreamSHA256'] or
                    info[10]!=baseline['UIResult'][10] or
                    display['GoodFrames']<=previous['DisplayResult']['GoodFrames'] or
                    display['Flips']<=previous['DisplayResult']['Flips'] or
                    info[7]<=previous['UIResult'][7] or info[6]<=previous['UIResult'][6] or
                    info[12]<previous['UIResult'][12] or info[13]!=baseline['UIResult'][13]):
                    raise RuntimeError('UI run/epoch/progress changed or stopped during endurance observation')
                if info[12]>previous['UIResult'][12]: last_final_tick=tick
                if tick-last_final_tick>45:
                    raise RuntimeError('No new final subtitle within the demo continuous-progress bound')
            elapsed=tick-first_tick
            record['Samples'].append({'VerifiedAtUTC':sample['VerifiedAtUTC'],'ElapsedRealSeconds':round(elapsed,3),
                'UIResult':info,'DisplayResult':display,'Evidence':sample['ResultFile'],
                'HardwareAttempt':sample['Attempt']})
            ui.app.write('soak.json',record)
            print(f"SOAK_PROGRESS real_seconds={elapsed:.1f} frames={display['GoodFrames']} "
                  f"flips={display['Flips']} finals={info[12]} errors=0",flush=True)
            previous=sample
            if elapsed>=1800:
                if info[12]<=baseline['UIResult'][12] or info[11]!=4:
                    raise RuntimeError('No real new final subtitles or fixed history filled during soak')
                record.update(Status='passed',VerifiedAtUTC=ui.fb.now(),ActualRealSeconds=round(elapsed,3),
                    FrameDelta=display['GoodFrames']-baseline['DisplayResult']['GoodFrames'],
                    FlipDelta=display['Flips']-baseline['DisplayResult']['Flips'],
                    FinalDelta=info[12]-baseline['UIResult'][12],MaxHistoryCount=max(s['UIResult'][11] for s in record['Samples']),
                    MaxRenderMilliseconds=max(s['UIResult'][9] for s in record['Samples']),
                    Underflows=0,FormatErrors=0,VDMAErrors=0,GuardChecksVerified=True)
                break
            until=min(tick+45,first_tick+1800)
            while time.monotonic()<until: time.sleep(max(0,min(1,until-time.monotonic())))
        if ui.fb.hashes((*ui.INPUTS,'scripts/soak_subtitle_ui.py'))!=record['SourceHashes']:
            raise RuntimeError('Observation scripts changed during soak')
        print('PASS_SUBTITLE_REAL_30_MINUTES',flush=True)
    except (Exception,KeyboardInterrupt) as error:
        record.update(Status='failed',FailedAtUTC=ui.fb.now(),Error=str(error));print('SOAK_STOPPED '+str(error),flush=True)
    ui.app.write('soak.json',record)
    return 0 if record['Status']=='passed' else 1
if __name__=='__main__': raise SystemExit(main())
