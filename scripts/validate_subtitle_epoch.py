"""Decisive stale/current epoch comparison using an otherwise-valid font command."""
import argparse,ctypes,json,msvcrt
from pathlib import Path
import ui_debug as ui
from validate_subtitle_controls import State
from compare_subtitle_capture import compare

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--xsdb',type=Path,required=True)
    parser.add_argument('--vivado',type=Path,required=True);parser.add_argument('--gcc-directory',type=Path,required=True)
    args=parser.parse_args();inputs=(*ui.INPUTS,'scripts/validate_subtitle_epoch.py',
                                   'scripts/validate_subtitle_controls.py','scripts/compare_subtitle_capture.py')
    record={'Status':'pending','StartedAtUTC':ui.fb.now(),'SourceHashes':ui.fb.hashes(inputs),'Operations':[],
            'Scope':'Actual accepted pause, stale/current valid command, immutable state/pixel proofs and resume; no USB touch'}
    def operation(action,command=None,epoch=None):
        if ui.execute(argparse.Namespace(action=action,command=command,expected_epoch=epoch,
                                       xsdb=args.xsdb,vivado=args.vivado)):
            raise RuntimeError('Epoch hardware operation failed')
        latest=json.loads((ui.OUT/('latest_'+action+'.json')).read_text(encoding='utf-8'))
        saved=json.loads((ui.ROOT/latest['ResultFile']).read_text(encoding='utf-8'))
        if saved!=latest or saved['Status']!='passed':raise RuntimeError('Epoch proof changed')
        record['Operations'].append({'Action':action,'Command':command,'Epoch':epoch,'Evidence':saved['ResultFile']})
        ui.app.write('epoch_verification.json',record);return saved
    def command(name,epoch=None,expected=1):
        saved=operation('command',name,epoch)
        if saved['CommandResult'][1]!=expected:raise RuntimeError('Incorrect acceptance for '+name)
        return saved
    def capture():
        saved=operation('capture');folder=(ui.ROOT/saved['ResultFile']).parent
        if compare(folder,args.gcc_directory):raise RuntimeError('Epoch captured frame differs from native image')
        if saved['SnapshotHeader'][3]!=ctypes.sizeof(State):raise RuntimeError('Epoch model ABI mismatch')
        return State.from_buffer_copy((folder/'snapshot.bin').read_bytes()[32:32+ctypes.sizeof(State)])
    try:
        with (ui.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
            try:
                paused=command('pause');before=capture()
                if before.session!=2 or before.epoch<2 or before.font!=1 or \
                   before.history_count!=paused['UIResult'][11] or before.final_count!=paused['UIResult'][12]:
                    raise RuntimeError('Expected a successful running-to-paused standard-font transition')
                stale=command('font-plus',before.epoch-1,2);unchanged=capture()
                if stale['UIResult'][10]!=before.epoch or bytes(before)!=bytes(unchanged):
                    raise RuntimeError('Stale epoch modified the paused state')
                current=command('font-plus',before.epoch,1);changed=capture()
                if changed.session!=2 or changed.epoch!=before.epoch or changed.font!=2 or \
                   changed.final_count!=before.final_count or changed.history_count!=before.history_count or \
                   bytes(changed.source)!=bytes(before.source) or bytes(changed.translated)!=bytes(before.translated) or \
                   bytes(changed.history)!=bytes(before.history):
                    raise RuntimeError('Current valid command failed or disturbed final/history state')
                command('font-minus',before.epoch);resumed=command('resume',before.epoch)
                if resumed['UIResult'][10]<=before.epoch or resumed['UIResult'][11]!=before.history_count or \
                   resumed['UIResult'][12]!=before.final_count:
                    raise RuntimeError('Resume did not retain final/history and create a fresh epoch')
            finally:
                lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
        if ui.fb.hashes(inputs)!=record['SourceHashes']:raise RuntimeError('Epoch verification inputs changed')
        record.update(Status='passed',VerifiedAtUTC=ui.fb.now(),PreviousEpoch=before.epoch,
            RunningEpoch=resumed['UIResult'][10],StateUnchangedAfterStaleCommand=True,
            ValidCommandAccepted=True,FinalAndHistoryPreserved=True,ThreeWholeFramesMatched=True,
            ApplicationELFSHA256=ui.app.checked_ui_build()['ELFSHA256'])
        print('PASS_SUBTITLE_DECISIVE_EPOCH_CONTROL',flush=True)
    except (Exception,KeyboardInterrupt) as error:
        record.update(Status='failed',FailedAtUTC=ui.fb.now(),Error=str(error));print('EPOCH_STOPPED '+str(error),flush=True)
    ui.app.write('epoch_verification.json',record)
    return 0 if record['Status']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
