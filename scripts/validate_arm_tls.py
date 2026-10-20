"""Online certificate/integrity checks for the actual opt-in TLS engine.

Requires installed Windows GCC and network access, but no cloud API key, board,
microphone or dependency installation. Does not test model responses.
"""
import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--gcc', type=Path, required=True)
    args = p.parse_args()
    out = ROOT / 'output/repair/arm_tls_native'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-O2', '-DZ7_TLS_TRACE', '-ffunction-sections', '-fdata-sections',
             '-DMBEDTLS_CONFIG_FILE="tls_config.h"',
             '-I' + str(ROOT / 'software/network_ui'),
             '-I' + str(ROOT / 'third_party/mbedtls/include')]
    objects = []
    for source in sorted((ROOT / 'third_party/mbedtls/library').glob('*.c')):
        obj = out / (source.stem + '.o')
        result = subprocess.run([str(args.gcc), *flags, '-c', str(source), '-o', str(obj)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        objects.append(str(obj))
    for name, extra in [('normal', []), ('wrong_host', ['-DTLS_TEST_WRONG_HOST'])]:
        command = [*flags, *extra, str(ROOT / 'tests/arm_tls_host_test.c'),
                   str(ROOT / 'software/network_ui/arm_tls.c'), *objects,
                   '-Wl,--gc-sections', '-lws2_32', '-lbcrypt', '-o', str(out / (name + '.exe'))]
        response = out / (name + '.rsp')
        response.write_text('\n'.join('"' + a.replace('\\', '\\\\').replace('"', '\\"') + '"'
                                      for a in command) + '\n', encoding='utf-8')
        result = subprocess.run([str(args.gcc), '@' + str(response)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
    cases = []
    for exe, option in [('normal', ''), ('normal', 'full_record'), ('wrong_host', ''), ('normal', 'expired'),
                        ('normal', 'noseed'), ('normal', 'corrupt')]:
        result = subprocess.run([str(out / (exe + '.exe')), *([option] if option else [])],
                                capture_output=True, text=True, timeout=40)
        case = {'test': exe + ':' + option, 'returncode': result.returncode,
                'result': json.loads(result.stdout)}
        cases.append(case)
        print(json.dumps(case), flush=True)
    inputs = ['software/network_ui/arm_tls.c', 'software/network_ui/arm_tls.h',
              'software/network_ui/tls_config.h', 'software/network_ui/tls_root.h',
              'software/network_ui/tls_tx_trace.h', 'tests/arm_tls_host_test.c', 'third_party/mbedtls/SOURCE.json']
    record = {'passed': all(c['returncode'] == 0 for c in cases), 'cases': cases,
              'utc': datetime.now(timezone.utc).isoformat(),
              'scope': 'real TLS service, fragmented TCP, authentication and ciphertext integrity; no API key',
              'input_sha256': {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in inputs}}
    (out / 'validation.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    if not record['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
