"""Simulate the default UART and optional wire-fingerprint diagnostic."""
import hashlib,json,subprocess
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BIN=Path('D:/AMDDesignTools/2026.1/Vivado/bin')
WORK=ROOT/'tmp/tls_uart_wire_sim';OUT=ROOT/'output/tls_alert_20261020/wire_sim'
INPUTS=['rtl/wifi_byte_fifo.v','rtl/wifi_uart_phy.v','rtl/wifi_uart_tx_trace.v',
        'rtl/wifi_uart_axi.v','sim/tb_wifi_uart_axi.sv','sim/tb_wifi_uart_trace.sv']
WORK.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
def run(tool,args,name,timeout=120):
    r=subprocess.run([str(BIN/tool),*map(str,args)],cwd=WORK,capture_output=True,timeout=timeout)
    (OUT/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout
run('xvlog.bat',['--sv',*[ROOT/p for p in INPUTS]],'compile')
for top,marker in [('tb_wifi_uart_axi',b'PASS_WIFI_UART'),('tb_wifi_uart_trace',b'PASS_WIFI_TX_TRACE')]:
    run('xelab.bat',['work.'+top,'-s',top,'--debug','off'],top+'_elab')
    result=run('xsim.bat',[top,'-runall','-log',OUT/(top+'_xsim.log')],top,180)
    if marker not in result:raise RuntimeError('Missing completion marker: '+top)
    print('PASS',top,flush=True)
assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
(OUT/'validation.json').write_text(json.dumps({'status':'passed','utc':datetime.now(timezone.utc).isoformat(),
    'scope':'RTL simulation, not physical FPGA or ESP receive acceptance','input_sha256':hashes},indent=2)+'\n')
