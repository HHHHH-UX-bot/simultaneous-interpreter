"""Offline transport/timestamp/session/ASR subtitle regression; no cloud or JTAG."""
import argparse
import base64
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", type=Path, required=True, help="native Windows GCC")
    parser.add_argument("--mode", choices=("zhzh", "zhen"), default="zhzh")
    parser.add_argument("--speech", action="store_true")
    parser.add_argument("--selftest-clock", action="store_true")
    parser.add_argument("--tls-passthrough", action="store_true")
    args = parser.parse_args()
    temp = ROOT / "tmp/asr_accuracy"
    out = ROOT / ("output/zhen" if args.mode == "zhen" else "output/asr_accuracy")
    temp.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    sources = ["tests/asr_accuracy_test.c", "software/network_ui/ogg_opus.c",
               "software/network_ui/cloud_events.c", "software/network_ui/cloud_wire.c",
               "software/network_ui/cloud_frame.c", "software/network_ui/speech_playback.c", "software/network_ui/protocol.c",
               "software/subtitle_ui/ui_model.c", "software/subtitle_ui/ui_render.c", "output/subtitle_ui/ui_assets.c"]
    exe = temp / "asr_accuracy_test.exe"
    command = [str(args.gcc), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT / "third_party/wslay/lib/includes"),
               "-I", str(ROOT / "software/subtitle_ui"),
               *[str(ROOT / p) for p in sources], "-o", str(exe)]
    if args.mode == "zhen":
        command.insert(1, "-DZ7_TRANSLATE_EN")
    if args.speech:
        command.insert(1,"-DZ7_TRANSLATE_AUDIO")
    if args.selftest_clock:
        command.insert(1,"-DZ7_SELFTEST_AUDIO")
    if args.tls_passthrough:
        command.insert(1,"-DZ7_TLS_PASSTHROUGH")
    result = subprocess.run(command, capture_output=True)
    (out / "native_build.log").write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    result = subprocess.run([str(exe)], capture_output=True, cwd=ROOT)
    (out / "native.log").write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    updates, pages = [], []
    if args.selftest_clock:
        assert "PASS_SELFTEST_CLOCK" in result.stdout.decode()
        print("PASS_SELFTEST_CLOCK reconnect does not burst offline audio")
        return
    for line in result.stdout.decode().splitlines():
        if not line.startswith("FRAME "):
            continue
        frame = bytes.fromhex(line[6:])
        size, at = frame[1] & 127, 2
        if size == 126:
            size, at = int.from_bytes(frame[2:4], "big"), 4
        mask = frame[at:at+4]
        payload = bytes(v ^ mask[i % 4] for i, v in enumerate(frame[at+4:]))
        assert len(payload) == size
        event = json.loads(payload)
        if event["type"] == "session.update":
            updates.append(event["session"])
        else:
            page = base64.b64decode(event["audio"], validate=True)
            assert page[:4] == b"OggS"
            expected = int.from_bytes(page[22:26], "little")
            raw = bytearray(page)
            raw[22:26] = bytes(4)
            crc = 0
            for value in raw:
                crc ^= value << 24
                for _ in range(8):
                    crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
            assert crc == expected
            pages.append(page)
    assert len(updates) == 2
    if args.mode == "zhzh":
        assert "translation" not in updates[0] and "modalities" not in updates[0]
        assert updates[0]["input_audio_transcription"] == {"language": "zh"}
    else:
        assert all(s["translation"]["language"] == "en" and s["input_audio_transcription"]["language"] == "zh" for s in updates)
        if args.speech:
            assert all(s["modalities"] == ["text", "audio"] and s["voice"] == "Tina" and not s["translation"]["same_language_skip_options"]["skip_audio"] for s in updates)
    assert updates[1]["input_audio_transcription"]["model"] == "qwen3-asr-flash-realtime"
    vad_silence_ms = updates[0]["turn_detection"]["silence_duration_ms"]
    assert vad_silence_ms in (400, 500)
    assert all(s["turn_detection"]["silence_duration_ms"] == vad_silence_ms for s in updates)
    granule, sequences = 0, []
    for page in pages:
        sequences.append(int.from_bytes(page[18:22], "little"))
        if sequences[-1] < 2:
            assert int.from_bytes(page[6:14], "little") == 0
            continue
        # Test codec produces one 20 ms packet per lacing entry.
        granule += 960 * page[26]
        assert int.from_bytes(page[6:14], "little") == granule
    assert sequences == list(range(len(pages)))
    record = {"status": "passed", "scope": "offline actual C client/muxer/event/UI with mock codec and transport; not recognition quality",
              "native_result": result.stdout.decode().splitlines()[-1],
              "ogg_pages_crc_and_48khz_timeline": len(pages), "session_profiles": len(updates),
              "vad_silence_ms": vad_silence_ms, "mock_backlog_recovery": "100 frames drain below 20 in 1.6 s with "+("100 ms/send, frames <=1 KiB" if args.speech else "200 ms/send")+"; zero drops",
              "board_quality": "not measured"}
    if args.mode == "zhen":
        import struct
        import zlib
        def chunk(kind, data):
            return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
        for name in ("zhen_preview", "zhen_waiting_preview"):
            raw = (temp / (name+".rgbx")).read_bytes()
            scanlines = bytearray()
            for y in range(600):
                scanlines.append(0)
                for x in range(1024):
                    blue, green, red, _ = raw[(y*1024+x)*4:(y*1024+x+1)*4]
                    scanlines.extend((red, green, blue))
            png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",1024,600,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(scanlines))+chunk(b"IEND",b"")
            (out / (name+".png")).write_bytes(png)
        record["layout"] = "English translation above Chinese source; actual native renderer; no missing glyphs"
        record["waiting"] = "initial translation content is blank; retain previous English through new speech and Chinese ASR until next English replaces it"
    (out / "offline_validation.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record))


if __name__ == "__main__":
    main()
