"""Temporarily start the owned LAN API and verify real and invalid requests."""
import argparse
import hashlib
import http.client
import io
import json
import random
import secrets
import socket
import struct
import subprocess
import sys
import threading
import time
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'software/model_backend'))
import runtime
from engine import MAX_WAV_BYTES


def make_wav(raw):
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(16000)
        audio.writeframes(raw)
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--allow-board', help='Allow this board IP, without claiming it connected')
    args = parser.parse_args()
    report = {'Status': 'pending', 'StartedAtUTC': runtime.now(), 'Checks': [],
              'URL': f'http://{args.bind}:{args.port}',
              'Scope': 'Owned LAN listener and direct PC HTTP requests; no ESP client/audio/microphone/touch validation'}
    runtime.save('service/pilot.json', report)
    token = secrets.token_hex(32)
    checks = report['Checks']

    def require(name, condition):
        if not condition:
            report['FailedCheck'] = name
            raise AssertionError(name)
        checks.append(name)

    def request(method, path='/v1/interpret', data=None, headers=None):
        connection = http.client.HTTPConnection(args.bind, args.port, timeout=30)
        try:
            connection.request(method, path, body=data, headers=headers or {})
            response = connection.getresponse()
            raw = response.read()
            return response.status, json.loads(raw) if raw else None
        finally:
            connection.close()

    def post(data, *, session='controlled', segment=1, revision=1, extra=None):
        headers = {'X-API-Key': token, 'Content-Type': 'audio/wav',
                   'X-Session-ID': session, 'X-Segment-ID': str(segment), 'X-Revision': str(revision)}
        if extra:
            headers.update(extra)
        return request('POST', data=data, headers=headers)

    peers = [args.bind]
    if args.allow_board:
        peers.append(args.allow_board)
    command = [sys.executable, '-B', '-X', 'utf8', str(ROOT / 'scripts/run_translation_service.py'),
               '--bind', args.bind, '--port', str(args.port), '--token-stdin']
    for peer in peers:
        command += ['--allow-peer', peer]
    temp = ROOT / 'tmp/model_backend/service'
    temp.mkdir(parents=True, exist_ok=True)
    child = None
    async_thread = None
    try:
        with (temp / 'stdout.log').open('w', encoding='utf-8') as stdout, \
                (temp / 'stderr.log').open('w', encoding='utf-8') as stderr:
            child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
                                     text=True, encoding='utf-8',
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            child.stdin.write(json.dumps({'api_token': token}) + '\n')
            child.stdin.flush()
            deadline = time.monotonic() + 60
            active = None
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError('Owned service exited during startup')
                try:
                    active = json.loads((runtime.OUT / 'service/runtime.json').read_text(encoding='utf-8'))
                    if active['PID'] == child.pid and active['Status'] == 'running':
                        break
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(0.2)
            else:
                raise TimeoutError('Owned service startup timeout')
            require('Runtime identity belongs to this service process', active['PID'] == child.pid)
            report['Runtime'] = active
            status, health = request('GET', '/v1/health')
            require('LAN health ready and CPU format limits match', status == 200 and health['ready']
                    and health['device'] == 'cpu' and health['maximum_bytes'] == MAX_WAV_BYTES)
            require('Unknown endpoint rejected', request('GET', '/unknown')[0] == 404)
            silence = make_wav(b'\x00\x00' * 16000)
            require('Missing API key rejected', request('POST', data=b'')[0] == 401)
            require('Incorrect API key rejected', post(silence, extra={'X-API-Key': '0' * 64})[0] == 401)
            require('Wrong media type rejected', post(b'', extra={'Content-Type': 'application/json'})[0] == 415)
            require('Chunked input rejected', post(b'', extra={'Transfer-Encoding': 'chunked'})[0] == 400)
            require('Oversized declared audio rejected before reading',
                    post(b'', extra={'Content-Length': str(MAX_WAV_BYTES + 1)})[0] == 413)
            require('Bad session identifier rejected', post(b'', session='bad session')[0] == 400)
            require('Zero segment rejected', post(b'', segment=0)[0] == 400)
            require('Overflowing revision rejected', post(b'', revision=0x100000000)[0] == 400)
            require('Malformed WAV rejected', post(b'x' * 44)[0] == 400)
            malformed_chunk = bytes.fromhex(
                '5249464628000000574156454a554e4bffffffff00000000000000000000000000000000000000000000000000000000')
            require('Out-of-container RIFF seek returns structured invalid_wav',
                    post(malformed_chunk) == (400, {'protocol': 'interpret-v1', 'error': 'invalid_wav'}))
            slow = []
            try:
                for _ in range(4):
                    connection = socket.create_connection((args.bind, args.port), timeout=2)
                    connection.sendall(b'GET /v1/health HTTP/1.1\r\nHost: ')
                    slow.append(connection)
                for _ in range(4):
                    time.sleep(1.55)  # Active bytes defeat a receive-only timeout.
                    for connection in slow:
                        try:
                            connection.sendall(b'x')
                        except OSError:
                            pass
                require('Anonymous slow headers release all workers at the total deadline',
                        request('GET', '/v1/health')[0] == 200)
            finally:
                for connection in slow:
                    connection.close()

            reference = json.loads((runtime.OUT / 'fixtures/reference.json').read_text(encoding='utf-8'))
            path = ROOT / reference['Audio']
            require('Controlled input retains its known hash', runtime.sha(path) == reference['AudioSHA256'])
            audio = path.read_bytes()
            outcome = {}
            def infer():
                try:
                    outcome['response'] = post(audio)
                except Exception as error:
                    outcome['error_type'] = type(error).__name__
            async_thread = threading.Thread(target=infer)
            async_thread.start()
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                _, health = request('GET', '/v1/health')
                if health['inference_busy']:
                    break
                if not async_thread.is_alive():
                    raise RuntimeError('Inference ended before overload check')
                time.sleep(0.02)
            else:
                raise TimeoutError('Inference did not start')
            require('Health stays responsive during real inference', health['ready'])
            require('Concurrent audio is explicitly rejected without queueing',
                    post(silence, session='overload')[0] == 429)
            async_thread.join(timeout=40)
            require('Real inference completed within test deadline', not async_thread.is_alive())
            status, result = outcome['response']
            baseline = json.loads((runtime.OUT / 'file_baseline.json').read_text(encoding='utf-8'))
            require('Actual API ASR matches the controlled English reference',
                    status == 200 and result['transcript'] == reference['Source'])
            require('Actual API MT preserves the real model output',
                    result['translation'] == baseline['Result']['Chinese'] and not result['no_speech'])
            report['RealResult'] = result
            report['AudioSHA256'] = reference['AudioSHA256']
            duplicate_status, duplicate = post(audio)
            require('Identical retry returns the exact same completed result',
                    duplicate_status == 200 and duplicate == result)
            _, health = request('GET', '/v1/health')
            require('Retry did not run the models twice', health['completed_requests'] == 1)
            changed = audio[:-2] + struct.pack('<h', struct.unpack('<h', audio[-2:])[0] ^ 1)
            require('Changed audio with the same identity is rejected', post(changed)[0] == 409)
            status, zero = post(silence, revision=2)
            require('Exact silence yields no invented source or Chinese text', status == 200
                    and zero['no_speech'] and zero['transcript'] == zero['translation'] == '')
            require('Stale revision is rejected after newer result', post(audio)[0] == 409)

            rng = random.Random(7010)
            noise = make_wav(b''.join(struct.pack('<h', rng.randint(-128, 128)) for _ in range(3 * 16000)))
            status, noise_result = post(noise, session='synthetic_noise')
            report['SyntheticNoise'] = {'AudioSHA256': hashlib.sha256(noise).hexdigest(),
                                        'Result': noise_result, 'StatusCode': status}
            require('Controlled quiet white noise does not create a subtitle', status == 200
                    and noise_result['no_speech'] and noise_result['translation'] == '')
            for index in range(6):
                require('Bounded session slot ' + str(index),
                        post(silence, session='slot_' + str(index))[0] == 200)
            require('Ninth session is rejected rather than evicting retry history',
                    post(silence, session='extra_session')[0] == 409)
            report.update(Status='passed', VerifiedAtUTC=runtime.now())
    except Exception as error:
        report.update(Status='failed', FailedAtUTC=runtime.now(), ErrorType=type(error).__name__)
        print('Service pilot stopped: ' + type(error).__name__, flush=True)
    finally:
        if async_thread is not None:
            async_thread.join(timeout=40)
        if child is not None:
            try:
                if child.poll() is None:
                    child.stdin.write('STOP\n')
                    child.stdin.flush()
                    child.wait(timeout=20)
            except (OSError, subprocess.TimeoutExpired):
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=10)
            finally:
                child.stdin.close()
            report['OwnedServiceStopped'] = child.poll() is not None
            probe = socket.socket()
            probe.settimeout(1)
            report['TestPortClosed'] = probe.connect_ex((args.bind, args.port)) != 0
            probe.close()
        report['SourceHashes'] = {
            str(path.relative_to(ROOT)).replace('\\', '/'): runtime.sha(path)
            for path in (Path(__file__), ROOT / 'scripts/run_translation_service.py',
                         ROOT / 'software/model_backend/service.py', ROOT / 'software/model_backend/engine.py',
                         ROOT / 'software/model_backend/runtime.py')
        }
        report['APIKeyAbsentFromOutput'] = all(token not in path.read_text(encoding='utf-8')
                                              for path in (temp / 'stdout.log', temp / 'stderr.log'))
        if (not report.get('OwnedServiceStopped') or not report.get('TestPortClosed')
                or not report.get('APIKeyAbsentFromOutput')):
            report['Status'] = 'failed'
        runtime.save('service/pilot.json', report)
        if report['Status'] == 'passed':
            print('PASS_LOCAL_TRANSLATION_SERVICE ' + str(len(checks)), flush=True)
    return 0 if report['Status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
