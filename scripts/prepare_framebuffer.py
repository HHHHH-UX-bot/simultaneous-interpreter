"""Derive current PS configuration from the verified RevB source record."""
import hashlib,json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/framebuffer'

def main():
    baseline=json.loads((ROOT/'hardware/zynq_mini_revb/source.json').read_text(encoding='utf-8'))
    passed=json.loads((ROOT/'output/ps_bringup/run.json').read_text(encoding='utf-8'))
    if passed['Status']!='passed' or baseline['Part']!='xc7z010clg400-1':
        raise RuntimeError('Verified RevB PS/DDR stage required')
    for path,digest in passed['SourceHashes'].items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('PS baseline input changed: '+path)
    params={k:v for k,v in baseline['PSParameters'].items() if k.startswith('PCW_UIPARAM_DDR_')}
    params.update(PCW_CRYSTAL_PERIPHERAL_FREQMHZ=baseline['PSReferenceMHz'],
                  PCW_APU_PERIPHERAL_FREQMHZ=baseline['CPUFrequencyMHz'],
                  PCW_DDR_RAM_HIGHADDR=baseline['DDRHigh'],
                  PCW_USE_M_AXI_GP0='1',PCW_USE_S_AXI_HP0='1',
                  PCW_S_AXI_HP0_DATA_WIDTH='64',PCW_EN_CLK0_PORT='0',PCW_EN_RST0_PORT='0')
    OUT.mkdir(parents=True,exist_ok=True)
    lines=['# Derived from hardware/zynq_mini_revb/source.json; do not edit independently.',
           'proc configure_rev_b_ps {cell} {','    set pairs {}']
    for key,value in sorted(params.items()):
        if any(c in value for c in '{}\n\r'): raise RuntimeError('Invalid configuration value')
        lines.extend([f'    if {{[lsearch -exact [list_property $cell] CONFIG.{key}] < 0}} {{error "Missing PS parameter {key}"}}',
                      f'    lappend pairs CONFIG.{key} {{{value}}}'])
    lines.extend(['    set_property -dict $pairs $cell','}'])
    (OUT/'ps7_config.tcl').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (OUT/'configuration_source.json').write_text(json.dumps({
        'Part':baseline['Part'],'BaselineSHA256':hashlib.sha256((ROOT/'hardware/zynq_mini_revb/source.json').read_bytes()).hexdigest(),
        'Parameters':params,'PixelClockHz':50000000,'FramebufferFormat':'XRGB8888',
        'Width':1024,'Height':600,'Stride':4096,'Buffers':[0x01000000,0x01400000],
        'VDMABase':0x43c00000,'VideoBase':0x43c10000,
        'Scope':'Derived configuration, not a build or hardware result'},indent=2),encoding='utf-8')
    (OUT/'framebuffer_hw.h').write_text('''/* Derived configuration; regenerate with prepare_framebuffer.py. */
#define FB_WIDTH 1024u
#define FB_HEIGHT 600u
#define FB_STRIDE 4096u
#define FB_BASE0 0x01000000u
#define FB_BASE1 0x01400000u
#define VDMA_BASE 0x43c00000u
#define VIDEO_BASE 0x43c10000u
#define TIMER_TICKS_PER_MS 333333u
''',encoding='utf-8')
    print('PASS_FRAMEBUFFER_PREPARE')

if __name__=='__main__': main()
