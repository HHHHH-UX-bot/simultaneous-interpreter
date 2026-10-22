"""Generate the Chinese TTS PCM array used by the microphone-less self-test build.

Windows-only (uses the built-in System.Speech voice, e.g. Microsoft Huihui).
The output file is written to output/cloud_app/selftest_pcm.c and compiled into
cloud_app_selftest.elf by `python scripts/build_cloud_app.py --selftest`.

The self-test firmware loops "four sentences + 30 s silence", which lets the
full board pipeline (Opus encoder -> Ogg muxer -> WSS -> ASR -> subtitle ->
T1 -> history) run without a person speaking at the microphone.
"""
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, "tmp", "cloud")
OUT = os.path.join(ROOT, "output", "cloud_app", "selftest_pcm.c")
SENTENCES = [
    "早上好。",
    "今天天气非常好。",
    "我想喝一杯咖啡，再吃一点东西。",
    "请把这句话翻译成中文。",
]
GAP_SECONDS = 0.8


def synthesize(voice="Microsoft Huihui Desktop",rate=0):
    os.makedirs(TMP, exist_ok=True)
    paths = []
    for index, sentence in enumerate(SENTENCES, start=1):
        path = os.path.join(TMP, "selftest_zh%d.wav" % index)
        script = (
            "Add-Type -AssemblyName System.Speech;"
            "$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
            "$s.SelectVoice('%s');"
            "$s.Rate=%d;"
            "$f=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,"
            "[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,"
            "[System.Speech.AudioFormat.AudioChannel]::Mono);"
            "$s.SetOutputToWaveFile('%s',$f);"
            "$s.Speak('%s');$s.Dispose()" % (voice, rate, path, sentence)
        )
        subprocess.run(["powershell", "-NoProfile", "-Command", script], check=True)
        paths.append(path)
    return paths


def main():
    paths = synthesize()
    silence = b"\x00\x00" * int(16000 * GAP_SECONDS)
    pcm = bytearray()
    for path in paths:
        with wave.open(path, "rb") as handle:
            if (handle.getnchannels(), handle.getsampwidth(), handle.getframerate()) != (1, 2, 16000):
                raise SystemExit("unexpected wav format: " + path)
            pcm += handle.readframes(handle.getnframes())
        pcm += silence
    samples = len(pcm) // 2
    values = struct.unpack("<%dh" % samples, bytes(pcm))
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("/* Generated Chinese TTS self-test audio (16 kHz mono s16le). */\n")
        handle.write("#include <stdint.h>\n")
        handle.write("const int16_t selftest_pcm[] = {\n")
        for at in range(0, samples, 16):
            handle.write("    " + ",".join(str(v) for v in values[at:at + 16]) + ",\n")
        handle.write("};\n")
        handle.write("const uint32_t selftest_samples = %du;\n" % samples)
        handle.write("const uint32_t selftest_speech_ms = %du;\n" % (samples * 1000 // 16000))
    print("PASS_SELFTEST_AUDIO %s samples=%d speech_ms=%d" % (OUT, samples, samples * 1000 // 16000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
