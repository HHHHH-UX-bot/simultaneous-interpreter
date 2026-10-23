"""Native tests for public Wi-Fi identity, shared UI and full-text exports."""
import argparse,ast,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--gcc',required=True,type=Path);a=p.parse_args()
 out=ROOT/'output/network_history';out.mkdir(parents=True,exist_ok=True)
 work=ROOT/'tmp/network_history';work.mkdir(parents=True,exist_ok=True)
 sources=['tests/network_history/identity_ui_test.c','software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c','output/subtitle_ui/ui_assets.c']
 exe=work/'identity_ui_test.exe'
 subprocess.run([str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DZ7_ARM_TLS','-DZ7_TRANSLATE_EN','-DZ7_LOCAL_AI','-DZ7_TINYQWEN','-DZ7_SPEAKER_CNN','-I',str(ROOT/'software/subtitle_ui'),'-I',str(ROOT/'software/z7_nano_usb'),*[str(ROOT/s) for s in sources],'-o',str(exe)],cwd=ROOT,check=True)
 checks=[subprocess.check_output([str(exe)],cwd=ROOT).decode().strip()]
 for script,args in [('tests/network_history/export_test.py',[]),('tests/history_save_test.py',[]),('scripts/validate_remote_display.py',['--gcc',str(a.gcc)])]:
  result=subprocess.run([sys.executable,'-B',str(ROOT/script),*args],cwd=ROOT,capture_output=True);checks.append((result.stdout+result.stderr).decode('utf-8',errors='replace').strip());result.check_returncode()
 for script in ['remote_display_server.py','board_llm_web.py','board_llm_client.py']:ast.parse((ROOT/'scripts'/script).read_text(encoding='utf-8'))
 inputs=sources+['software/network_ui/wifi_identity.h','software/subtitle_ui/ui_model.h','software/network_ui/network.c','software/network_ui/remote_display.c','software/network_ui/tiny_board_llm.c','software/network_ui/llm_ui_bridge.c','scripts/remote_display_server.py','scripts/board_llm_web.py','scripts/board_llm_client.py','tests/network_history/export_test.py','assets/remote_display.html']
 (out/'native_validation.json').write_text(json.dumps(dict(passed=True,scope='Actual C AT association parser, four UI pages and bounds; CSV/TXT/JSON paragraph assembly and save; no physical hardware claim',checks=checks,input_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in inputs}),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('PASS_NETWORK_HISTORY_NATIVE',flush=True)
if __name__=='__main__':main()
