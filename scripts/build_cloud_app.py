"""Build the ARM cloud application (output/cloud_app/cloud_app.elf).

Same freestanding flags as scripts/run_network_ui.py plus the cloud modules
(cloud_events/cloud_wire/cloud_live/cloud_client/cloud_frame, bundled wslay
frame codec), the MMU/cache boot path and the fixed-point libopus encoder. The
byte-for-byte ELF used for a board session is recorded in
output/cloud_app/build.json; the API key is never compiled in (it is written to
the .cloud_config OCM window by the host).

Usage:
  python -B scripts/build_cloud_app.py [--gcc <arm-none-eabi-gcc>]
"""
import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/cloud_app"
PROFILE = json.loads((ROOT / "PROFILE.json").read_text(encoding="utf-8")) if (ROOT / "PROFILE.json").is_file() else {}
DEFAULT_GCC = Path(r"C:\Xilinx\SDK\2018.3\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe")

FLAGS = [
    "-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access",
    "-O2", "-g", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-strict-aliasing", "-fno-short-enums", "-Wall", "-Wextra", "-Werror",
    "-std=c11", "-nostdlib", "-Wl,--build-id=none", "-Wl,--print-memory-usage",
    "-DZ7_NETWORK_CAPABILITIES", "-DZ7_MMU_CACHE", "-DZ7_OPUS", "-DZ7_ASR_SUBTITLE", "-DZ7_LATENCY_UI",
    "-I", str(ROOT / "software/subtitle_ui"),
    "-I", str(ROOT / "third_party/wslay/lib/includes"),
    "-I", str(ROOT / "third_party/opus/include"),
    "-T", str(ROOT / "software/network_ui/link.ld"),
    "-Wl,-Map=" + str(OUT / "cloud_app.map"),
]

SOURCES = [
    "software/ps_bringup/start.S",
    "software/ps_bringup/cache.c",
    "software/network_ui/main.c",
    "software/network_ui/network.c",
    "software/network_ui/wifi_recover.c",
    "software/network_ui/protocol.c",
    "software/network_ui/cloud_events.c",
    "software/network_ui/cloud_wire.c",
    "software/network_ui/cloud_live.c",
    "software/network_ui/cloud_client.c",
    "software/network_ui/speech_playback.c",
    "software/network_ui/cloud_frame.c",
    "software/network_ui/stream_pilot.c",
    "software/network_ui/opus_codec.c",
    "software/network_ui/ogg_opus.c",
    "software/ps_wifi/wifi_at_probe.c",
    "software/subtitle_ui/ui_model.c",
    "software/subtitle_ui/ui_render.c",
    "software/subtitle_ui/ui_demo.c",
    "software/subtitle_ui/runtime.c",
    "output/subtitle_ui/ui_assets.c",
]

OPUS_DIR = ROOT / "third_party/opus"
OPUS_CFLAGS = [
    "-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access",
    "-O2", "-g", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-strict-aliasing", "-fno-short-enums", "-std=c11",
    "-DFIXED_POINT", "-DOPUS_BUILD", "-DDISABLE_FLOAT_API=1", "-DVAR_ARRAYS=1",
    "-include", str(OPUS_DIR / "config/opus_alloc_override.h"),
    "-I", str(OPUS_DIR / "config"),
    "-I", str(OPUS_DIR / "include"),
    "-I", str(OPUS_DIR / "celt"),
    "-I", str(OPUS_DIR / "silk"),
    "-I", str(OPUS_DIR / "silk/fixed"),
    "-I", str(OPUS_DIR / "src"),
    "-I", str(OPUS_DIR),
]
OPUS_EXCLUDE = ("/x86/", "/arm/", "/neon/", "/ne10/", "/float/", "/mips/", "/tests/", "dump_modes")
OPUS_SKIP = ("src/analysis.c", "src/mlp.c", "src/mlp_data.c", "celt/opus_custom_demo.c",
             "src/opus_demo.c", "src/opus_compare.c", "src/repacketizer_demo.c")

TINYQWEN_SOURCES = [
    "software/network_ui/tinyqwen.c",
    "software/network_ui/tiny_tokenizer.c",
    "software/network_ui/tiny_board_llm.c",
    "software/network_ui/tiny_model_transport.c",
    "software/network_ui/tiny_heap.c",
    "software/network_ui/llm_ui_bridge.c",
]


def opus_sources():
    files = []
    for sub in ("celt", "silk", "src"):
        for path in sorted((OPUS_DIR / sub).rglob("*.c")):
            rel = path.relative_to(OPUS_DIR).as_posix()
            if any(token in "/" + rel for token in OPUS_EXCLUDE):
                continue
            if rel in OPUS_SKIP:
                continue
            files.append(path)
    return files


# Mailboxes and windows pinned by the host tooling and the PS-side ABI.
SYMBOLS = [
    ("_start", 0x10000),
    ("report", 0x1E000),
    ("ui_info", 0x1E080),
    ("ui_command", 0x1E0C0),
    ("wifi_info", 0x1E100),
    ("network_report", 0x1E140),
    ("network_config", 0x1E180),
    ("cloud_client_config", 0x1E200),
    ("snapshots", 0x1B00000),
]


def main():
    global OUT
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc", type=Path, default=DEFAULT_GCC)
    parser.add_argument("--mode", choices=("zhzh", "zhen", "enzh", "enen"), default=PROFILE.get("mode", "zhzh"),
                        help="source/target profile: zhzh, zhen, enzh or enen")
    parser.add_argument("--selftest", action="store_true",
                        help="build the microphone-less self-test ELF from the matching generated TTS PCM")
    parser.add_argument("--selftest-pcm",type=Path,help="project-local generated PCM C file for a custom automatic speech test; requires --selftest")
    parser.add_argument("--selftest-once",action="store_true",help="send the synthetic speech once, then continue streaming silence")
    parser.add_argument("--touch",action="store_true",help="enable the Z7-Nano CPU1 touch consumer")
    parser.add_argument("--remote-display",action="store_true",help="PL Ethernet framebuffer and mouse input; requires ARM TLS and touch profile")
    parser.add_argument("--tinyqwen",action="store_true",help="on-board Qwen inference; requires --remote-display --touch --arm-tls")
    parser.add_argument("--speaker-cnn",action="store_true",help="FPGA MFCC and CNN; requires the matching speaker platform and --tinyqwen")
    parser.add_argument("--sd-boot",action="store_true",help="when BOOT_MODE is SD, start CPU1, apply z7cfg.bin and (with --tinyqwen) load qwen.tok/qwen.tqw from the card itself; JTAG boots unchanged")
    parser.add_argument("--speech", action="store_true",help="translated audio on lineout (requires playback PL; automatic for enzh)")
    parser.add_argument("--text-only-response", action="store_true", help="diagnostic: request text only while retaining the same audio upload/playback build")
    parser.add_argument("--output-dir", type=Path, help="separate build directory, including ELF, symbols and metadata")
    parser.add_argument("--event-id-trace", action="store_true", help="record bounded hashed event IDs/lengths; no speech text")
    parser.add_argument("--latency-probe", action="store_true", help="record queue and transport timing metadata; no audio/text payloads")
    parser.add_argument("--low-latency-playback", action="store_true", help="adapt 1.28 s / 2 s voice prefill to observed PCM receive cadence")
    parser.add_argument("--arm-tls", action="store_true", help="verified TLS on Zynq; ESP carries raw TCP")
    parser.add_argument("--arm-tls-packets", action="store_true", help="ARM TLS over acknowledged CIPSEND blocks; required with --arm-tls")
    parser.add_argument("--arm-tls-active-recv", action="store_true", help="ARM TCP packet transport with active IPD receive, avoiding passive read round trips")
    parser.add_argument("--uart-baud", type=int, choices=(460800,921600,1000000,1666666,2000000),default=2000000)
    args = parser.parse_args()
    if args.speaker_cnn and not args.tinyqwen:parser.error('--speaker-cnn requires --tinyqwen')
    if args.tinyqwen and not (args.remote_display and args.touch and args.arm_tls):
        parser.error('--tinyqwen requires --remote-display --touch --arm-tls')
    if args.selftest_pcm and not args.selftest:parser.error('--selftest-pcm requires --selftest')
    if args.selftest_once and not args.selftest:parser.error('--selftest-once requires --selftest')
    if PROFILE and args.mode != PROFILE["mode"]:
        parser.error("This folder belongs to " + PROFILE["mode"] + "; use its matching project")
    if args.mode == "enzh":
        args.speech = True
    if args.text_only_response and not args.speech:
        parser.error("--text-only-response requires translated-audio mode")

    if args.arm_tls != args.arm_tls_packets:
        parser.error("--arm-tls and --arm-tls-packets go together")
    if args.arm_tls_active_recv and not args.arm_tls_packets:
        parser.error("--arm-tls-active-recv requires --arm-tls-packets")
    if args.arm_tls:
        if args.mode == "zhen" and not args.speech:
            parser.error("--mode zhen --arm-tls requires --speech")
    if args.mode == "zhen":
        OUT = ROOT / "output/cloud_app_zhen"
    if args.speech:
        if args.mode not in ("zhen", "enzh"):
            parser.error("--speech requires --mode zhen")
        OUT = ROOT / "output/cloud_app_zhen_speech"
    if args.arm_tls and args.mode == "zhzh":
        OUT = ROOT / "output/cloud_app_zhzh_arm_tls"
    if args.mode in ("enzh", "enen"):
        OUT = ROOT / "output" / args.mode / ("selftest" if args.selftest else "microphone")
    if args.output_dir:
        OUT = args.output_dir.resolve()
    gcc = args.gcc
    nm = gcc.with_name("arm-none-eabi-nm.exe")
    if not gcc.is_file() or not nm.is_file():
        raise SystemExit("ARM GCC not found: " + str(gcc))
    OUT.mkdir(parents=True, exist_ok=True)
    # Z7-Nano display constants, derived from the board source exactly as
    # prepare_z7_nano_display.py does, so a clean clone needs no copied header.
    ps = json.loads((ROOT / "hardware/z7_nano/source.json").read_text(encoding="utf-8"))["PSParameters"]
    (OUT / "framebuffer_hw.h").write_text(
        "/* Generated by build_cloud_app.py from hardware/z7_nano/source.json. */\n"
        "#define FB_WIDTH 1024u\n#define FB_HEIGHT 600u\n#define FB_STRIDE 4096u\n"
        "#define FB_BASE0 0x01000000u\n#define FB_BASE1 0x01400000u\n"
        "#define VDMA_BASE 0x43c00000u\n#define VIDEO_BASE 0x43c10000u\n"
        f"#define TIMER_TICKS_PER_MS {int(float(ps['PCW_APU_PERIPHERAL_FREQMHZ']) * 1000 / 2)}u\n",
        encoding="utf-8", newline="\n")
    flags = list(FLAGS) + ["-I", str(OUT)]
    if args.event_id_trace or args.latency_probe:flags.append('-DZ7_EVENT_ID_TRACE')
    if args.latency_probe:flags.append('-DZ7_LATENCY_PROBE')
    if args.low_latency_playback:flags.append('-DZ7_LOW_LATENCY_PLAYBACK')
    if args.arm_tls_active_recv:flags.append('-DZ7_ARM_TCP_ACTIVE_RECV')
    if args.arm_tls:
        flags += ["-DZ7_ARM_TLS", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                  '-DMBEDTLS_CONFIG_FILE="tls_config.h"', "-I", str(ROOT / "software/network_ui"),
                  "-I", str(ROOT / "third_party/mbedtls/include")]
        flags = [str(ROOT / "software/network_ui/link_arm_tls.ld") if f==str(ROOT / "software/network_ui/link.ld") else f for f in flags]
    if args.uart_baud==921600:
        flags.append("-DZ7_UART_LEGACY_STREAM")
    elif args.uart_baud==460800:
        flags.append("-DZ7_UART_460800_STREAM")
    elif args.uart_baud==1000000:
        flags.append("-DZ7_UART_1M_STREAM")
    elif args.uart_baud==1666666:
        flags.append("-DZ7_UART_1666666_STREAM")
    flags = ["-Wl,-Map=" + str(OUT / "cloud_app.map") if flag.startswith("-Wl,-Map=") else flag for flag in flags]
    if args.mode == "zhen":
        flags.remove("-DZ7_ASR_SUBTITLE")
        flags.append("-DZ7_TRANSLATE_EN")
        flags = ["-Wl,-Map=" + str(OUT / "cloud_app.map") if flag.startswith("-Wl,-Map=") else flag for flag in flags]
    if args.mode == "enzh":
        flags.remove("-DZ7_ASR_SUBTITLE")
        flags += ["-DZ7_SOURCE_EN", "-DZ7_TRANSLATE_ZH"]
    elif args.mode == "enen":
        flags += ["-DZ7_SOURCE_EN", "-DZ7_RECOGNIZE_EN"]
    sources = list(SOURCES)
    if args.tinyqwen:
        flags = ['-mfloat-abi=softfp' if f=='-mfloat-abi=soft' else f for f in flags]
        flags += ['-mfpu=neon','-DZ7_TINYQWEN','-DZ7_LOCAL_AI']
        sources += TINYQWEN_SOURCES
        sources += ['software/network_ui/local_voice.c']
    if args.speaker_cnn:
        flags.append('-DZ7_SPEAKER_CNN')
        sources += ['software/network_ui/speaker_local.c','output/speaker_cnn/model_weights.c']
    if args.sd_boot:
        if not args.arm_tls or not args.touch:parser.error("--sd-boot requires --arm-tls --touch")
        flags.append("-DZ7_SD_BOOT");sources.append("software/network_ui/sd_boot_runtime.c")
        if args.tinyqwen:   # Qwen assets and the optional glossary.txt read from the TF card
            flags.append("-DZ7_GLOSSARY");sources += ["software/network_ui/sd_model.c","software/network_ui/glossary.c"]
    if args.remote_display:
        if not args.arm_tls or not args.touch or args.mode not in ("zhen","enzh"):parser.error("--remote-display requires --arm-tls --touch and a bilingual mode")
        flags.append("-DZ7_REMOTE_DISPLAY");sources.append("software/network_ui/remote_display.c")
    if args.touch:
        flags += ["-DZ7_NANO_TOUCH","-I",str(ROOT/"software/z7_nano_usb")]
        sources.append("software/subtitle_ui/ui_touch.c")
    tls_objects=[]
    if args.arm_tls:
        sources.append("software/network_ui/arm_tls.c")
        tls_dir=OUT / "tls_obj"
        tls_dir.mkdir(exist_ok=True)
        tls_flags=[f for f in flags if not f.startswith("-Wl,") and f!="-Werror"]
        at=tls_flags.index("-T"); del tls_flags[at:at+2]
        for source in sorted((ROOT / "third_party/mbedtls/library").glob("*.c")):
            obj=tls_dir/(source.stem+".o")
            # AMD's Windows launcher strips literal quotes in direct -D arguments.
            response = tls_dir / "compile.rsp"
            arguments = [*tls_flags,"-c",str(source),"-o",str(obj)]
            response.write_text("\n".join('"'+a.replace('\\','\\\\').replace('"','\\"')+'"' for a in arguments)+"\n",encoding="utf-8")
            proc=subprocess.run([str(gcc),"@"+str(response)],capture_output=True,text=True)
            if proc.returncode:
                (OUT/"tls_build.log").write_text(proc.stdout+proc.stderr,encoding="utf-8")
                raise SystemExit(proc.stdout+proc.stderr)
            tls_objects.append(str(obj))
    if args.speech:
        flags.append("-DZ7_TRANSLATE_AUDIO")
    if args.text_only_response:
        flags.append("-DZ7_TEXT_ONLY_RESPONSE")
    elf_name = "cloud_app.elf"
    if args.selftest:
        generated = args.selftest_pcm.resolve() if args.selftest_pcm else ROOT / ("output/selftest_en/selftest_pcm.c" if args.mode in ("enzh", "enen") else "output/cloud_app/selftest_pcm.c")
        if not generated.is_file():
            raise SystemExit("selftest PCM not generated: " + str(generated))
        flags.append("-DZ7_SELFTEST_AUDIO")
        if args.selftest_once:flags.append('-DZ7_SELFTEST_ONCE')
        sources.append(generated.relative_to(ROOT).as_posix())
        elf_name = "cloud_app_selftest.elf"
    opus_objects = OUT / "opus_obj"
    opus_objects.mkdir(exist_ok=True)
    for stale in opus_objects.glob("*.o"):
        stale.unlink()
    opus_log = []
    for source in opus_sources():
        obj = opus_objects / (source.relative_to(OPUS_DIR).as_posix().replace("/", "_") + ".o")
        compile_cmd = [str(gcc), *OPUS_CFLAGS, "-c", str(source), "-o", str(obj)]
        proc = subprocess.run(compile_cmd, cwd=str(ROOT), stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        output = proc.stdout.decode("utf-8", "replace")
        if output:
            opus_log.append(source.name + ":\n" + output)
        if proc.returncode != 0:
            (OUT / "build.log").write_text("\n".join(opus_log), encoding="utf-8")
            print("\n".join(opus_log))
            raise SystemExit("opus build failed: " + str(source))
    opus_list = sorted(str(p) for p in opus_objects.glob("*.o"))
    tiny_objects = []
    if args.tinyqwen:
        tiny_dir = OUT / 'tinyqwen_obj'
        tiny_dir.mkdir(exist_ok=True)
        tiny_flags = [('-O3' if f=='-O2' else f) for f in flags if not f.startswith('-Wl,')]
        at = tiny_flags.index('-T'); del tiny_flags[at:at+2]
        for source in TINYQWEN_SOURCES:
            obj = tiny_dir / (Path(source).stem+'.o')
            arguments = [*tiny_flags,'-c',str(ROOT/source),'-o',str(obj)]
            response = tiny_dir / 'compile.rsp'
            response.write_text('\n'.join('"'+a.replace('\\','\\\\').replace('"','\\"')+'"' for a in arguments)+'\n',encoding='utf-8')
            proc = subprocess.run([str(gcc),'@'+str(response)],cwd=str(ROOT),capture_output=True,text=True)
            if proc.returncode:
                (OUT/'tinyqwen_build.log').write_text(proc.stdout+proc.stderr,encoding='utf-8')
                raise SystemExit(proc.stdout+proc.stderr)
            tiny_objects.append(str(obj))
    linked_sources = [s for s in sources if not args.tinyqwen or s not in TINYQWEN_SOURCES]
    elf_path = OUT / elf_name
    math_libs = ['-lm'] if args.tinyqwen else []
    cmd = [str(gcc), *flags, *[str(ROOT / s) for s in linked_sources], *tiny_objects, *opus_list, *tls_objects, *math_libs, "-lgcc",
           "-o", str(elf_path)]
    response=OUT/"link.rsp"
    response.write_text("\n".join('"'+arg.replace('\\','\\\\').replace('"','\\"')+'"' for arg in cmd[1:])+"\n",encoding="utf-8")
    proc = subprocess.run([str(gcc),"@"+str(response)], cwd=str(ROOT), stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    text = proc.stdout.decode("utf-8", "replace")
    (OUT / "build.log").write_text(" ".join(cmd) + "\n\n" + text, encoding="utf-8")
    print(text)
    if proc.returncode != 0:
        raise SystemExit("cloud_app build failed")
    symbols = subprocess.run([str(nm), "-n", str(elf_path)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    table = symbols.stdout.decode("utf-8", "replace")
    (OUT / "symbols.log").write_text(table, encoding="utf-8")
    layout = SYMBOLS + ([("touch_feedback", 0x1e040)] if args.touch else [])
    for name, address in layout:
        m = re.search(r"^([0-9a-fA-F]+)\s+\w\s+" + re.escape(name) + r"\r?$", table, re.M)
        if not m or int(m.group(1), 16) != address:
            raise SystemExit("layout mismatch: " + name)
    record = {
        "Status": "passed",
        "BuiltAtUTC": datetime.now(timezone.utc).isoformat(),
        "Compiler": str(gcc),
        "Flags": flags,
        "Sources": sources,
        "Selftest": bool(args.selftest),
        "SelftestOnce": bool(args.selftest_once),
        "EventIDTrace": bool(args.event_id_trace or args.latency_probe),
        "LatencyProbe": bool(args.latency_probe),
        "LowLatencyPlayback": bool(args.low_latency_playback),
        "OpusOptimization": "O2",
        "Touch": bool(args.touch),
        "RemoteDisplay":bool(args.remote_display),
        "TinyQwen":bool(args.tinyqwen),
        "SpeakerCNN":bool(args.speaker_cnn),
        "SDBoot":bool(args.sd_boot),
        "TinyQwenOptimization":"O3" if args.tinyqwen else None,
        "Mode": args.mode,
        "Speech": bool(args.speech),
        "TextOnlyResponse": bool(args.text_only_response),
        "ArmTLS": bool(args.arm_tls),
        "ArmTLSPackets": bool(args.arm_tls_packets),
        "ArmTLSActiveReceive": bool(args.arm_tls_active_recv),
        "UARTBaud": args.uart_baud,
        "ELFSHA256": hashlib.sha256(elf_path.read_bytes()).hexdigest(),
        "InputSHA256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sorted(set(sources) | {"scripts/build_cloud_app.py", "software/network_ui/language_profile.h", "software/network_ui/link_arm_tls.ld", "software/z7_nano_usb/touch_link.h", "hardware/z7_nano/source.json"} | {p.relative_to(ROOT).as_posix() for d in ("software/network_ui", "software/subtitle_ui") for p in (ROOT / d).glob("*.h")})},
        "ELF": str(elf_path.relative_to(ROOT)).replace("\\", "/"),
    }
    (OUT / "build.json").write_text(
        json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("PASS_CLOUD_APP_BUILD")


if __name__ == "__main__":
    main()
