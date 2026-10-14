"""Input-boundary and silence checks without downloading or running models."""
import io
import json
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'software/model_backend'))
import runtime
from engine import LocalInterpreter, MAX_SECONDS, MAX_WAV_BYTES, decode_wav


def wav(raw, *, rate=16000, channels=1, width=2):
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as audio:
        audio.setnchannels(channels)
        audio.setsampwidth(width)
        audio.setframerate(rate)
        audio.writeframes(raw)
    return stream.getvalue()


def main():
    checks = []

    def check(name, action):
        action()
        checks.append(name)

    def rejected(data):
        try:
            decode_wav(data)
        except (ValueError, wave.Error, EOFError):
            return
        raise AssertionError('Invalid audio accepted')

    raw = struct.pack('<hhh', -32768, 0, 32767)
    check('PCM sign, byte order and sample count preserved',
          lambda: require(decode_wav(wav(raw)) == (raw, 3 / 16000)))
    maximum = b'\x00\x00' * (16000 * MAX_SECONDS)
    check('Inclusive 30 second duration boundary',
          lambda: require(decode_wav(wav(maximum))[1] == MAX_SECONDS))
    bad = {
        'Empty bytes': b'',
        'Missing RIFF header': b'x' * 44,
        'Zero frames': wav(b''),
        'Stereo': wav(b'\x00' * 16, channels=2),
        'Wrong sample rate': wav(raw, rate=8000),
        '8 bit PCM': wav(b'\x80' * 4, width=1),
        '24 bit PCM': wav(b'\x00' * 12, width=3),
        'Duration one sample over limit': wav(maximum + b'\x00\x00'),
        'Byte count over limit': b'\x00' * (MAX_WAV_BYTES + 1),
        'Truncated sample payload': wav(raw)[:-1],
        'Truncated chunk header': wav(raw)[:38],
        'RIFF chunk seek beyond container': bytes.fromhex(
            '5249464628000000574156454a554e4bffffffff00000000000000000000000000000000000000000000000000000000'),
    }
    malformed = bytearray(wav(raw))
    struct.pack_into('<I', malformed, 40, len(raw) + 2)
    bad['Declared PCM payload larger than actual file'] = bytes(malformed)
    malformed = bytearray(wav(raw))
    struct.pack_into('<H', malformed, 20, 3)
    bad['Floating point WAV rejected'] = bytes(malformed)
    for name, data in bad.items():
        check(name, lambda data=data: rejected(data))

    runtime.bootstrap()
    import numpy as np
    interpreter = object.__new__(LocalInterpreter)
    interpreter.np = np
    # With no ASR/MT object installed, a successful return also proves that
    # exact digital silence did not invoke or invent either model result.
    result = interpreter.transcribe_translate(wav(b'\x00\x00' * 16000))
    check('Exact digital silence produces no invented subtitle',
          lambda: require(result['NoSpeech'] and result['Transcript'] == ''
                          and result['Chinese'] == '' and result['Segments'] == []))
    check('Exact silence bypasses both expensive model calls',
          lambda: require(result['ASRSeconds'] == 0 and result['MTSeconds'] == 0))
    record = {
        'Status': 'passed', 'VerifiedAtUTC': runtime.now(), 'Checks': checks,
        'SourceHashes': {
            str(path.relative_to(ROOT)).replace('\\', '/'): runtime.sha(path)
            for path in (Path(__file__), ROOT / 'software/model_backend/engine.py',
                         ROOT / 'software/model_backend/runtime.py')
        },
        'Scope': 'WAV boundaries and exact-zero silence only; no claim of real noise/VAD quality',
    }
    runtime.save('input_validation.json', record)
    print('PASS_MODEL_INPUT_BOUNDARIES ' + str(len(checks)))


def require(condition):
    if not condition:
        raise AssertionError('Input validation assertion failed')


if __name__ == '__main__':
    main()
