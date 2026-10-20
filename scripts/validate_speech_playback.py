"""Native translated-audio and UART duplex checks; no board, cloud or key."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--gcc',required=True,type=Path)
    args=parser.parse_args()
    temp=ROOT/'tmp/speech_playback';temp.mkdir(parents=True,exist_ok=True)
    out=ROOT/'output/zhen';out.mkdir(parents=True,exist_ok=True)
    cases=[('speech_playback',['-DZ7_TRANSLATE_AUDIO'],['tests/speech_playback_test.c','software/network_ui/cloud_events.c','software/network_ui/cloud_wire.c','software/network_ui/cloud_frame.c']),
           ('network_duplex',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO'],['tests/network_duplex_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c'])]
    cases.append(('network_tls',['-DZ7_NETWORK_CAPABILITIES'],['tests/network_tls_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_tls_audio',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO'],['tests/network_tls_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_speech_recovery',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO'],['tests/network_speech_recovery_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_speech_passive',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO'],['tests/network_speech_passive_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_speech_drain_first',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO','-DZ7_SSL_DRAIN_FIRST'],['tests/network_speech_passive_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_speech_no_length',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO','-DZ7_SSL_DRAIN_FIRST','-DZ7_SSL_NO_LENGTH_QUERY'],['tests/network_speech_no_length_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_tls_raw',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO','-DZ7_TLS_PASSTHROUGH'],['tests/network_tls_raw_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    cases.append(('network_arm_tcp',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO','-DZ7_TLS_PASSTHROUGH','-DZ7_ARM_TLS'],['tests/network_tls_raw_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    results={}
    cases.append(('wifi_recover',[],['tests/wifi_recover_test.c','software/network_ui/wifi_recover.c']))
    cases.append(('wifi_recover_921600',['-DZ7_UART_LEGACY_STREAM'],['tests/wifi_recover_test.c','software/network_ui/wifi_recover.c']))
    cases.append(('wifi_recover_460800',['-DZ7_UART_460800_STREAM'],['tests/wifi_recover_test.c','software/network_ui/wifi_recover.c']))
    cases.append(('wifi_recover_1m',['-DZ7_UART_1M_STREAM'],['tests/wifi_recover_test.c','software/network_ui/wifi_recover.c']))
    cases.append(('network_framing_recovery',['-DZ7_NETWORK_CAPABILITIES','-DZ7_TRANSLATE_AUDIO'],['tests/network_framing_recovery_test.c','software/network_ui/network.c','software/network_ui/protocol.c','software/subtitle_ui/ui_model.c','output/subtitle_ui/ui_assets.c']))
    for _,_,sources in cases:
        if 'software/network_ui/network.c' in sources: sources.append('software/network_ui/wifi_recover.c')
    for name,defines,sources in cases:
        exe=temp/(name+'.exe')
        command=[str(args.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',*defines,
                 '-I',str(ROOT/'third_party/wslay/lib/includes'),'-I',str(ROOT/'software/network_ui'),
                 '-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/s) for s in sources],'-o',str(exe)]
        compiled=subprocess.run(command,capture_output=True,timeout=60)
        (out/(name+'_build.log')).write_bytes(compiled.stdout+compiled.stderr);compiled.check_returncode()
        tested=subprocess.run([str(exe)],capture_output=True,timeout=30)
        (out/(name+'_native.log')).write_bytes(tested.stdout+tested.stderr);tested.check_returncode()
        results[name]=tested.stdout.decode().strip()
    inputs=sorted(set(s for _,_,sources in cases for s in sources)|{'software/network_ui/speech_playback.c','software/network_ui/speech_playback.h','software/network_ui/cloud_events.h'})
    record={'status':'passed','tests':results,'scope':'native real WebSocket/JSON/Base64 to simulated DAC, buffer pacing and full-duplex IPD/AT framing; not subjective speech quality',
            'input_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}}
    (out/'speech_validation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(results))

if __name__=='__main__':main()
