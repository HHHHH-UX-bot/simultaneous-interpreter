"""Load the verified board firmware and upload its exact local Qwen assets.

No Windows Wi-Fi profile, API key, password or credential mailbox is read.
Optional cloud configuration uses the existing private interactive configurator.
"""
import argparse
import hashlib
import json
import socket
import struct
import subprocess
import sys
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CABLE = "210241277120"
MODEL_NAME = "qwen2.5-0.5b-instruct-g64-q6.tqw"
MODEL_BYTES = 401637888
MODEL_SHA = "e7ebb0a8435ac90173ef9cefa753adc7fe2f6a5fb8fa86757a98f1344dcf4863"
TOKENIZER_NAME = "qwen2_0.5b.tokenizer.bin"
TOKENIZER_BYTES = 5277796
TOKENIZER_SHA = "b6aa0fa09ec4913e8892cc1f25c0c76f24a72dbd3074131782bea58b6a944aea"


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def project_path(value):
    path = Path(value)
    return (path if path.is_absolute() else ROOT / path).resolve()


def checked_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def verified_files(app, model, tokenizer):
    build = checked_json(app / "build.json")
    if build.get("Status") != "passed" or not build.get("TinyQwen"):
        raise RuntimeError("A successful --tinyqwen application build is required")
    if not all(build.get(field) for field in ("ArmTLS", "RemoteDisplay", "Touch")):
        raise RuntimeError("The application must include ARM TLS, remote display and touch")
    if build.get("Selftest"):
        raise RuntimeError("The default launcher requires the microphone firmware")
    elf = app / Path(build["ELF"]).name
    if sha(elf) != build["ELFSHA256"]:
        raise RuntimeError("Application ELF hash mismatch")
    for name, digest in build["InputSHA256"].items():
        if sha(ROOT / name) != digest:
            raise RuntimeError("Application sources changed; rebuild first: " + name)
    platform = ROOT / "output/z7_nano/remote_display"
    pl = checked_json(platform / "build.json")
    bit = platform / "z7_nano_audio_top.bit"
    if pl.get("Status") != "passed" or sha(bit) != pl["BitstreamSHA256"]:
        raise RuntimeError("Matching verified PL bitstream required")
    if bool(build.get('SpeakerCNN')) != bool(pl.get('SpeakerCNN')):
        raise RuntimeError('Speaker firmware and PL platform must match')
    if build.get('SpeakerCNN') and pl.get('usr_access') != 0x5a374e35:
        raise RuntimeError('Verified speaker platform identity required')
    for item in pl["LogicHashes"]:
        if sha(ROOT / item["File"]) != item["SHA256"]:
            raise RuntimeError("PL sources changed; rebuild first: " + item["File"])
    ps_init = next(platform.rglob("ps7_init.tcl"), None)
    if ps_init is None:
        raise RuntimeError("Matching PS initialization file is missing")
    if pl.get('PS7InitSHA256') and sha(ps_init)!=pl['PS7InitSHA256']:
        raise RuntimeError('PS initialization hash mismatch')
    host = ROOT / "output/z7_nano/usb"
    cpu1 = checked_json(host / "host_build.json")
    if cpu1.get("status") != "passed" or sha(host / "host.elf") != cpu1["elf_sha256"]:
        raise RuntimeError("Matching verified CPU1 touch firmware required")
    for name, digest in cpu1["input_hashes"].items():
        if sha(ROOT / name) != digest:
            raise RuntimeError("CPU1 sources changed; rebuild first: " + name)
    for path, length, digest in ((model, MODEL_BYTES, MODEL_SHA),
                                 (tokenizer, TOKENIZER_BYTES, TOKENIZER_SHA)):
        if path.stat().st_size != length or sha(path) != digest:
            raise RuntimeError("Exact firmware-pinned asset required: " + str(path))
    for name in ("load_tts.py", "run_cloud_touch.py", "send_model_udp.py",
                 "close_cloud_for_tinyqwen.tcl"):
        if not (ROOT / "scripts" / name).is_file():
            raise RuntimeError("Required local script missing: " + name)
    return build, elf, bit, ps_init


def owned_touch_app(explicit):
    if explicit:
        candidates = [project_path(explicit) / "touch_live.json"]
    else:
        candidates = list((ROOT / "output").rglob("touch_live.json"))
    active = []
    for manifest in candidates:
        if not manifest.is_file():
            if explicit:
                raise RuntimeError("Current app has no owned CPU1 manifest: " + str(manifest))
            continue
        record = checked_json(manifest)
        if record.get("status") == "start_unconfirmed":
            raise RuntimeError("Unconfirmed CPU1 start requires the existing owned cleanup: " + str(manifest))
        if record.get("status") not in ("running", "unconfirmed"):
            continue
        app = manifest.parent
        if record.get("load_id") != sha(app / "load.json") or record.get("cable_serial") != CABLE:
            raise RuntimeError("CPU1 ownership does not match its recorded board load: " + str(manifest))
        active.append(app)
    if len(active) > 1:
        raise RuntimeError("More than one active ownership record; use --current-app-dir for the board's matching app")
    return active[0] if active else None


def board_status(board, bind, timeout):
    magic, sequence = 0x314C4254, 0
    deadline = time.perf_counter() + timeout
    last = None
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((bind, 0))
        sock.connect((board, 4190))
        sock.settimeout(0.5)
        while time.perf_counter() < deadline:
            sequence += 1
            head = struct.pack("<7I", magic, 1, sequence, 0, 0, 0, 0)
            sock.send(head + struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF))
            try:
                packet = sock.recv(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            if len(packet) < 96:
                continue
            fields = struct.unpack_from("<8I", packet)
            if fields[:3] != (magic, 0x81, sequence) or fields[6] != len(packet) - 32:
                continue
            crc = zlib.crc32(packet[32:], zlib.crc32(packet[:28])) & 0xFFFFFFFF
            if fields[7] != crc:
                continue
            words = struct.unpack_from("<16I", packet, 32)
            if words[0] != 1:
                raise RuntimeError("Unsupported board LLM status version")
            last = {"phase": words[1], "flags": words[2], "error": words[13],
                    "finish_reason": words[14], "detail": words[15]}
            if fields[5] or words[1] == 7 or words[13]:
                raise RuntimeError("Board LLM initialization failed: " + json.dumps(last))
            if (words[2] & 7) == 7:
                return last
            time.sleep(0.2)
    raise TimeoutError("Board did not confirm both SHA-validated assets and engine initialization: " + str(last))


def run(command):
    subprocess.run([str(value) for value in command], check=True, cwd=ROOT)


def ensure_hw_server():
    def reachable():
        try:
            with socket.create_connection(("127.0.0.1", 3121), timeout=0.5):
                return True
        except OSError:
            return False
    if reachable():
        return
    executable = Path("C:/Xilinx/SDK/2018.3/bin/unwrapped/win64.o/hw_server.exe")
    if not executable.is_file():
        raise RuntimeError("SDK 2018.3 hardware server is missing")
    logs = ROOT / "output/tinyqwen"
    logs.mkdir(parents=True, exist_ok=True)
    with (logs / "hw_server_stdout.log").open("ab") as out, (logs / "hw_server_stderr.log").open("ab") as err:
        process = subprocess.Popen([str(executable), "-s", "tcp::3121"],
            stdout=out, stderr=err, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    deadline = time.perf_counter() + 10
    while not reachable():
        if process.poll() is not None or time.perf_counter() > deadline:
            raise RuntimeError("Hardware server did not start; see output/tinyqwen/hw_server_stderr.log")
        time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-dir", default="output/tinyqwen/microphone")
    parser.add_argument("--model", default="models/" + MODEL_NAME)
    parser.add_argument("--tokenizer", default="models/" + TOKENIZER_NAME)
    parser.add_argument("--xsct", type=Path, default=ROOT / "scripts/xsct_path_compat.bat")
    parser.add_argument("--board", default="192.168.1.50")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--pace-ms", type=float, default=0)
    parser.add_argument("--window", type=int, default=1)
    parser.add_argument("--retry-ms", type=float, default=20)
    parser.add_argument("--status-timeout", type=float, default=300)
    parser.add_argument("--current-app-dir", help="explicit matching CPU1 ownership if recorded apps are ambiguous")
    parser.add_argument("--upload-only", action="store_true", help="retry assets and verify board status without JTAG or reloading")
    parser.add_argument("--cold-start", action="store_true", help="the whole board was power cycled; no live session or CPU1 ownership remains")
    parser.add_argument("--configure", action="store_true", help="after local startup, privately type cloud runtime credentials")
    parser.add_argument("--local-voice", action="store_true", help="after SHA readiness, arm cloud ASR -> board LLM -> cloud TTS through public UI controls")
    parser.add_argument("--dry-run", action="store_true", help="verify local hashes and display the plan; do not access the board")
    args = parser.parse_args()
    args.xsct = args.xsct.resolve()
    if args.upload_only and (args.cold_start or args.current_app_dir or args.configure or args.local_voice):
        parser.error("--upload-only cannot stop, reload or configure an application")
    if args.status_timeout <= 0 or args.pace_ms < 0 or args.retry_ms <= 0 or not 1 <= args.window <= 32:
        parser.error("invalid timing")
    app, model, tokenizer = map(project_path, (args.app_dir, args.model, args.tokenizer))
    build, elf, bit, ps_init = verified_files(app, model, tokenizer)
    previous = None if args.upload_only or args.cold_start else owned_touch_app(args.current_app_dir)
    print("Verified local ELF, PL, PS init, CPU1 and both exact asset hashes.", flush=True)
    print("ELF:", elf, "\nPL:", bit, "\nPS init:", ps_init, flush=True)
    if not args.upload_only:
        if args.cold_start:
            print("Plan: explicit cold start -> full load_tts PL + PS + CPU1.", flush=True)
        else:
            print("Plan: stop owned CPU1 " + (str(previous) if previous else "(none recorded; close requires CPU1 already stopped)") +
                  " -> public UI_PAUSE/NCF4/AT closure -> full load_tts PL + PS + CPU1.", flush=True)
    print("Plan: upload Q6 to UDP 4178 and tokenizer to UDP 4179; require board engine initialized on UDP 4190.", flush=True)
    if args.configure:
        print("Plan: optional configure_cloud_runtime private interactive input; no credentials retained by this loader.", flush=True)
    if args.dry_run:
        print("DRY_RUN_FILES_VERIFIED_PLAN_ONLY; no board load, upload or board validation was performed.")
        return
    if not args.upload_only:
        if not args.xsct.is_file():
            raise RuntimeError("Xilinx SDK 2018.3 xsct is missing")
        ensure_hw_server()
        if not args.cold_start:
            if previous:
                owner=next((p for p in previous.parents if (p/'scripts/run_cloud_touch.py').is_file()),None)
                if owner is None:raise RuntimeError('Recorded touch owner project is missing')
                run([sys.executable, owner / "scripts/run_cloud_touch.py", "stop",
                     "--app-dir", previous, "--xsdb", args.xsct])
            attempt = app / "loads" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
            attempt.mkdir(parents=True, exist_ok=False)
            result = attempt / "close_cloud.json"
            run([args.xsct, ROOT / "scripts/close_cloud_for_tinyqwen.tcl", CABLE, result])
            closed = checked_json(result)
            if closed.get("status") != "passed" or not closed.get("cpu0_stopped") or closed.get("active_tcp") is not False:
                raise RuntimeError("Owned cloud closure was not confirmed; firmware was not reloaded")
        run([sys.executable, ROOT / "scripts/load_tts.py", "--app-dir", app, "--xsct", args.xsct])
    retained = False
    if args.upload_only:
        try:
            board_status(args.board, args.bind, min(args.status_timeout, 1))
            retained = True
            print("Existing initialized engine retains both previously validated assets; no overwrite.", flush=True)
        except TimeoutError:
            pass
    assets = () if retained else ((model, 4178), (tokenizer, 4179))
    for path, port in assets:
        try:
            run([sys.executable, ROOT / "scripts/send_model_udp.py", path,
                 "--board", args.board, "--bind", args.bind, "--port", port,
                 "--pace-ms", args.pace_ms, "--window", args.window, "--retry-ms", args.retry_ms])
        except subprocess.CalledProcessError:
            # An initialized engine locks both validated assets. This can also
            # occur if a final upload ACK was lost just before initialization.
            # Confirm that BOTH assets and engine are ready before retaining them.
            board_status(args.board, args.bind, min(args.status_timeout, 3))
            retained = True
            print("Existing initialized engine retains both previously validated assets; no overwrite.", flush=True)
            break
    status = board_status(args.board, args.bind, args.status_timeout)
    record = {"status": "passed", "verified_at_utc": datetime.now(timezone.utc).isoformat(),
              "scope": "asset SHA256 readiness and board engine initialization; no inference/audio claim",
              "model_sha256": MODEL_SHA, "tokenizer_sha256": TOKENIZER_SHA,
              "board": args.board, "llm_status": status, "upload_only": args.upload_only}
    record["retained_existing_assets"] = retained
    (app / "tinyqwen_ready.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print("BOARD_TINYQWEN_ASSETS_VERIFIED_AND_ENGINE_INITIALIZED", flush=True)
    if args.local_voice:
        symbols = (app / 'symbols.log').read_text(encoding='utf-8')
        address = next((int(v[0],16) for line in symbols.splitlines() if len(v:=line.split())==3 and v[2]=='local_voice_diag'),None)
        if address is None:raise RuntimeError('Firmware has no local voice controller')
        run([args.xsct,ROOT/'scripts/activate_local_voice.tcl',address])
    if args.configure:
        run([sys.executable, ROOT / "scripts/configure_cloud_runtime.py", "--yes",
             "--mode", build["Mode"], "--symbols", app / "symbols.log", "--xsdb", args.xsct])


if __name__ == "__main__":
    main()
