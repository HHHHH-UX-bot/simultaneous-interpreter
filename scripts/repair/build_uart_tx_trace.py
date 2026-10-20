"""Build the optional UART wire diagnostic in a separate output directory."""
import hashlib,json,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/z7_nano/audio_tx_trace';WORK=ROOT/'tmp/tls_uart_wire_build'
OUT.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
sim=json.loads((ROOT/'output/tls_alert_20261020/wire_sim/validation.json').read_text())
assert sim['status']=='passed' and all(sha(ROOT/p)==v for p,v in sim['input_sha256'].items())
baseline=json.loads((ROOT/'output/z7_nano/audio_loopback/build.json').read_text())
prepared=subprocess.run([sys.executable,'-B',str(ROOT/'scripts/prepare_z7_nano.py')],cwd=ROOT,capture_output=True)
(OUT/'prepare.log').write_bytes(prepared.stdout+prepared.stderr);prepared.check_returncode()
assert b'PASS_Z7_NANO_PREPARE' in prepared.stdout
inputs={x['File'] for x in baseline['LogicHashes']}|{'rtl/wifi_uart_tx_trace.v','scripts/repair/build_uart_tx_trace.py'}
inputs.update('output/z7_nano/config/'+name for name in ('board.tcl','ps7_config.tcl','hdmi.xdc','audio.xdc'))
hashes={p:sha(ROOT/p) for p in sorted(inputs)}
(OUT/'build_inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
command=['D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat','-mode','batch','-notrace',
         '-source',str(ROOT/'scripts/build_z7_nano_audio.tcl'),'-log',str(OUT/'vivado.log'),
         '-journal',str(WORK/'vivado.jou'),'-tclargs','build',str(OUT),'tx-trace']
with (OUT/'console.log').open('wb') as log:
    r=subprocess.run(command,cwd=WORK,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
r.check_returncode()
assert 'PASS_Z7_NANO_AUDIO_BUILD' in (OUT/'console.log').read_text(errors='replace')
assert hashes=={p:sha(ROOT/p) for p in sorted(inputs)}
record=json.loads((OUT/'implementation.json').read_text())
assert record['usr_access']==0x5a375458 and record['setup_slack_ns']>=0 and record['hold_slack_ns']>=0
record.update(status='passed',utc=datetime.now(timezone.utc).isoformat(),input_sha256=hashes,
              bitstream_sha256=sha(OUT/'z7_nano_audio_top.bit'),scope='Built TX_TRACE PL, not programmed or physically accepted')
(OUT/'build.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS_UART_TX_TRACE_BUILD',record['bitstream_sha256'],flush=True)
