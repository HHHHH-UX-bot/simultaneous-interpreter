"""Offline AT/TCP framing and UART recovery checks; never accesses hardware."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--gcc', required=True, type=Path)
    args = p.parse_args()
    out = ROOT / 'output/repair_20261020/transport'
    temp = ROOT / 'tmp/repair_20261020/transport'
    out.mkdir(parents=True, exist_ok=True)
    temp.mkdir(parents=True, exist_ok=True)
    network = ['software/network_ui/network.c', 'software/network_ui/wifi_recover.c',
               'software/network_ui/protocol.c', 'software/subtitle_ui/ui_model.c',
               'output/subtitle_ui/ui_assets.c']
    stream = ['-DZ7_NETWORK_CAPABILITIES', '-DZ7_STREAM_TRANSPORT', '-DZ7_UART_1M_STREAM']
    cases = [
        ('runtime', [], 'network_runtime_test.c'),
        ('legacy_tls', ['-DZ7_NETWORK_CAPABILITIES'], 'network_tls_test.c'),
        ('stream_tls', stream, 'network_tls_test.c'),
        ('duplex', stream, 'network_duplex_test.c'),
        ('send_recovery', stream, 'network_speech_recovery_test.c'),
        ('passive_receive', stream, 'network_speech_passive_test.c'),
        ('receive_first', stream + ['-DZ7_SSL_DRAIN_FIRST'], 'network_speech_passive_test.c'),
        ('zero_plaintext_read', stream + ['-DZ7_SSL_DRAIN_FIRST', '-DZ7_SSL_NO_LENGTH_QUERY'], 'network_speech_no_length_test.c'),
        ('raw_tls', stream + ['-DZ7_TLS_PASSTHROUGH'], 'network_tls_raw_test.c'),
        ('arm_tcp', stream + ['-DZ7_TLS_PASSTHROUGH', '-DZ7_ARM_TLS'], 'network_tls_raw_test.c'),
        ('arm_tcp_trace', stream + ['-DZ7_TLS_PASSTHROUGH', '-DZ7_ARM_TLS', '-DZ7_TLS_TRACE'], 'network_tls_raw_test.c'),
        ('arm_tcp_packets', stream + ['-DZ7_ARM_TLS'], 'network_arm_packet_test.c'),
        ('arm_tcp_receive_probe', stream + ['-DZ7_ARM_TLS', '-DZ7_PASSIVE_RECV_PROBE'], 'network_arm_packet_test.c'),
        ('framing_recovery_1m', stream, 'network_framing_recovery_test.c'),
        ('framing_recovery_921600', [d for d in stream if d != '-DZ7_UART_1M_STREAM'] + ['-DZ7_UART_LEGACY_STREAM'], 'network_framing_recovery_test.c'),
    ]
    checks = [(name, defines, network + ['tests/' + fixture]) for name, defines, fixture in cases]
    for name, defines in [('recover_1m', ['-DZ7_UART_1M_STREAM']), ('recover_921600', ['-DZ7_UART_LEGACY_STREAM'])]:
        checks.append((name, defines, ['tests/wifi_recover_test.c', 'software/network_ui/wifi_recover.c']))
    results = {}
    inputs = set()
    for name, defines, sources in checks:
        exe = temp / (name + '.exe')
        cmd = [str(args.gcc), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *defines,
               '-I', str(ROOT / 'software/network_ui'), '-I', str(ROOT / 'software/subtitle_ui'),
               *[str(ROOT / s) for s in sources], '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, timeout=60)
        (out / (name + '_build.log')).write_bytes(build.stdout + build.stderr)
        build.check_returncode()
        test = subprocess.run([str(exe)], capture_output=True, timeout=30)
        (out / (name + '_native.log')).write_bytes(test.stdout + test.stderr)
        test.check_returncode()
        results[name] = test.stdout.decode().strip()
        inputs.update(sources)
        print(name, 'passed', flush=True)
    inputs.update(['software/network_ui/network.h', 'software/network_ui/tls_tx_trace.h', 'software/network_ui/wifi_recover.h', 'software/ps_wifi/wifi_at_probe.h'])
    record = {'status':'passed', 'utc':datetime.now(timezone.utc).isoformat(),
              'scope':'native mocked ESP UART/AT/TCP framing and recovery; no board or model quality measurement',
              'tests':results, 'input_sha256':{s:hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in sorted(inputs)}}
    (out / 'validation.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
