"""Build/select the simulated subtitle app, then reuse the verified display gates."""
import argparse,json,msvcrt,shutil,struct,sys
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
import validate_subtitle_ui as host

ROOT=fb.ROOT
OUT=ROOT/'output/subtitle_ui'
APP_INPUTS=(*host.COMMON,'software/subtitle_ui/main.c','software/subtitle_ui/runtime.c',
    'software/subtitle_ui/link.ld','software/ps_bringup/start.S','output/framebuffer/framebuffer_hw.h',
    'output/framebuffer/configuration_source.json','output/subtitle_ui/assets_manifest.json',
    'scripts/run_subtitle_ui.py')
def write(name,data):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def checked_ui_build():
    result=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
    if result['Status']!='passed' or result['SourceHashes']!=fb.hashes(APP_INPUTS) or \
       result['ELFSHA256']!=fb.sha(OUT/'subtitle_ui.elf'):
        raise RuntimeError('Current successful subtitle application build required')
    return result
def elf_layout(path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x01\x01': raise RuntimeError('Expected ELF32 little-endian executable')
    header=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    if header[0]!=2 or header[1]!=40 or header[3]!=0x10000:
        raise RuntimeError('Unexpected ARM executable/entry point')
    segments=[]
    ranges=((0x10000,0x20000),(0x01800000,0x01a00000),(0x01b00000,0x01b10000))
    for index in range(header[9]):
        kind,offset,virtual,physical,file_bytes,memory_bytes,flags,alignment=struct.unpack_from(
            '<IIIIIIII',data,header[4]+index*header[8])
        if kind!=1: continue
        if physical!=virtual or file_bytes>memory_bytes or offset+file_bytes>len(data) or \
           not any(lo<=physical and physical+memory_bytes<=hi for lo,hi in ranges):
            raise RuntimeError('ELF load segment exceeds its dedicated region')
        segments.append({'Address':physical,'FileBytes':file_bytes,'MemoryBytes':memory_bytes,'Flags':flags})
    if not any(s['Address']==0x01800000 and s['FileBytes'] for s in segments):
        raise RuntimeError('Font DDR payload missing from executable')
    return segments
def build(args):
    if args.gcc is None: raise ValueError('--gcc required')
    record={'Status':'pending','SourceHashes':fb.hashes(APP_INPUTS),'StartedAtUTC':fb.now()}
    write('build.json',record)
    runner.OUT=OUT;runner.TEMP=ROOT/'tmp/subtitle_ui';runner.TEMP.mkdir(parents=True,exist_ok=True)
    try:
        fb.checked_build()
        model=json.loads((OUT/'model_tests.json').read_text(encoding='utf-8'))
        if model['Status']!='passed' or any(fb.sha(ROOT/p)!=h for p,h in model['SourceHashes'].items()):
            raise RuntimeError('Current model regression required')
        assets=json.loads((OUT/'assets_validation.json').read_text(encoding='utf-8'))
        if assets['status']!='passed' or assets['source_sha256']!=fb.sha(OUT/'ui_assets.c') or \
           assets['header_sha256']!=fb.sha(ROOT/'software/subtitle_ui/ui_assets.h') or \
           assets['manifest_sha256']!=fb.sha(OUT/'assets_manifest.json'):
            raise RuntimeError('Current font asset validation required')
        validation=json.loads((OUT/'host_validation.json').read_text(encoding='utf-8'))
        if validation['Status']!='passed' or any(fb.sha(ROOT/p)!=h for p,h in validation['SourceHashes'].items()):
            raise RuntimeError('Current native UI rendering validation required')
        elf=OUT/'subtitle_ui.elf'
        sources=('software/ps_bringup/start.S','software/subtitle_ui/main.c','software/subtitle_ui/ui_model.c',
            'software/subtitle_ui/ui_render.c','software/subtitle_ui/ui_demo.c','software/subtitle_ui/runtime.c',
            'output/subtitle_ui/ui_assets.c')
        flags=['-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O2','-g','-ffreestanding','-fno-builtin',
            '-fno-stack-protector','-fno-strict-aliasing','-fno-short-enums',
            '-Wall','-Wextra','-Werror','-std=c11','-nostdlib',
            '-Wl,--build-id=none','-I',ROOT/'software/subtitle_ui','-I',ROOT/'output/framebuffer',
            '-T',ROOT/'software/subtitle_ui/link.ld','-Wl,-Map='+str(OUT/'subtitle_ui.map')]
        runner.command([args.gcc,*flags,*[ROOT/p for p in sources],'-lgcc','-o',elf],'build/arm.log',timeout=180)
        symbols=runner.command([args.gcc.with_name('arm-none-eabi-nm.exe'),'-n',elf],'build/symbols.log')
        def symbol(name):
            import re
            match=re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+re.escape(name)+r'$',symbols,re.M)
            if not match: raise RuntimeError('Missing ELF symbol '+name)
            return int(match[1],16)
        if symbol('_start')!=0x10000 or symbol('report')!=0x1e000 or symbol('ui_info')!=0x1e080 or \
           symbol('ui_command')!=0x1e0c0 or symbol('snapshots')!=0x01b00000:
            raise RuntimeError('Unexpected UI/display ABI or memory layout')
        if symbol('__ui_runtime_end')-symbol('__ui_runtime_start')!=2*(validation['ModelBytes']+64):
            raise RuntimeError('ARM and native snapshot/model sizes differ')
        record.update(Status='passed',ELFSHA256=fb.sha(elf),LoadSegments=elf_layout(elf),
            ModelBytes=validation['ModelBytes'],Symbols={key:symbol(key) for key in
                ('_start','report','ui_info','ui_command','snapshots','__ui_assets_end','__bss_end')},
            Application='simulated_subtitle_ui',VerifiedAtUTC=fb.now(),
            Scope='CPU-drawn Chinese/English demo with bounded model/history; no audio, network inference or USB touch')
        if fb.hashes(APP_INPUTS)!=record['SourceHashes']: raise RuntimeError('UI inputs changed during build')
        # Select the new app for the existing driver, preserving old attempt ELFs.
        shutil.copyfile(elf,fb.OUT/'framebuffer_app.elf')
        if fb.sha(fb.OUT/'framebuffer_app.elf')!=record['ELFSHA256']: raise RuntimeError('Application selection hash mismatch')
        fb.save('build-app.json',record);write('build.json',record)
        print('PASS_SUBTITLE_ARM_BUILD',flush=True)
    except (Exception,KeyboardInterrupt) as error:
        record.update(Status='failed',Error=str(error),FailedAtUTC=fb.now());write('build.json',record)
        print(error,flush=True);return 1
    return 0
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('build','program','run','sample'))
    parser.add_argument('--gcc',type=Path);parser.add_argument('--vivado',type=Path);parser.add_argument('--xsdb',type=Path)
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try: msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError: print('Another PS operation is active.',flush=True);return 1
        try:
            if args.action=='build': return build(args)
            current=checked_ui_build()
            if fb.sha(fb.OUT/'framebuffer_app.elf')!=current['ELFSHA256']:
                raise RuntimeError('Selected application is not the validated subtitle UI')
            fb.APP_INPUTS=APP_INPUTS
            fb.HARDWARE_INPUTS=(*fb.HARDWARE_INPUTS,'scripts/run_subtitle_ui.py')
            return fb.execute(args)
        except (Exception,KeyboardInterrupt) as error:
            print('Subtitle UI stage stopped: '+str(error),flush=True);return 1
        finally:
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__': raise SystemExit(main())
