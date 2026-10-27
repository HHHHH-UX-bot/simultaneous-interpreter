"""Run every PC-only regression suite with one compiler and print a summary.

No board, JTAG, network or credentials are used. Each suite's own output goes
to tmp/host_tests/<name>.log; the exit code is non-zero if any suite fails.
Prerequisite: output/subtitle_ui/ui_assets.c (scripts/generate_subtitle_assets.py).

Not run here, and why:
  validate_speech_timing        needs a captured --metadata file from a board run
  validate_arm_tls              online certificate checks
  validate_subtitle_controls/epoch  read a live board over JTAG
  validate_translation_service  starts the LAN translation service
  validate_z7_nano_stream/upload/uart_baud  model the stream pilot, ESP-side TLS
      passthrough and 921600 profiles, which the product build does not compile;
      their expectations predate 2026-10-17 and are kept only as history.
"""
import argparse, os, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GCC = Path("D:/cenvironment/mingw64/bin/gcc.exe")

# name -> how the suite takes the compiler: "gcc" (--gcc FILE), "dir" (--gcc-directory DIR),
# "env" (CC variable) or None (fixed inside the script)
SUITES = {
    "pytest": None,
    "test_sd_model": "gcc",
    "test_glossary": "gcc",
    "test_network_history": "gcc",
    "test_speaker_ui": "env",
    "test_speaker_driver": "env",
    "test_speaker_llm_guard": "env",
    "validate_asr_accuracy": "gcc",
    "validate_cloud_events": None,
    "validate_cloud_live": None,
    "validate_english_profile": "gcc",
    "validate_history_ui": "gcc",
    "validate_model_backend": None,
    "validate_network_client": None,
    "validate_network_protocol": "dir",
    "validate_network_runtime": "dir",
    "validate_opus_dtx": "gcc",
    "validate_ps_wifi_probe": "dir",
    "validate_remote_display": "gcc",
    "validate_settings": "gcc",
    "validate_speech_playback": "gcc",
    "validate_subtitle_demo": "gcc",
    "validate_subtitle_ui": "gcc",
    "validate_z7_nano_capabilities": None,
    "validate_z7_nano_tls_boot": None,
    "validate_z7_nano_usb_reset": "gcc",
    "validate_zhzh_transport": "gcc",
}


def command(name, gcc):
    if name == "pytest":
        return [sys.executable, "-B", "-m", "pytest", "-q", "-p", "no:cacheprovider", "tests"], None
    style = SUITES[name]
    argv = [sys.executable, "-B", str(ROOT / "scripts" / (name + ".py"))]
    env = None
    if style == "gcc":
        argv += ["--gcc", str(gcc)]
    elif style == "dir":
        argv += ["--gcc-directory", str(gcc.parent)]
    elif style == "env":
        env = {**os.environ, "CC": str(gcc)}
    return argv, env


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--gcc", type=Path, default=Path(os.environ.get("CC", DEFAULT_GCC)), help="Windows host gcc.exe")
    p.add_argument("--only", nargs="+", choices=sorted(SUITES), help="run just these suites")
    a = p.parse_args()
    if not (ROOT / "output/subtitle_ui/ui_assets.c").exists():
        raise SystemExit("Generate output/subtitle_ui/ui_assets.c first (scripts/generate_subtitle_assets.py)")
    logs = ROOT / "tmp/host_tests"
    logs.mkdir(parents=True, exist_ok=True)
    results = []
    for name in a.only or SUITES:
        argv, env = command(name, a.gcc.resolve())
        start = time.monotonic()
        for attempt in range(2):
            try:
                run = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, timeout=900)
                output, code = run.stdout + run.stderr, run.returncode
            except subprocess.TimeoutExpired as error:
                output, code = f"{error}\n", -1
            # On this machine Tk occasionally fails to read its own init.tcl
            # ("couldn't read file ... No error"); only that is rerun, once.
            if not (code and name == "pytest" and "usable init.tcl" in output):
                break
        (logs / (name + ".log")).write_text(output, encoding="utf-8")
        last = next((line for line in reversed(output.splitlines()) if line.strip()), "")
        results.append((name, code, time.monotonic() - start))
        print(f"{'PASS' if code == 0 else 'FAIL'} {name:32s} {time.monotonic() - start:5.0f}s  {last[:110]}", flush=True)
    failed = [n for n, c, _ in results if c]
    print(f"\n{len(results) - len(failed)}/{len(results)} suites passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    print(f"logs: {logs.relative_to(ROOT).as_posix()}/")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
