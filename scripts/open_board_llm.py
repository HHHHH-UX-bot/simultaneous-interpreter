"""Open the loopback text console; the PC never loads model weights."""
import json,subprocess,sys,time,urllib.request,webbrowser
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
URL='http://127.0.0.1:8790'
def running():
    try:
        with urllib.request.urlopen(URL+'/api/status',timeout=1) as reply:
            return 'connected' in json.load(reply)
    except (OSError,ValueError):return False
def main():
    if not running():
        folder=ROOT/'output/tinyqwen';folder.mkdir(parents=True,exist_ok=True)
        with (folder/'web_stdout.log').open('ab') as out,(folder/'web_stderr.log').open('ab') as err:
            subprocess.Popen([sys.executable,'-B',str(ROOT/'scripts/board_llm_web.py')],cwd=ROOT,
                stdout=out,stderr=err,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        deadline=time.perf_counter()+8
        while not running():
            if time.perf_counter()>deadline:raise RuntimeError('Text console did not start; check output/tinyqwen/web_stderr.log')
            time.sleep(.2)
    webbrowser.open(URL);print(URL)
if __name__=='__main__':main()
