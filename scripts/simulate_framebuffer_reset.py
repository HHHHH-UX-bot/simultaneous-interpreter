"""Generate the actual block design and verify its reset IP/AXI release path."""
import argparse,hashlib,json
from datetime import datetime,timezone
from pathlib import Path
import run_ps_bringup as runner

ROOT=Path(__file__).resolve().parents[1]
INPUTS=('scripts/build_framebuffer.tcl','rtl/framebuffer_video.v','sim/tb_framebuffer_reset.v',
        'scripts/simulate_framebuffer_reset.py','scripts/run_ps_bringup.py')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes(): return {p:sha(ROOT/p) for p in INPUTS}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--vivado',type=Path,required=True)
    args=parser.parse_args()
    out=ROOT/'output/framebuffer'; temp=ROOT/'tmp/framebuffer_reset_sim'
    out.mkdir(parents=True,exist_ok=True); temp.mkdir(parents=True,exist_ok=True)
    runner.OUT=out; runner.TEMP=temp
    record={'Status':'pending','SourceHashes':hashes()}
    try:
        runner.command([args.vivado,'-mode','batch','-nojournal','-log',temp/'design.log',
            '-source',ROOT/'scripts/build_framebuffer.tcl','-tclargs','design'],
            'reset_simulation/design.log','PASS_FRAMEBUFFER_DESIGN',timeout=240)
        generated=out/'vivado/framebuffer.gen/sources_1/bd/ps_display/ip/ps_display_reset_0/synth/ps_display_reset_0.vhd'
        vendor=args.vivado.parent.parent/'data/ip/xilinx/proc_sys_reset_v5_0/hdl/proc_sys_reset_v5_0_rfs.vhd'
        netlist=out/'vivado/framebuffer.gen/sources_1/bd/ps_display/synth/ps_display.v'
        # The build also asserts the BD connection; check the generated wiring itself.
        text=netlist.read_text(encoding='utf-8')
        if '.aux_reset_in(one_dout)' not in text or '.mb_debug_sys_rst(zero_dout)' not in text:
            raise RuntimeError('Generated auxiliary reset idle level does not match the fix')
        bin=args.vivado.parent
        # Compile the vendor model into the task's cache, never its installation.
        mapping=(args.vivado.parent.parent/'data/xsim/xsim.ini').read_text(encoding='utf-8')
        mapping=mapping.replace('proc_sys_reset_v5_0_17=$RDI_DATADIR/xsim/ip/proc_sys_reset_v5_0_17',
                                'proc_sys_reset_v5_0_17=xsim.dir/proc_sys_reset_v5_0_17')
        (temp/'xsim.ini').write_text(mapping,encoding='utf-8')
        runner.command([bin/'xvhdl.bat','--relax','--work','proc_sys_reset_v5_0_17',vendor],
                       'reset_simulation/vendor_compile.log',timeout=120)
        runner.command([bin/'xvhdl.bat','--relax',generated],
                       'reset_simulation/generated_compile.log',timeout=120)
        global_model=args.vivado.parent.parent/'data/verilog/src/glbl.v'
        runner.command([bin/'xvlog.bat','--sv',ROOT/'rtl/framebuffer_video.v',ROOT/'sim/tb_framebuffer_reset.v',global_model],
                       'reset_simulation/tb_compile.log',timeout=120)
        runner.command([bin/'xelab.bat','--relax','-L','proc_sys_reset_v5_0_17','-L','xpm',
                       '-L','unisims_ver','tb_framebuffer_reset','glbl','-s','framebuffer_reset_sim'],
                       'reset_simulation/elaborate.log',timeout=180)
        text=runner.command([bin/'xsim.bat','framebuffer_reset_sim','-runall'],
                           'reset_simulation/run.log','PASS_FRAMEBUFFER_RESET_INTEGRATION',timeout=120)
        if 'PASS_RESET_NEGATIVE_CONTROL' not in text or 'Fatal:' in text or 'ERROR:' in text:
            raise RuntimeError('Reset regression did not pass')
        if hashes()!=record['SourceHashes']: raise RuntimeError('Sources changed during reset simulation')
        record.update(Status='passed',GeneratedResetSHA256=sha(generated),VendorModelSHA256=sha(vendor),
            VerifiedAtUTC=datetime.now(timezone.utc).isoformat(),
            Cases=['low auxiliary input holds resets','high auxiliary input releases resets',
                   'AXI ID/dimensions readable after release','external reset and release','clock loss and recovery'],
            AuxiliaryIdleLevel=1,AuxiliaryActiveHigh=0,ExternalActiveHigh=1)
        print('PASS_FRAMEBUFFER_RESET_INTEGRATION',flush=True)
    except (Exception,KeyboardInterrupt) as error:
        record.update(Status='failed',Error=str(error)); print(error,flush=True)
    finally:
        (out/'reset_simulation.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    return 0 if record['Status']=='passed' else 1

if __name__=='__main__': raise SystemExit(main())
