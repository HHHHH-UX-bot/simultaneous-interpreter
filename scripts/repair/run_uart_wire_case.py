import hashlib,json,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'scripts/repair')]
import run_z7_nano as base
from uart_wire_decode import analyze

name=sys.argv[1]
seconds={'short':5,'burst':8,'paced':14}[name]
app=ROOT/'output/uart_wire_test'
out=app/name;out.mkdir(exist_ok=True)
payload=app/(name+'.bin');spec=json.loads((app/(name+'.json')).read_text())
assert hashlib.sha256(payload.read_bytes()).hexdigest()==spec['sha256']
load=json.loads((app/'load.json').read_text())
assert base.sha(app/'uart_wire_test.elf')==load['elf_sha256']
assert base.sha(ROOT/'scripts/repair/uart_wire_test.tcl')==load['loader_sha256']
tcl=ROOT/'scripts/repair/uart_wire_test.tcl'
dll=ROOT/'tmp/logic_analyzer_20261020/runtime/PulseView-win64-portable/libusb-1.0.dll'
with base.hardware_lock():
    base.run([base.XSDB,tcl,'prepare','210241277120',out/'prepare.json',payload,spec['bytes'],spec['paced']],out/'prepare','PASS_UART_WIRE_PREPARE',20)
    with (out/'capture.log').open('w',encoding='utf-8') as log:
        process=subprocess.Popen([sys.executable,'-B','-X','utf8',str(ROOT/'scripts/repair/slogic_capture.py'),
          '--dll',str(dll),'--output',str(out/'capture'),'--seconds',str(seconds)],stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
        try:
            deadline=time.monotonic()+5
            while 'CAPTURE_ARMED' not in (out/'capture.log').read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Capture not armed')
                time.sleep(.02)
            base.run([base.XSDB,tcl,'start','210241277120',out/'start.json'],out/'start','PASS_UART_WIRE_START',15)
            process.wait(timeout=seconds+8)
            if process.returncode:raise RuntimeError('USB capture failed; inspect capture.log')
        finally:
            if process.poll() is None:process.kill();process.wait()
    base.run([base.XSDB,tcl,'result','210241277120',out/'board.json'],out/'result','PASS_UART_WIRE_RESULT',15)
board=json.loads((out/'board.json').read_text());report=board['report']
capture=json.loads((out/'capture/capture.json').read_text());raw=(out/'capture/samples_2ch.bin').read_bytes()
assert len(raw)==capture['bytes'] and hashlib.sha256(raw).hexdigest()==capture['sha256']
decoded=analyze(raw,payload.read_bytes(),capture['sample_rate_hz'])
(out/'decoded.json').write_text(json.dumps(decoded,indent=2)+'\n')
passed=report[1]==3 and report[8]==0 and report[7]==spec['bytes'] and report[11]==spec['bytes'] and report[12]==0 and report[14]==spec['fnv1a'] and not any(board['uart'][2:4]) and decoded['status']=='passed'
result={'status':'passed' if passed else 'failed','case':name,'payload':spec,'elf_sha256':load['elf_sha256'],'bitstream_sha256':load['bitstream_sha256'],'board':board,'decode':decoded,'capture':capture}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
raise SystemExit(0 if passed else 1)
