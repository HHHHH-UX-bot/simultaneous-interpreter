"""PS-owned AT probe while the accepted simulated UI continues drawing."""
import argparse,json,msvcrt,re,shutil
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
import run_subtitle_ui as ui
import validate_subtitle_ui as host
import build_ps_wifi as platform
ROOT=fb.ROOT
OUT=platform.OUT
BASE_OUT=fb.OUT
APP_INPUTS=(*host.COMMON,'software/subtitle_ui/main.c','software/subtitle_ui/runtime.c',
    'software/ps_wifi/main.c','software/ps_wifi/wifi_at_probe.c','software/ps_wifi/wifi_at_probe.h',
    'software/ps_wifi/link.ld','software/ps_bringup/start.S',
    'output/ps_wifi/framebuffer_hw.h','output/ps_wifi/configuration_source.json',
    'output/subtitle_ui/assets_manifest.json','scripts/run_ps_wifi.py')
HW_INPUTS=(*fb.HARDWARE_INPUTS,'scripts/hardware_ps_wifi.tcl','scripts/run_ps_wifi.py','scripts/build_ps_wifi.py')
def checked_app():
    record=json.loads((OUT/'build-app.json').read_text(encoding='utf-8'))
    if record['Status']!='passed' or record['SourceHashes']!=fb.hashes(APP_INPUTS) or \
       record['ELFSHA256']!=fb.sha(OUT/'framebuffer_app.elf'):
        raise RuntimeError('Current PS-owned Wi-Fi application build required')
    return record
def configure(action):
    # Both platforms share the same physical PS and must share failure gates.
    original_gate=fb.hardware_gate
    original_command=runner.command
    original_probe=fb.probe_live
    original_save=fb.save
    fb.OUT=OUT;fb.TEMP=ROOT/'tmp/ps_wifi';fb.TEMP.mkdir(parents=True,exist_ok=True)
    fb.PL_INPUTS=platform.PL_INPUTS;fb.APP_INPUTS=APP_INPUTS;fb.HARDWARE_INPUTS=HW_INPUTS
    fb.checked_build=platform.checked_build
    before_program=[False]
    probe_prior=[None]
    shared_directories=(OUT,BASE_OUT,ROOT/'output/ps_bringup')
    if action=='cleanup':
        # CPU halt can recover unknown CPU state, but it cannot prove an old
        # external process exited. Never overwrite another platform's gate.
        for directory in shared_directories:
            for filename in ('failure_state.json','probe_operation.json'):
                path=directory/filename
                if path.exists() and not json.loads(path.read_text(encoding='utf-8')).get('OwnTreeStopped',False):
                    raise RuntimeError('Cleanup requires every prior process tree confirmed exited: '+str(path))
    def gate_snapshot():
        return {directory:json.loads((directory/'failure_state.json').read_text(encoding='utf-8'))
                if (directory/'failure_state.json').exists() else None for directory in shared_directories}
    def restore_gates(prior):
        for directory,value in prior.items():
            directory.mkdir(parents=True,exist_ok=True)
            if value is None:
                value={'CPUState':'unchanged_by_read_only_probe','OwnTreeStopped':True,
                       'Scope':'Read-only process exited; no current CPU halt/running state inferred'}
            (directory/'failure_state.json').write_text(json.dumps(value,indent=2),encoding='utf-8')
    def pending_process(pid=None):
        save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False,'Status':'pending_external_process',
            'OriginalProcessID':pid,'Scope':'Durable pre-launch gate; launcher death cannot unlock older entries'})
    def gate():
        saved=fb.OUT
        try:
            for directory in (BASE_OUT,OUT):
                fb.OUT=directory;original_gate()
        finally:fb.OUT=saved
    def probe(vivado,attempt):
        live=original_probe(vivado,attempt)
        before_program[0]=action=='program' and live['DONE']==1
        return live
    def require(live):
        allowed=(0x46564231,0x46574e31) if before_program[0] else (0x46574e31,)
        before_program[0]=False
        if live['DONE']!=1 or live['EOS']!=1 or live['CRCError']!=0 or live['USRAccess'] not in allowed:
            raise RuntimeError('Unexpected live display/PS UART platform; no MMIO access permitted')
    def command(args,*pos,**kwargs):
        args=[ROOT/'scripts/hardware_ps_wifi.tcl' if str(a)==str(ROOT/'scripts/hardware_framebuffer.tcl') else a for a in args]
        scripts=[Path(str(a)).name for a in args]
        readonly=('probe_framebuffer.tcl' in scripts or
                  (any(s in scripts for s in ('ps_framebuffer.tcl','ps_subtitle_ui.tcl','ps_wifi_ui_debug.tcl'))
                   and 'sample' in [str(a) for a in args]))
        prior=gate_snapshot();pending_process()  # Persist BEFORE Popen, not just on_start.
        callback=kwargs.get('on_start')
        def started(pid):
            pending_process(pid)
            if callback:callback(pid)
        kwargs['on_start']=started
        try:
            result=original_command(args,*pos,**kwargs)
        except (Exception,KeyboardInterrupt) as e:
            if readonly and getattr(e,'own_tree_stopped',True) and probe_prior[0] is None:restore_gates(prior)
            raise
        if readonly and probe_prior[0] is None:restore_gates(prior)
        return result
    def save(name,value):
        original_save(name,value)
        # These are current safety gates, not historical result snapshots. A
        # surviving new process must also prevent every older PS entrypoint.
        if name=='failure_state.json':
            shared={**value,'Owner':'ps_wifi','Scope':'Shared physical PS/process gate across platform outputs'}
            for directory in (BASE_OUT,ROOT/'output/ps_bringup'):
                directory.mkdir(parents=True,exist_ok=True)
                (directory/name).write_text(json.dumps(shared,indent=2),encoding='utf-8')
        elif name=='probe_operation.json':
            (BASE_OUT/name).write_text(json.dumps({**value,'Owner':'ps_wifi'},indent=2),encoding='utf-8')
            if not value.get('OwnTreeStopped',False):
                if probe_prior[0] is None:probe_prior[0]=gate_snapshot()
                pending_process(value.get('OriginalProcessID'))
            elif probe_prior[0] is not None:
                restore_gates(probe_prior[0]);probe_prior[0]=None
        elif name=='active_app.json':
            (BASE_OUT/name).write_text(json.dumps({'Status':'invalidated_by_ps_wifi','Owner':'ps_wifi',
                'Scope':'Old simulated UI run is historical; current platform application is separate'},indent=2),encoding='utf-8')
            current=ROOT/'output/ps_runtime/active_app.json';current.parent.mkdir(parents=True,exist_ok=True)
            current.write_text(json.dumps({**value,'ApplicationOutput':str(fb.OUT.relative_to(ROOT)).replace('\\','/'),
                'Scope':'Single physical PS application owner; artifacts verified by its run snapshot'},indent=2),encoding='utf-8')
    fb.hardware_gate=gate;fb.probe_live=probe;fb.require_framebuffer=require;fb.save=save;runner.command=command
def build_app(args):
    platform.checked_build()
    for name in ('host_validation.json','model_tests.json','demo_tests.json'):
        r=json.loads((ROOT/'output/subtitle_ui'/name).read_text(encoding='utf-8'))
        if r['Status']!='passed' or any(fb.sha(ROOT/p)!=h for p,h in r['SourceHashes'].items()):
            raise RuntimeError('Current UI predecessor required: '+name)
    test=json.loads((OUT/'probe_host_tests.json').read_text(encoding='utf-8'))
    if test['Status']!='passed' or any(fb.sha(ROOT/p)!=h for p,h in test['SourceHashes'].items()):
        raise RuntimeError('Current AT driver host tests required')
    record={'Status':'pending','SourceHashes':fb.hashes(APP_INPUTS),'StartedAtUTC':fb.now()}
    platform.save('build-app.json',record)
    runner.OUT=OUT;runner.TEMP=ROOT/'tmp/ps_wifi'
    sources=('software/ps_bringup/start.S','software/ps_wifi/main.c','software/ps_wifi/wifi_at_probe.c',
        'software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c','software/subtitle_ui/ui_demo.c',
        'software/subtitle_ui/runtime.c','output/subtitle_ui/ui_assets.c')
    flags=['-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O2','-g','-ffreestanding','-fno-builtin',
        '-fno-stack-protector','-fno-strict-aliasing','-fno-short-enums','-Wall','-Wextra','-Werror',
        '-std=c11','-nostdlib','-Wl,--build-id=none','-I',ROOT/'software/subtitle_ui','-I',OUT,
        '-T',ROOT/'software/ps_wifi/link.ld','-Wl,-Map='+str(OUT/'framebuffer_app.map')]
    runner.command([args.gcc,*flags,*[ROOT/p for p in sources],'-lgcc','-o',OUT/'framebuffer_app.elf'],'app/arm.log')
    symbols=runner.command([args.gcc.with_name('arm-none-eabi-nm.exe'),'-n',OUT/'framebuffer_app.elf'],'app/symbols.log')
    for name,address in (('_start',0x10000),('report',0x1e000),('ui_info',0x1e080),
                         ('ui_command',0x1e0c0),('wifi_info',0x1e100),('snapshots',0x1b00000)):
        m=re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+name+r'$',symbols,re.M)
        if not m or int(m[1],16)!=address: raise RuntimeError('Unexpected ARM symbol: '+name)
    if fb.hashes(APP_INPUTS)!=record['SourceHashes']: raise RuntimeError('AT/UI build inputs changed')
    record.update(Status='passed',ELFSHA256=fb.sha(OUT/'framebuffer_app.elf'),LoadSegments=ui.elf_layout(OUT/'framebuffer_app.elf'),
        VerifiedAtUTC=fb.now(),ModelBytes=11384,Application='simulated_ui_ps_at_probe',
        Scope='PS-owned AT/echo/firmware query while simulated UI runs; no association, TCP, audio or USB touch')
    platform.save('build-app.json',record);print('PASS_PS_WIFI_ARM_BUILD',flush=True);return 0
def main():
    p=argparse.ArgumentParser()
    p.add_argument('action',choices=('build-app','probe','program','run','sample','cleanup'))
    p.add_argument('--gcc',type=Path);p.add_argument('--vivado',type=Path);p.add_argument('--xsdb',type=Path)
    a=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        try:
            if a.action=='build-app':
                if a.gcc is None:raise ValueError('--gcc required')
                return build_app(a)
            configure(a.action)
            if a.action not in ('probe','cleanup'):checked_app()
            return fb.execute(a)
        except (Exception,KeyboardInterrupt) as e:
            print('PS Wi-Fi stopped: '+str(e),flush=True);return 1
        finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__':raise SystemExit(main())
