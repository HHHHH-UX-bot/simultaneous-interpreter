"""Run local models once and expose the bounded, memory-only inference API."""
import argparse
import getpass
import json
import os
import re
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'software/model_backend'))
import runtime
from engine import LocalInterpreter, VAD_OPTIONS
from service import InterpreterServer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1', help='One explicit private IPv4 address')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--allow-peer', action='append', required=True)
    parser.add_argument('--token-stdin', action='store_true',
                        help='Read private JSON configuration from a parent process pipe')
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('Port must be within 1024..65535')
    try:
        if args.token_stdin:
            line = sys.stdin.readline(256)
            token = json.loads(line)['api_token']
            del line
        else:
            token = getpass.getpass('Runtime API key (64 hexadecimal characters, hidden): ')
        if not isinstance(token, str) or not re.fullmatch('[0-9a-f]{64}', token):
            raise ValueError('Invalid runtime API key')
        interpreter = LocalInterpreter(threads=8)
        server = InterpreterServer((args.bind, args.port), interpreter, args.allow_peer, token)
        del token
        record = {
            'Status': 'running', 'StartedAtUTC': runtime.now(), 'PID': os.getpid(),
            'URL': f'http://{args.bind}:{args.port}', 'AllowedPeers': sorted(server.peers),
            'Protocol': 'interpret-v1', 'Device': 'cpu',
            'VAD': interpreter.vad_asset, 'VADOptions': VAD_OPTIONS,
            'SourceHashes': {
                str(path.relative_to(ROOT)).replace('\\', '/'): runtime.sha(path)
                for path in (Path(__file__), ROOT / 'software/model_backend/service.py',
                             ROOT / 'software/model_backend/engine.py', ROOT / 'software/model_backend/runtime.py')
            },
            'ModelManifestSHA256': runtime.sha(runtime.OUT / 'models/manifest.json'),
            'Scope': 'Bounded file API only; no microphone, streaming, board client or touch validation',
        }
        runtime.save('service/runtime.json', record)
        if args.token_stdin:
            # Closing the parent's pipe also stops its owned service; no orphan
            # listener remains after a helper exits or a session is interrupted.
            def control_pipe():
                for line in sys.stdin:
                    if line.strip() == 'STOP':
                        break
                server.shutdown()
            threading.Thread(target=control_pipe, daemon=True).start()
        print('READY_LOCAL_INTERPRETER ' + record['URL'], flush=True)
        try:
            server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
            record.update(Status='stopped', StoppedAtUTC=runtime.now())
            runtime.save('service/runtime.json', record)
        return 0
    except Exception as error:
        # Private input and model/transport exception strings are not logged.
        print('Translation service stopped: ' + type(error).__name__, flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
