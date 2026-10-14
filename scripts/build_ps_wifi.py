"""Verify the PS UART address window, then build an independent display+UART PL."""
import argparse,json,uuid
from pathlib import Path
import run_framebuffer as fb
import run_ps_bringup as runner
import prepare_framebuffer
ROOT=fb.ROOT
OUT=ROOT/'output/ps_wifi'
TEMP=ROOT/'tmp/ps_wifi'
SIM_INPUTS=('rtl/ps_wifi_uart.v','rtl/wifi_uart_axi.v','rtl/wifi_uart_phy.v','rtl/wifi_byte_fifo.v',
            'sim/tb_ps_wifi_uart.sv','scripts/build_ps_wifi.py','scripts/run_framebuffer.py','scripts/run_ps_bringup.py')
PL_INPUTS=tuple(p for p in fb.PL_INPUTS if p!='rtl/framebuffer_top.v')+(
    'rtl/ps_wifi_top.v','rtl/ps_wifi_uart.v','rtl/wifi_uart_axi.v','rtl/wifi_uart_phy.v',
    'rtl/wifi_byte_fifo.v','scripts/build_ps_wifi.tcl','scripts/build_ps_wifi.py',
    'scripts/run_framebuffer.py','scripts/run_ps_bringup.py')
def save(name,data):
    path=OUT/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def checked_build():
    value=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
    if value['Status']!='passed' or value['SourceHashes']!=fb.hashes(PL_INPUTS):
        raise RuntimeError('Current PS Wi-Fi build required')
    for p,k in (('framebuffer_top.bit','BitstreamSHA256'),('ps_display.xsa','XSASHA256'),('ps7_init.tcl','PSInitSHA256')):
        if fb.sha(OUT/p)!=value[k]: raise RuntimeError('PS Wi-Fi output changed: '+p)
    return value
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('simulate','build'))
    parser.add_argument('--vivado',required=True,type=Path)
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);TEMP.mkdir(parents=True,exist_ok=True)
    runner.OUT=OUT;runner.TEMP=TEMP
    inputs=SIM_INPUTS if args.action=='simulate' else PL_INPUTS
    rec={'Status':'pending','StartedAtUTC':fb.now(),'SourceHashes':fb.hashes(inputs)}
    attempt=uuid.uuid4().hex
    name='simulation.json' if args.action=='simulate' else 'build.json'
    save(name,rec)
    try:
        if args.action=='simulate':
            work=TEMP/'sim';work.mkdir(exist_ok=True)
            runner.TEMP=work
            tools=args.vivado.parent
            # This simulation uses independent UART wire decoding and channel backpressure.
            runner.command([tools/'xvlog.bat','--sv',*[ROOT/p for p in SIM_INPUTS[:5]]],
                attempt+'/xvlog.log')
            runner.command([tools/'xelab.bat','work.tb_ps_wifi_uart','-s','tb_ps_wifi_uart','--debug','off'],
                attempt+'/xelab.log')
            runner.command([tools/'xsim.bat','tb_ps_wifi_uart','-runall','-log',work/'xsim.log'],
                attempt+'/xsim.log','PASS_PS_WIFI_UART')
            rec['Scope']='Full PS AXI addresses, foreign/reserved addresses, independent TX/RX wire decoding, FIFO and backpressure'
        else:
            sim=json.loads((OUT/'simulation.json').read_text(encoding='utf-8'))
            if sim['Status']!='passed' or sim['SourceHashes']!=fb.hashes(SIM_INPUTS):
                raise RuntimeError('Current full-address UART regression required')
            fb.checked_record('simulation.json',('rtl/framebuffer_video.v','sim/tb_framebuffer_video.v'))
            fb.checked_record('reset_simulation.json',('scripts/build_framebuffer.tcl','rtl/framebuffer_video.v',
                'sim/tb_framebuffer_reset.v','scripts/simulate_framebuffer_reset.py','scripts/run_ps_bringup.py'))
            prepare_framebuffer.OUT=OUT;prepare_framebuffer.main()
            runner.command([args.vivado,'-mode','batch','-nojournal','-log',TEMP/'implementation.log',
                '-source',ROOT/'scripts/build_ps_wifi.tcl','-tclargs','build'],
                attempt+'/implementation.log','PASS_FRAMEBUFFER_BUILD',timeout=1800)
            implementation=json.loads((OUT/'implementation.json').read_text(encoding='utf-8'))
            if implementation['usr_access']!=0x46574e31: raise RuntimeError('New platform configuration identity missing')
            rec.update(Implementation=implementation,BitstreamSHA256=fb.sha(OUT/'framebuffer_top.bit'),
                XSASHA256=fb.sha(OUT/'ps_display.xsa'),PSInitSHA256=fb.sha(OUT/'ps7_init.tcl'),
                UARTBase=0x43c20000,USRAccess=0x46574e31,Scope='Independent display+PS UART; no software launch or hardware programming')
        if fb.hashes(inputs)!=rec['SourceHashes']: raise RuntimeError('Build/simulation inputs changed during action')
        rec.update(Status='passed',VerifiedAtUTC=fb.now());save(name,rec)
        print('PASS_PS_WIFI_'+args.action.upper(),flush=True);return 0
    except (Exception,KeyboardInterrupt) as e:
        rec.update(Status='failed',Error=str(e),FailedAtUTC=fb.now());save(name,rec)
        print('PS Wi-Fi stopped: '+str(e),flush=True);return 1
if __name__=='__main__': raise SystemExit(main())
