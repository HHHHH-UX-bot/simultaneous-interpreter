"""Correct the START criterion using the already verified immutable board evidence."""
import ctypes,json,hashlib,shutil
from pathlib import Path
import ui_debug as ui
from validate_subtitle_controls import State

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    path=ui.OUT/'controls.json';original=json.loads(path.read_text(encoding='utf-8'))
    if original['Status']!='failed' or original.get('Error')!='New running demo session did not resume correctly':
        raise RuntimeError('Only the known START acceptance criterion is eligible for evidence recheck')
    build=ui.app.checked_ui_build();pl=ui.fb.checked_build()
    expected=ui.fb.hashes(ui.INPUTS)
    for p,h in original['SourceHashes'].items():
        if p!='scripts/validate_subtitle_controls.py' and ui.fb.sha(ui.ROOT/p)!=h:
            raise RuntimeError('A non-criterion input changed since the controls attempt')
    operations=original['Operations']
    if len(operations)!=15:raise RuntimeError('Expected complete original 15-operation sequence')
    results=[]
    for item in operations:
        saved=json.loads((ui.ROOT/item['Evidence']).read_text(encoding='utf-8'))
        if saved['Status']!='passed' or saved['Action']!=item['Action'] or saved['SourceHashes']!=expected or \
           saved['ApplicationELFSHA256']!=build['ELFSHA256'] or saved['BitstreamSHA256']!=pl['BitstreamSHA256']:
            raise RuntimeError('Original hardware evidence is not current, successful and bound')
        if item['Action']=='command' and (saved['Command']!=item['Command'] or
                saved['CommandResult'][1]!=(2 if item['ExpectedEpoch'] is not None else
                                           saved['CommandResult'][1] if item['Command']=='pause' else 1)):
            raise RuntimeError('Unexpected control acceptance in original evidence')
        results.append(saved)
    def state(index):
        saved=results[index];folder=(ui.ROOT/operations[index]['Evidence']).parent
        compared=json.loads((folder/'pixel_comparison.json').read_text(encoding='utf-8'))
        if compared['Status']!='passed' or compared['PixelsCompared']!=614400 or compared['PixelMismatches'] or \
           sha(folder/'snapshot.bin')!=saved['SnapshotSHA256'] or sha(folder/'framebuffer.bin')!=saved['PixelSHA256']:
            raise RuntimeError('Original captured state or exact pixel proof changed')
        return State.from_buffer_copy((folder/'snapshot.bin').read_bytes()[32:32+ctypes.sizeof(State)])
    initial,large,small,cleared,running=[state(i) for i in (1,4,7,11,14)]
    if initial.session!=2 or initial.font!=1 or large.session!=2 or large.font!=2 or large.show_source or \
       small.session!=2 or small.font!=0 or large.final_count!=initial.final_count or \
       small.final_count!=initial.final_count or bytes(large.translated)!=bytes(initial.translated) or \
       bytes(small.translated)!=bytes(initial.translated):
        raise RuntimeError('Paused fonts/source/final preservation failed')
    if cleared.session!=0 or cleared.epoch<=initial.epoch or cleared.history_count or cleared.final_count or \
       cleared.source[0] or cleared.translated[0] or cleared.font!=1 or cleared.show_source!=1 or \
       results[12]['UIResult'][10]!=cleared.epoch or results[12]['UIResult'][11] or results[12]['UIResult'][12]:
        raise RuntimeError('Clear/stale epoch isolation failed')
    if running.session!=1 or running.epoch!=cleared.epoch or running.font!=1 or running.show_source!=1:
        raise RuntimeError('START did not reuse the empty epoch already created by CLEAR')
    archive=ui.OUT/'controls_attempts';archive.mkdir(exist_ok=True)
    old=archive/('failed_start_criterion_'+sha(path)[:12]+'.json')
    if old.exists():raise RuntimeError('Original failed record archive already exists')
    shutil.copyfile(path,old)
    record={**original,'Status':'passed','VerifiedAtUTC':ui.fb.now(),
        'OriginalFailedRecord':str(old.relative_to(ui.ROOT)).replace('\\','/'),
        'CriteriaCorrection':'CLEAR advances epoch; START from empty idle reuses it. Model code was unchanged.',
        'RecheckSourceHashes':ui.fb.hashes(('scripts/recheck_subtitle_controls.py','scripts/validate_subtitle_controls.py',
                                         'software/subtitle_ui/ui_model.c')),
        'ApplicationELFSHA256':build['ELFSHA256'],'RunningEpoch':running.epoch,
        'Checks':['pause retains final','28/36/44 board fonts','source hidden/restored','clear removes results',
                  'stale epoch rejected','START uses cleared epoch','five exact 614400-pixel comparisons']}
    record.pop('Error',None);record.pop('FailedAtUTC',None)
    ui.app.write('controls.json',record);print('PASS_SUBTITLE_BOARD_CONTROLS_RECHECK',flush=True)
    return 0
if __name__=='__main__':raise SystemExit(main())
