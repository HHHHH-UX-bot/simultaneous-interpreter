"""Exercise the ARM TLS bootstrap against a bounded UART model; no device or key."""
import hashlib,json,re,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=('software/network_ui/network.c','software/network_ui/network.h',
    'software/network_ui/protocol.c','software/network_ui/protocol.h',
    'software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_model.h',
    'output/subtitle_ui/ui_assets.c','tests/network_runtime_test.c','tests/network_tls_test.c',
    'scripts/validate_z7_nano_tls_boot.py','scripts/probe_z7_nano_tls_boot.py',
    'scripts/z7_nano_tls_boot.tcl','tests/test_tls_boot_observer.py')
def main():
    folder=ROOT/'tmp/z7_nano/tls_boot';folder.mkdir(parents=True,exist_ok=True)
    out=ROOT/'output/z7_nano/tls_boot';out.mkdir(parents=True,exist_ok=True)
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
    exe=folder/'tls_boot.exe'
    sources=('software/network_ui/network.c','software/network_ui/protocol.c',
        'software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c','tests/network_tls_test.c')
    args=['D:/cenvironment/mingw64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-O2','-DZ7_NETWORK_CAPABILITIES',
        '-I',str(ROOT/'software/network_ui'),'-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/p) for p in sources],'-o',str(exe)]
    built=subprocess.run(args,capture_output=True,timeout=30);(folder/'build.log').write_bytes(built.stdout+built.stderr);built.check_returncode()
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
    (folder/'tests.log').write_text(tested.stdout+tested.stderr,encoding='utf-8');tested.check_returncode()
    observer=subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'tests/test_tls_boot_observer.py')],capture_output=True,text=True,timeout=20)
    (folder/'observer_tests.log').write_text(observer.stdout+observer.stderr,encoding='utf-8');observer.check_returncode()
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
    checks=int(re.findall(r'checks=(\d+)',tested.stdout)[-1])
    (out/'host_validation.json').write_text(json.dumps({'status':'passed','checked_utc':datetime.now(timezone.utc).isoformat(),
        'input_hashes':hashes,'assertions':checks,'observer_tests':3,'scope':'Native ARM TLS AT setup, legacy LAN and observer process ownership; not physical TLS, WSS or cloud acceptance'},indent=2)+'\n',encoding='utf-8')
    print('PASS_NETWORK_TLS',checks)
if __name__=='__main__':main()
