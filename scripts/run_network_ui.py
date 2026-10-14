"""Select the network subtitle app on the separately verified PS UART platform."""
import argparse,json,msvcrt,re,shutil
from pathlib import Path
import run_ps_wifi as net
import run_subtitle_ui as ui
import validate_network_protocol as codec
ROOT=net.ROOT;OUT=ROOT/'output/network_ui';PL_OUT=net.platform.OUT
APP_INPUTS=(*net.host.COMMON,'software/subtitle_ui/main.c','software/subtitle_ui/runtime.c',
    'software/ps_wifi/wifi_at_probe.c','software/ps_wifi/wifi_at_probe.h','software/ps_bringup/start.S',
    'software/network_ui/main.c','software/network_ui/network.c','software/network_ui/network.h',
    'software/network_ui/protocol.c','software/network_ui/protocol.h','software/network_ui/link.ld',
    'output/ps_wifi/framebuffer_hw.h','output/ps_wifi/configuration_source.json',
    'output/subtitle_ui/assets_manifest.json','scripts/run_ps_wifi.py','scripts/run_network_ui.py')
HW_INPUTS=(*net.HW_INPUTS,'scripts/run_network_ui.py')
def save(name,value):codec.save(name,value)
def checked_app():
    r=json.loads((OUT/'build-app.json').read_text(encoding='utf-8'))
    if r['Status']!='passed' or r['SourceHashes']!=net.fb.hashes(APP_INPUTS) or r['ELFSHA256']!=net.fb.sha(OUT/'framebuffer_app.elf'):
        raise RuntimeError('Current validated network UI application required')
    return r
def checked_platform():
    r=net.platform.checked_build()
    for p,k in (('framebuffer_top.bit','BitstreamSHA256'),('ps_display.xsa','XSASHA256'),('ps7_init.tcl','PSInitSHA256')):
        if net.fb.sha(OUT/p)!=r[k]:raise RuntimeError('Selected network platform artifact changed: '+p)
    return r
def require_owner():
    active=json.loads((ROOT/'output/ps_runtime/active_app.json').read_text(encoding='utf-8'))
    local=json.loads((OUT/'active_app.json').read_text(encoding='utf-8'))
    if active.get('Status')!='ready' or active.get('ApplicationOutput')!='output/network_ui' or \
       active.get('RunAttempt')!=local.get('RunAttempt') or active.get('ProgramAttempt')!=local.get('ProgramAttempt'):
        raise RuntimeError('Physical PS application owner is not the current network UI')
def configure(action):
    net.OUT=OUT;net.APP_INPUTS=APP_INPUTS;net.HW_INPUTS=HW_INPUTS
    net.configure(action)
    net.fb.checked_build=checked_platform
def build(a):
    platform=net.platform.checked_build()
    for path in (OUT/'protocol_tests.json',OUT/'runtime_tests.json',ROOT/'output/subtitle_ui/host_validation.json'):
        r=json.loads(path.read_text(encoding='utf-8'))
        if r['Status']!='passed' or any(net.fb.sha(ROOT/p)!=h for p,h in r['SourceHashes'].items()):
            raise RuntimeError('Current prerequisite tests required: '+str(path.relative_to(ROOT)))
    OUT.mkdir(parents=True,exist_ok=True)
    for p,k in (('framebuffer_top.bit','BitstreamSHA256'),('ps_display.xsa','XSASHA256'),('ps7_init.tcl','PSInitSHA256')):
        shutil.copyfile(PL_OUT/p,OUT/p)
        if net.fb.sha(OUT/p)!=platform[k]:raise RuntimeError('Platform selection copy mismatch')
    record={'Status':'pending','SourceHashes':net.fb.hashes(APP_INPUTS),'StartedAtUTC':net.fb.now()};save('build-app.json',record)
    net.runner.OUT=OUT;net.runner.TEMP=ROOT/'tmp/network_ui'
    # MMU-off ARM memory cannot accept compiler-generated unaligned halfwords.
    flags=['-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-mno-unaligned-access','-O2','-g','-ffreestanding','-fno-builtin',
        '-fno-stack-protector','-fno-strict-aliasing','-fno-short-enums','-Wall','-Wextra','-Werror',
        '-std=c11','-nostdlib','-Wl,--build-id=none','-I',ROOT/'software/subtitle_ui','-I',PL_OUT,
        '-T',ROOT/'software/network_ui/link.ld','-Wl,-Map='+str(OUT/'framebuffer_app.map')]
    sources=('software/ps_bringup/start.S','software/network_ui/main.c','software/network_ui/network.c',
        'software/network_ui/protocol.c','software/ps_wifi/wifi_at_probe.c','software/subtitle_ui/ui_model.c',
        'software/subtitle_ui/ui_render.c','software/subtitle_ui/ui_demo.c','software/subtitle_ui/runtime.c','output/subtitle_ui/ui_assets.c')
    net.runner.command([a.gcc,*flags,*[ROOT/p for p in sources],'-lgcc','-o',OUT/'framebuffer_app.elf'],'app/arm.log')
    symbols=net.runner.command([a.gcc.with_name('arm-none-eabi-nm.exe'),'-n',OUT/'framebuffer_app.elf'],'app/symbols.log')
    for name,address in (('_start',0x10000),('report',0x1e000),('ui_info',0x1e080),('ui_command',0x1e0c0),
                         ('wifi_info',0x1e100),('network_report',0x1e140),('network_config',0x1e180),('snapshots',0x1b00000)):
        m=re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+name+r'$',symbols,re.M)
        if not m or int(m[1],16)!=address:raise RuntimeError('ARM mailbox/layout mismatch: '+name)
    if net.fb.hashes(APP_INPUTS)!=record['SourceHashes']:raise RuntimeError('Network inputs changed during build')
    record.update(Status='passed',VerifiedAtUTC=net.fb.now(),ELFSHA256=net.fb.sha(OUT/'framebuffer_app.elf'),
        LoadSegments=ui.elf_layout(OUT/'framebuffer_app.elf'),ModelBytes=11384,CompileFlags=[str(f) for f in flags],
        PlatformBuildSHA256=net.fb.sha(PL_OUT/'build.json'),
        Application='network_simulated_subtitle_ui',Scope='Runtime-only Wi-Fi/TCP and simulated results; no audio, model inference or touch')
    save('build-app.json',record);print('PASS_NETWORK_UI_ARM_BUILD',flush=True);return 0
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=('build','program','run','sample','cleanup'))
    p.add_argument('--gcc',type=Path);p.add_argument('--vivado',type=Path);p.add_argument('--xsdb',type=Path);a=p.parse_args()
    with (ROOT/'tmp/ps/operation.lock').open('a+b') as lock:
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:print('Another PS operation is active.',flush=True);return 1
        try:
            if a.action=='build':return build(a)
            configure(a.action)
            if a.action!='cleanup':checked_app()
            if a.action=='sample':require_owner()
            return net.fb.execute(a)
        except (Exception,KeyboardInterrupt) as e:
            print('Network UI stopped: '+str(e),flush=True);return 1
        finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
if __name__=='__main__':raise SystemExit(main())
