"""Verify actual JTAG semantic controls against immutable board state captures."""
import argparse,ctypes,json,msvcrt
from pathlib import Path
import ui_debug as ui
from compare_subtitle_capture import compare

class History(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('epoch','segment','revision','source_revision')]+[
        ('source',ctypes.c_uint32*256),('translated',ctypes.c_uint32*256)]
class State(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('epoch','session','stage','font','show_source')]+[
        ('source',ctypes.c_uint32*256),('translated',ctypes.c_uint32*256),('history',History*4)]+[
        (n,ctypes.c_uint32) for n in ('history_count','final_count','rejected_events','change_sequence',
            'processing_segment','source_revision','source_final','translation_revision','last_final_segment')]+[
        ('last_final_source',ctypes.c_uint32*256),('language',ctypes.c_uint32),('language_menu',ctypes.c_uint32)]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--xsdb',type=Path,required=True)
    parser.add_argument('--vivado',type=Path,required=True);parser.add_argument('--gcc-directory',type=Path,required=True)
    args=parser.parse_args()
    record={'Status':'pending','StartedAtUTC':ui.fb.now(),'Operations':[],
        'SourceHashes':ui.fb.hashes((*ui.INPUTS,'scripts/validate_subtitle_controls.py','scripts/compare_subtitle_capture.py')),
        'Scope':'Actual JTAG controls and DDR pixels; no physical USB touch claim'}
    def operate(action,command=None,epoch=None):
        operation=argparse.Namespace(action=action,xsdb=args.xsdb,vivado=args.vivado,command=command,expected_epoch=epoch)
        if ui.execute(operation): raise RuntimeError('Board '+action+' failed')
        latest=json.loads((ui.OUT/('latest_'+action+'.json')).read_text(encoding='utf-8'))
        saved=json.loads((ui.ROOT/latest['ResultFile']).read_text(encoding='utf-8'))
        if saved!=latest or saved['Status']!='passed': raise RuntimeError('Operation evidence changed')
        record['Operations'].append({'Action':action,'Command':command,'ExpectedEpoch':epoch,
                                     'Evidence':saved['ResultFile']})
        ui.app.write('controls.json',record)
        return saved
    def command(name,expected=1,epoch=None):
        saved=operate('command',name,epoch)
        if saved['CommandResult'][1]!=expected: raise RuntimeError('Unexpected acceptance for '+name)
        return saved
    def capture():
        saved=operate('capture');folder=(ui.ROOT/saved['ResultFile']).parent
        if compare(folder,args.gcc_directory): raise RuntimeError('Actual controlled frame differs from native state')
        model=saved['SnapshotHeader'][3]
        if model!=ctypes.sizeof(State): raise RuntimeError('Control decoder model ABI mismatch')
        state=State.from_buffer_copy((folder/'snapshot.bin').read_bytes()[32:32+model])
        return state
    try:
        with (ui.ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
            try:
                # The caller may already have paused; both cases are checked by a real capture.
                paused=operate('command','pause')
                if paused['CommandResult'][1] not in (1,2): raise RuntimeError('Pause mailbox result invalid')
                initial=capture()
                if initial.session!=2 or initial.font!=1: raise RuntimeError('Initial paused standard-font state expected')
                epoch=initial.epoch;final_count=initial.final_count
                command('font-plus');command('source-toggle');large=capture()
                if large.session!=2 or large.font!=2 or large.show_source!=0 or \
                   large.final_count!=final_count or bytes(large.translated)!=bytes(initial.translated):
                    raise RuntimeError('Pause/font/source control did not preserve final translation')
                command('font-minus');command('font-minus');small=capture()
                if small.font!=0 or small.session!=2 or small.final_count!=final_count:
                    raise RuntimeError('Small-font control or pause persistence failed')
                command('source-toggle');command('font-plus');command('clear');cleared=capture()
                if cleared.session!=0 or cleared.epoch<=epoch or cleared.history_count or cleared.final_count or \
                   cleared.source[0] or cleared.translated[0] or cleared.font!=1 or cleared.show_source!=1:
                    raise RuntimeError('Clear did not isolate session/results while preserving preferences')
                stale=command('resume',expected=2,epoch=epoch)
                if stale['UIResult'][10]!=cleared.epoch or stale['UIResult'][11] or stale['UIResult'][12]:
                    raise RuntimeError('Stale epoch command contaminated the clear state')
                command('start');running=capture()
                if running.session!=1 or running.epoch!=cleared.epoch or running.font!=1 or running.show_source!=1:
                    raise RuntimeError('New running demo session did not resume correctly')
            finally:
                lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
        if ui.fb.hashes((*ui.INPUTS,'scripts/validate_subtitle_controls.py','scripts/compare_subtitle_capture.py'))!=record['SourceHashes']:
            raise RuntimeError('Control verification scripts changed during the run')
        record.update(Status='passed',VerifiedAtUTC=ui.fb.now(),Checks=[
            'pause retains final','28/36/44 board fonts','source hidden/restored','clear removes results',
            'stale epoch rejected','new running session','five exact 614400-pixel comparisons'],
            RunningEpoch=running.epoch,ApplicationELFSHA256=ui.app.checked_ui_build()['ELFSHA256'])
        print('PASS_SUBTITLE_BOARD_CONTROLS',flush=True)
    except (Exception,KeyboardInterrupt) as error:
        record.update(Status='failed',FailedAtUTC=ui.fb.now(),Error=str(error));print('CONTROLS_STOPPED '+str(error),flush=True)
    ui.app.write('controls.json',record)
    return 0 if record['Status']=='passed' else 1
if __name__=='__main__': raise SystemExit(main())
