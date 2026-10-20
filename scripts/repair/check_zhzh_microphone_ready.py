"""Observe public ASR counters for the already loaded microphone ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument('--directory', type=Path, required=True)
a = p.parse_args()
a.directory.mkdir(parents=True, exist_ok=True)
build = a.directory.parent
metadata = json.loads((build / 'build.json').read_text(encoding='utf-8'))
expected = hashlib.sha256((build / 'cloud_app.elf').read_bytes()).hexdigest()
assert metadata['ELFSHA256']==expected and metadata['Mode']=='zhzh' and not metadata['Selftest']
samples = []
for i in range(4):
    path = a.directory / ('sample_%03d.json' % i)
    subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('board_snapshot.py')),
                    '--symbols', str(build / 'symbols.log'), '--elf', str(build / 'cloud_app.elf'),
                    '--output', str(path)], check=True, stdout=subprocess.DEVNULL, timeout=45)
    data = json.loads(path.read_text(encoding='utf-8'))
    c = data['counters']
    n, h, u, t = c['network_report'], c['cloud_report'], c['upload_diag'], c['arm_tls_diag']
    row = {'utc':data['utc'], 'elf_sha256':expected, 'network_phase':n[2], 'network_error':n[3],
           'cloud_phase':h[4], 'cloud_error':h[5], 'asr_finals':h[7], 'committed':h[9],
           'client_phase':c['client'][0], 'network_faults':c['duplex_diag'][34],
           'upload_pages':u[2], 'upload_drops':u[1], 'upload_queue_peak':u[3],
           'capture_overflows':c['capture'][1], 'tls_phase':t[1], 'tls_error':t[2],
           'tls_sessions':t[9], 'tls_failures':t[10], 'tls_overflows':t[7],
           'uart_overflow':n[12], 'uart_framing':n[13], 'usb_power':(c['usb_power'][0]>>12)&1}
    samples.append(row)
    print(json.dumps(row), flush=True)
    if i<3:
        time.sleep(10)
faults = ('network_error','cloud_error','network_faults','upload_drops','capture_overflows',
          'tls_error','tls_failures','tls_overflows','uart_overflow','uart_framing')
passed = all(x['network_phase']==4 and x['cloud_phase']==1 and x['client_phase']==4 and x['tls_phase']==2
             and x['tls_sessions']==1 and x['usb_power']==1 and not any(x[k] for k in faults) for x in samples)
passed = passed and samples[-1]['upload_pages']>samples[0]['upload_pages']
record = {'status':'passed' if passed else 'failed', 'scope':'actual microphone capture/upload, verified cloud connection and screen power; not recognition quality or long-term stability', 'samples':samples}
(a.directory / 'summary.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
print('ZHZH_MICROPHONE_READY_CHECK='+record['status'])
sys.exit(0 if passed else 1)
