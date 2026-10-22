"""Start the loopback viewer quietly and open it in the user's browser."""
import json,subprocess,sys,time,socket,urllib.request,webbrowser
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
URL='http://127.0.0.1:8787'
def ready():
    try:
        with urllib.request.urlopen(URL+'/state',timeout=1) as r:d=json.load(r)
        return d.get('board')=='192.168.1.50'
    except (OSError,ValueError):return False
def main():
    if not ready():
        probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);probe.connect(('192.168.1.50',4177));local=probe.getsockname()[0];probe.close()
        out=ROOT/'output/remote_display';out.mkdir(parents=True,exist_ok=True)
        with (out/'server.log').open('ab') as log:
            subprocess.Popen([sys.executable,str(ROOT/'scripts/remote_display_server.py'),'--local-ip',local],cwd=ROOT,stdout=log,stderr=log,creationflags=0x08000000|0x00000008 if sys.platform=='win32' else 0,close_fds=True)
        end=time.monotonic()+8
        while not ready():
            if time.monotonic()>end:raise RuntimeError('Local viewer did not start; see output/remote_display/server.log')
            time.sleep(.2)
    webbrowser.open(URL)
if __name__=='__main__':main()
