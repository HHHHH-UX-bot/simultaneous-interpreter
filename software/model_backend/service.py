"""Bounded LAN file-inference API. Audio and results stay in memory."""
import hashlib
import hmac
import ipaddress
import json
import re
import socket
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from engine import MAX_SECONDS, MAX_WAV_BYTES, decode_wav
HEADER_DEADLINE_SECONDS = 5
BODY_DEADLINE_SECONDS = 15


class InputDeadline:
    """A total ingress deadline, independent of per-receive socket timeouts."""
    def __init__(self, connection):
        self.connection = connection
        self.lock = threading.Lock()
        self.timer = None
        self.generation = 0
        self.expired = False

    def arm(self, seconds):
        with self.lock:
            if self.expired:
                raise TimeoutError('Input deadline expired')
            self.generation += 1
            generation = self.generation
            if self.timer is not None:
                self.timer.cancel()
            self.timer = threading.Timer(seconds, self.expire, (generation,))
            self.timer.daemon = True
            self.timer.start()

    def expire(self, generation):
        with self.lock:
            if generation != self.generation:
                return
            self.expired = True
            self.generation += 1
            # shutdown interrupts a makefile read even while it owns a socket
            # reference. A plain close from another thread would not do that.
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def cancel(self):
        with self.lock:
            self.generation += 1
            if self.timer is not None:
                self.timer.cancel()


class InterpreterServer(ThreadingHTTPServer):
    address_family = socket.AF_INET
    daemon_threads = True
    allow_reuse_address = False
    request_queue_size = 4

    def __init__(self, address, interpreter, peers, token):
        address_ip = ipaddress.IPv4Address(address[0])
        private = [ipaddress.IPv4Network(cidr) for cidr in
                   ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '127.0.0.0/8')]
        if not any(address_ip in subnet for subnet in private):
            raise ValueError('Bind one explicit private or loopback IPv4 address')
        if not re.fullmatch('[0-9a-f]{64}', token):
            raise ValueError('A 32-byte runtime API key is required')
        self.peers = {str(ipaddress.IPv4Address(peer)) for peer in peers}
        if not self.peers:
            raise ValueError('An explicit client IP allowlist is required')
        self.token = token
        self.interpreter = interpreter
        self.workers = threading.BoundedSemaphore(4)
        self.inference = threading.Lock()
        self.sessions = {}  # At most eight sessions, one completed result per session.
        self.counter_lock = threading.Lock()
        self.completed = 0
        super().__init__(address, InterpreterHandler)

    def process_request(self, request, client_address):
        if client_address[0] not in self.peers:
            self.reject_connection(request, 403, b'Forbidden')
            return
        if not self.workers.acquire(blocking=False):
            self.reject_connection(request, 503, b'Busy')
            return
        request.settimeout(5)
        try:
            super().process_request(request, client_address)
        except Exception:
            self.workers.release()
            self.shutdown_request(request)
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.workers.release()

    def handle_error(self, request, client_address):
        # No traceback, request body, authentication header, or voice transcript.
        pass

    @staticmethod
    def reject_connection(request, status, label):
        try:
            request.settimeout(1)
            request.sendall(b'HTTP/1.0 ' + str(status).encode() + b' ' + label +
                            b'\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
        except OSError:
            pass
        finally:
            request.close()


class InterpreterHandler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    server_version = 'LocalInterpreter/1'
    sys_version = ''

    def setup(self):
        super().setup()
        self.input_deadline = InputDeadline(self.connection)
        self.input_deadline.arm(HEADER_DEADLINE_SECONDS)

    def finish(self):
        self.input_deadline.cancel()
        super().finish()

    def log_message(self, format, *args):
        pass

    def send_json(self, status, value):
        body = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        if status in (429, 503):
            self.send_header('Retry-After', '1')
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def fail(self, status, code):
        self.input_deadline.cancel()
        self.send_json(status, {'protocol': 'interpret-v1', 'error': code})

    def one_header(self, name):
        values = self.headers.get_all(name, [])
        if len(values) != 1:
            raise ValueError('Missing or repeated header')
        return values[0]

    def do_GET(self):
        self.input_deadline.cancel()
        if self.path != '/v1/health':
            self.fail(404, 'not_found')
            return
        with self.server.counter_lock:
            completed = self.server.completed
        self.send_json(200, {
            'protocol': 'interpret-v1', 'ready': True,
            'source_language': 'en', 'target_language': 'zh-Hans', 'device': 'cpu',
            'audio_format': '16000 Hz mono PCM16 WAV', 'maximum_seconds': MAX_SECONDS,
            'maximum_bytes': MAX_WAV_BYTES, 'inference_slots': 1,
            'header_deadline_seconds': HEADER_DEADLINE_SECONDS,
            'body_deadline_seconds': BODY_DEADLINE_SECONDS,
            'inference_busy': self.server.inference.locked(),
            'completed_requests': completed,
        })

    def do_POST(self):
        if self.path != '/v1/interpret':
            self.fail(404, 'not_found')
            return
        try:
            token = self.one_header('X-API-Key')
        except ValueError:
            self.fail(401, 'unauthorized')
            return
        if not hmac.compare_digest(token.encode('utf-8'), self.server.token.encode('ascii')):
            self.fail(401, 'unauthorized')
            return
        if self.headers.get_all('Transfer-Encoding'):
            self.fail(400, 'chunked_input_not_supported')
            return
        if not self.headers.get_all('Content-Length'):
            self.fail(411, 'content_length_required')
            return
        try:
            length_text = self.one_header('Content-Length')
            content_type = self.one_header('Content-Type').split(';', 1)[0].strip().lower()
            session = self.one_header('X-Session-ID')
            segment = self.one_header('X-Segment-ID')
            revision = self.one_header('X-Revision')
            if not re.fullmatch('[0-9]{1,7}', length_text):
                raise ValueError('Invalid byte count')
            length = int(length_text)
            if not re.fullmatch('[A-Za-z0-9_-]{1,48}', session):
                raise ValueError('Invalid session')
            for value in (segment, revision):
                if not re.fullmatch('[1-9][0-9]{0,9}', value) or int(value) > 0xffffffff:
                    raise ValueError('Invalid sequence')
            segment, revision = int(segment), int(revision)
        except ValueError:
            self.fail(400, 'invalid_headers')
            return
        if content_type != 'audio/wav':
            self.fail(415, 'require_audio_wav')
            return
        if not 44 <= length <= MAX_WAV_BYTES:
            self.fail(413, 'audio_byte_limit')
            return
        try:
            self.input_deadline.arm(BODY_DEADLINE_SECONDS)
            data = self.rfile.read(length)
            # Model processing is not ingress; valid long inference must not
            # inherit either input timer after the complete body has arrived.
            self.input_deadline.cancel()
            if len(data) != length:
                raise ValueError('Incomplete body')
            decode_wav(data)
        except TimeoutError:
            self.fail(408, 'input_timeout')
            return
        except (ValueError, wave.Error, EOFError):
            self.fail(400, 'invalid_wav')
            return
        # Reject overload instead of building an unbounded audio backlog.
        if not self.server.inference.acquire(blocking=False):
            self.fail(429, 'inference_busy')
            return
        try:
            identity = (self.client_address[0], session)
            sequence = (segment, revision)
            digest = hashlib.sha256(data).hexdigest()
            previous = self.server.sessions.get(identity)
            if previous:
                if sequence == previous['sequence']:
                    if digest != previous['audio_hash']:
                        self.fail(409, 'conflicting_retry')
                    else:
                        self.send_json(200, previous['response'])
                    return
                if sequence < previous['sequence']:
                    self.fail(409, 'stale_sequence')
                    return
            elif len(self.server.sessions) >= 8:
                self.fail(409, 'session_limit_restart_required')
                return
            started = time.perf_counter()
            try:
                result = self.server.interpreter.transcribe_translate(data)
            except ValueError:
                self.fail(422, 'model_input_limit')
                return
            except Exception:
                self.fail(503, 'inference_failed')
                return
            response = {
                'protocol': 'interpret-v1', 'session_id': session,
                'segment_id': segment, 'revision': revision, 'final': True,
                'source_language': 'en', 'target_language': 'zh-Hans',
                'transcript': result['Transcript'], 'translation': result['Chinese'],
                'no_speech': result['NoSpeech'], 'audio_seconds': result['AudioSeconds'],
                'timings': {'asr_seconds': result['ASRSeconds'], 'mt_seconds': result['MTSeconds'],
                            'total_seconds': time.perf_counter() - started},
            }
            self.server.sessions[identity] = {
                'sequence': sequence, 'audio_hash': digest, 'response': response,
            }
            with self.server.counter_lock:
                self.server.completed += 1
            self.send_json(200, response)
        finally:
            self.server.inference.release()

