"""Native host tests only: mock cloud/model/DAC; never opens the board."""
import argparse,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default='gcc');a=p.parse_args()
 out=ROOT/'tmp/voice_unit';out.mkdir(parents=True,exist_ok=True)
 source=ROOT/'software/network_ui';suffix='.exe' if os.name=='nt' else ''
 common=[a.cc,'-O2','-std=c11','-I',str(source)]
 control=out/('controller'+suffix)
 subprocess.run([*common,'-Wall','-Wextra','-Werror','-DZ7_LOCAL_AI','-DZ7_HISTORY_UI',str(Path(__file__).with_name('controller_test.c')),str(source/'local_voice.c'),'-o',str(control)],check=True)
 result=subprocess.check_output([str(control)]).decode().strip()
 frames=out/('frames'+suffix)
 subprocess.run([*common,'-ffunction-sections','-fdata-sections','-Wl,--gc-sections',str(Path(__file__).with_name('frames_test.c')),'-o',str(frames)],check=True)
 events=[json.loads(v) for v in subprocess.check_output([str(frames)]).decode('utf-8').splitlines()]
 assert [v['type'] for v in events]==['session.update','input_text_buffer.append','input_text_buffer.commit','session.finish']
 assert len({v['event_id'] for v in events})==4
 assert events[1]['text']=='Open "window".\n你好'
 report={'passed':True,'scope':'host controller and actual C frame serialization; model/cloud/DAC mocked','controller':result,'frame_checks':['unique IDs','session configuration','UTF8/quote/newline escaping','append/commit/finish order','240-byte input bound']}
 target=ROOT/'output/voice_local/unit_validation.json';target.parent.mkdir(exist_ok=True)
 target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(report,ensure_ascii=True))
if __name__=='__main__':main()
