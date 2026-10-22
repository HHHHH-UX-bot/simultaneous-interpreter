"""Build the optional PL Ethernet platform using the accepted audio simulation."""
import argparse,json,sys
from pathlib import Path
import run_audio_loopback as audio
ROOT=audio.ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/Vivado/2018.3/bin/vivado.bat'));a=p.parse_args()
    sim=audio.read('simulation.json');assert sim['Status']=='passed'
    audio.check(sim['LogicHashes'],audio.RTL_INPUTS);audio.check(sim['TestHashes'],audio.SIM_INPUTS)
    inputs=audio.BUILD_INPUTS+['scripts/pl_ethernet_bd.tcl','hardware/z7_nano/pl_ethernet.xdc','hardware/z7_nano/pl_ethernet_clock.xdc','scripts/build_remote_display.py']
    before=audio.hashes(inputs);out=ROOT/'output/z7_nano/remote_display';out.mkdir(parents=True,exist_ok=True)
    (out/'build_inputs.json').write_text(json.dumps(before,indent=2)+'\n')
    audio.run(Path(sys.executable),['-B',ROOT/'scripts/prepare_z7_nano.py'],out/'prepare.log','PASS_Z7_NANO_PREPARE')
    audio.run(a.vivado,['-mode','batch','-notrace','-source',ROOT/'scripts/build_z7_nano_audio.tcl','-log',out/'build.log','-journal',out/'build.jou','-tclargs','build',out,'remote-ui'],out/'console.log','PASS_Z7_NANO_AUDIO_BUILD')
    audio.check(before,inputs);d=json.loads((out/'implementation.json').read_text())
    d.update(Status='passed',LogicHashes=before,BitstreamSHA256=audio.sha((out/'z7_nano_audio_top.bit').relative_to(ROOT).as_posix()),EthernetBase='0x40e00000',EthernetMbps=100)
    (out/'build.json').write_text(json.dumps(d,indent=2)+'\n');print('PASS_REMOTE_DISPLAY_BUILD')
if __name__=='__main__':main()
