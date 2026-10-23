"""Write runtime Wi-Fi/cloud configuration for the matching ARM firmware.

The firmware reads two OCM windows that the host owns:

  network_configuration @ 0x1E180  magic, request_sequence, action=3 (cloud TLS),
                                   ssid_bytes, password_bytes, credentials[100]
                                   (SSID at [0..], password at [33..]), port, reserved
  cloud_client_config   @ 0x1E200  magic 'CCL1', path[96], workspace[48], key[192]

Secrets are read with getpass or process environment and never printed.
The Tcl file contains only an environment-based launcher, never credential words.

One xsdb process does the whole job: wait for the link to go idle, write both
windows, then trace the network phase so progress is visible while it happens.

Usage: python scripts/configure_cloud_runtime.py [--dry-run]
"""
import argparse
import getpass
import json
import os
import re
import secrets
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = json.loads((ROOT / "PROFILE.json").read_text(encoding="utf-8")) if (ROOT / "PROFILE.json").is_file() else {}
XSDB = Path("C:/Xilinx/Vivado/2018.3/bin/xsdb.bat")
NET_ADDR = 0x1E180
CLOUD_ADDR = 0x1E200
MAGIC_NET = 0x3146434E    # 'NCF1'
MAGIC_CLOUD = 0x314C4343  # 'CCL1'
DEFAULT_PATH = "/api-ws/v1/realtime?model=qwen3.5-livetranslate-flash-realtime"
PHASE = 0x1E148           # network_report.phase
ERROR = 0x1E14C           # network_report.error
CODE = 0x1E168            # network_report.last_code


def words_of(data: bytes, count: int) -> list:
    """Little-endian 32-bit words of a fixed-size buffer."""
    buf = data[:count] + b"\0" * max(0, count - len(data))
    return [int.from_bytes(buf[i:i + 4], "little") for i in range(0, count, 4)]


def network_words(seq: int, ssid: bytes, password: bytes) -> list:
    creds = bytearray(100)
    creds[0:len(ssid)] = ssid
    creds[33:33 + len(password)] = password
    head = [MAGIC_NET, seq, 3, len(ssid), len(password)]
    return head + words_of(bytes(creds), 100) + [0, 0]


def tcl_script(ssid: bytes, password: bytes, path: bytes,
               workspace: bytes, key: bytes, client_address=None, tls_seed_address=None) -> str:
    net = network_words(0, ssid, password)
    cloud = [MAGIC_CLOUD] + words_of(path[:95], 96) + words_of(workspace[:47], 48) \
        + words_of(key[:191], 192)
    lines = [
        "connect",
        "after 500",
        'for {set retry 0} {$retry<20} {incr retry} {',
        '  set board [targets -target-properties -filter {name == "xc7z010"}]',
        '  if {[llength $board]==1 && [dict exists [lindex $board 0] jtag_cable_serial]} {break}',
        '  after 100',
        '}',
        'if {[llength $board]!=1 || ![dict exists [lindex $board 0] jtag_cable_serial] || [dict get [lindex $board 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}',
        'targets -set -filter {name =~ "*Cortex-A9*#0"}',
        "proc w32 {a v} {mwr -force $a $v}",
        "proc ph {} {lindex [mrd -force -value 0x43c20088] 0}",
        # configuration() drops a request that arrives while an AT command or
        # frame is still in flight (error 35): wait for the link to go idle.
        "for {set i 0} {$i < 150} {incr i} {",
        "  set p [lindex [mrd -force -value 0x1e148] 0]",
        "  if {$p != 1} { puts \"IDLE phase=$p\"; flush stdout; break }",
        "  if {($i % 10) == 0} { puts \"WAIT phase=$p\"; flush stdout }",
        "  after 200",
        "}",
    ]
    for index, value in enumerate(net):
        if index == 1:
            continue
        lines.append(f"w32 0x{NET_ADDR + 4 * index:x} 0x{value:08x}")
    for index, value in enumerate(cloud):
        lines.append(f"w32 0x{CLOUD_ADDR + 4 * index:x} 0x{value:08x}")
    lines.append("puts CLOUD_CONFIG_WRITTEN; flush stdout")
    if tls_seed_address is not None:
        seed_words=[0x31444553,int(time.time()),*words_of(secrets.token_bytes(64),64)]
        for index in list(range(1,18))+[0]:
            lines.append(f"w32 0x{tls_seed_address+4*index:x} 0x{seed_words[index]:08x}")
        lines.append("puts TLS_SEED_WRITTEN; flush stdout")
    # Publish the request only after cloud credentials and optional TLS seed.
    lines.append("set cur [lindex [mrd -force -value 0x1e184] 0]")
    lines.append("incr cur")
    lines.append(f"w32 0x{NET_ADDR + 4:x} $cur")
    lines.append("puts [format \"NET_REQUEST=0x%08x\" $cur]; flush stdout")
    # Follow the setup state machine so the operator sees progress live.
    lines.append("set last \"\"")
    lines.append("for {set i 0} {$i < 300} {incr i} {")
    lines.append("  set p [lindex [mrd -force -value 0x1e148] 0]")
    lines.append("  set e [lindex [mrd -force -value 0x1e14c] 0]")
    lines.append("  set c [lindex [mrd -force -value 0x1e168] 0]")
    lines.append("  set s \"$p/$e/$c\"")
    lines.append("  if {$s ne $last} { puts \"TRACE phase/error/code=$s\"; flush stdout; set last $s }")
    if client_address is not None:
        lines.append(f"  if {{$p == 4 && [lindex [mrd -force -value 0x{client_address:x}] 0] == 4}} {{ puts \"CLOUD_READY\"; flush stdout; break }}")
    else:
        lines.append("  if {$p == 8 || $p == 4} { puts \"TLS_READY\"; flush stdout; break }")
    lines.append("  if {$p == 6} { puts \"FAULTED\"; flush stdout; break }")
    lines.append("  after 200")
    lines.append("}")
    lines.append("puts DONE; flush stdout")
    return "\n".join(lines) + "\n"


def saved_lengths(xsdb: Path, symbols: Path) -> dict:
    """Read back the credential lengths the firmware stored (best effort)."""
    addresses = {}
    for line in symbols.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ("saved_ssid_bytes", "saved_pass_bytes"):
            addresses[parts[2]] = int(parts[0], 16)
    if len(addresses) != 2:
        return {}
    script = (
        "connect\nafter 300\n"
        'targets -set -filter {name =~ "*Cortex-A9*#0"}\n'
        f"puts \"SAVED [mrd -force -value 0x{addresses['saved_ssid_bytes']:x} 2]\"\n"
    )
    try:
        out = subprocess.run([str(xsdb), "-eval", script], capture_output=True, text=True,
                             timeout=120).stdout
    except subprocess.TimeoutExpired:
        return {}
    for line in out.splitlines():
        if line.startswith("SAVED "):
            values = line.split()[1:]
            if len(values) == 2:
                return {"ssid_bytes": int(values[0]), "pass_bytes": int(values[1])}
    return {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true",
                        help="validate packing without touching the board")
    parser.add_argument("--xsdb", type=Path, default=XSDB)
    parser.add_argument("--env", action="store_true", help="read Z7_WIFI_SSID, Z7_WIFI_PASSWORD and Z7_CLOUD_KEY without prompts")
    parser.add_argument("--credentials-stdin", action="store_true", help="read one private JSON record without console echo")
    parser.add_argument("--symbols", type=Path, help="symbol table for the ELF actually loaded")
    parser.add_argument("--mode", choices=("zhzh", "zhen", "enzh", "enen"), default=PROFILE.get("mode"))
    parser.add_argument("--model", choices=("asr", "translate"), help="defaults to the selected mode's model")
    parser.add_argument("--speech", action="store_true",help="use the translated-audio ELF symbol table")
    parser.add_argument("--yes", action="store_true", help="apply the requested configuration without a second confirmation")
    args = parser.parse_args()
    if args.mode:
        expected_model = "asr" if args.mode in ("zhzh", "enen") else "translate"
        if args.model and args.model != expected_model:
            parser.error("Model does not match the selected language mode")
        args.model = expected_model
        if args.symbols is None and args.mode in ("enzh", "enen"):
            args.symbols = ROOT / "output" / args.mode / "microphone" / "symbols.log"
        if args.symbols:
            build_record = json.loads(args.symbols.with_name("build.json").read_text(encoding="utf-8"))
            if build_record.get("Mode") != args.mode:
                parser.error("ELF symbols belong to another language mode")
    args.model = args.model or "translate"


    if args.credentials_stdin:
        from network_connect import read_credentials_line
        private = json.loads(read_credentials_line())
        ssid, password, key = (private[name].encode() for name in ("ssid", "password", "key"))
        private.clear()
    else:
        ssid = (os.environ.get("Z7_WIFI_SSID", "") if args.env else input("Wi-Fi SSID: ").strip()).encode()
        password = (os.environ.get("Z7_WIFI_PASSWORD", "") if args.env else getpass.getpass("Wi-Fi password: ")).encode()
        key = (os.environ.get("Z7_CLOUD_KEY", "") if args.env else getpass.getpass("Bailian API key: ")).encode()
    host = re.search(r'#define CLOUD_HOST "(ws-[^.]+)\.', (ROOT / 'software/network_ui/network.c').read_text(encoding='utf-8'))
    workspace_default = host.group(1) if host else ''
    workspace = ((os.environ.get("Z7_CLOUD_WORKSPACE", "") if args.env else "" if args.credentials_stdin else input(f"Bailian workspace id [{workspace_default}]: ").strip()) or workspace_default).encode()
    default_path = "/api-ws/v1/realtime?model=qwen3-asr-flash-realtime" if args.model == "asr" else DEFAULT_PATH
    path = (os.environ.get("Z7_CLOUD_PATH", "") if args.env else "" if args.credentials_stdin else input(f"Model path [{default_path}]: ").strip()) or default_path
    path = path.encode()

    if not 1 <= len(ssid) <= 32:
        raise SystemExit("SSID must be 1..32 bytes")
    if not 8 <= len(password) <= 64:
        raise SystemExit("Wi-Fi password must be 8..64 bytes")
    if not workspace.startswith(b"ws-"):
        raise SystemExit("workspace must start with ws-")
    if not key:
        raise SystemExit("API key required")
    if len(key) >= 192 or len(workspace) >= 48 or len(path) >= 96:
        raise SystemExit("configuration exceeds the board mailbox; values will not be truncated")
    # A pasted-twice password passes the length check but the module answers
    # CWJAP FAIL, which looks like a band/security problem for hours.
    if len(password) % 2 == 0 and password[:len(password) // 2] == password[len(password) // 2:]:
        raise SystemExit(f"the password looks like the same {len(password) // 2} characters "
                         "entered twice; re-run and type/paste it once")

    print(f"SSID: {len(ssid)} bytes")
    print(f"Wi-Fi password: {len(password)} bytes")
    print(f"workspace: {len(workspace)} bytes, API key: {len(key)} bytes, "
          f"model path: {len(path)} bytes")

    if args.dry_run:
        print(f"dry-run: net_words=32 cloud_words="
              f"{len([MAGIC_CLOUD] + words_of(path[:95], 96) + words_of(workspace[:47], 48) + words_of(key[:191], 192))}")
        return 0

    if not args.yes and input("write these to the board? [y/N]: ").strip().lower() != "y":
        raise SystemExit("cancelled before writing")

    symbol_dir = 'cloud_app_zhen_speech' if args.speech else 'cloud_app' if args.model == 'asr' else 'cloud_app_zhen'
    symbols = args.symbols or ROOT / 'output' / symbol_dir / 'symbols.log'
    client_address = next((int(p[0],16) for line in symbols.read_text().splitlines() if len(p:=line.split())==3 and p[2]=='client'),None)
    tls_seed_address = next((int(p[0],16) for line in symbols.read_text().splitlines() if len(p:=line.split())==3 and p[2]=='arm_tls_seed'),None)
    script_path = ROOT / "tmp" / "cloud_repro" / "configure_zhzh.tcl"
    script_path.parent.mkdir(parents=True, exist_ok=True)
    runtime_env = dict(os.environ)
    runtime_env["Z7_RUNTIME_TCL"] = tcl_script(ssid, password, path, workspace, key, client_address, tls_seed_address)
    script_path.write_text('if {[catch {eval $::env(Z7_RUNTIME_TCL)}]} {puts stderr "RUNTIME_CONFIGURATION_FAILED"; exit 1}\n', encoding="ascii")
    ready = False
    try:
        process = subprocess.Popen([str(args.xsdb), str(script_path)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=runtime_env)
        for line in process.stdout:
            line = line.strip()
            if line:
                print(line, flush=True)
            if line in {"TLS_READY","CLOUD_READY"}:
                ready = True
                break
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()
    finally:
        script_path.unlink(missing_ok=True)

    lengths = saved_lengths(args.xsdb,symbols)
    if lengths:
        print(f"stored credentials: ssid={lengths['ssid_bytes']} "
              f"pass={lengths['pass_bytes']} bytes")
    return 0 if ready else 1


if __name__ == "__main__":
    sys.exit(main())
