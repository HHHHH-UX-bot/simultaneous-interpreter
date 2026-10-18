"""Rebuild microphone WAV files from the board's PCM ring dump.

The firmware mirrors every 16 kHz/16-bit frame that is encoded and uploaded
into a DDR ring (header at 0x01D3F000, data at 0x01D40000, 768 KiB). On an ASR
final it freezes a window that starts 3 s before the server's speech-start
event and ends when the transcript is complete.

Run tmp/cloud/dump_audio.tcl first (it reads 7 header words and the ring), then:
    python scripts/dump_board_audio.py magic total capacity rate start end frozen
"""
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RING = os.path.join(ROOT, "tmp", "cloud", "pcm_ring.bin")
OUT = os.path.join(ROOT, "output", "cloud_app", "audio_dump")
MAGIC = 0x31445541


def window(ring, capacity, start, end):
    data = bytearray()
    at = start
    while at < end:
        offset = at % capacity
        take = min(end - at, capacity - offset)
        data += ring[offset:offset + take]
        at += take
    return bytes(data)


def save(path, pcm, rate):
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(pcm)


def main(values):
    magic, total, capacity, rate, start, end, frozen = values[:7]
    if magic != MAGIC:
        raise SystemExit("dump header invalid: %r" % (values,))
    ring = open(RING, "rb").read()
    if len(ring) < capacity:
        raise SystemExit("ring dump short: %d/%d" % (len(ring), capacity))
    os.makedirs(OUT, exist_ok=True)
    save(os.path.join(OUT, "cloud_mic_ring.wav"), window(ring, capacity, max(0, total - capacity), total), rate)
    if frozen and end > start:
        pcm = window(ring, capacity, start, end)
    else:
        pcm = window(ring, capacity, max(0, total - capacity), total)
    capture = os.path.join(OUT, "cloud_mic_capture.wav")
    save(capture, pcm, rate)
    samples = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    peak = max(map(abs, samples)) if samples else 0
    rms = int((sum(v * v for v in samples) / len(samples)) ** 0.5) if samples else 0
    open(os.path.join(ROOT, "tmp", "cloud", "capture_last.raw"), "wb").write(pcm)
    print("PASS_AUDIO_DUMP frozen=%d window=%.2fs peak=%d rms=%d path=%s" %
          (frozen, len(samples) / rate, peak, rms, capture))
    return 0


if __name__ == "__main__":
    sys.exit(main([int(x, 0) for x in sys.argv[1:]]))
