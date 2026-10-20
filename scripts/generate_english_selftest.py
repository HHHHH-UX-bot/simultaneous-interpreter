"""Generate reproducible-input English TTS using an already installed voice."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave

ROOT=Path(__file__).resolve().parents[1]
SENTENCES=['Good morning.', 'The weather is very nice today.',
           'I would like a cup of coffee.', 'Please show these words on the screen.']
def main():
    temp=ROOT/'tmp/english_selftest';temp.mkdir(parents=True,exist_ok=True)
    out=ROOT/'output/selftest_en';out.mkdir(parents=True,exist_ok=True)
    pcm=bytearray()
    for i,text in enumerate(SENTENCES):
        path=temp/('sentence_%d.wav'%i)
        script="Add-Type -AssemblyName System.Speech;$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;$s.SelectVoice('Microsoft Zira Desktop');$f=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono);$s.SetOutputToWaveFile('%s',$f);$s.Speak('%s');$s.Dispose()"%(path,text)
        subprocess.run(['powershell','-NoProfile','-Command',script],check=True,capture_output=True,timeout=30)
        with wave.open(str(path),'rb') as wav:
            assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate())==(1,2,16000)
            pcm.extend(wav.readframes(wav.getnframes()))
        pcm.extend(bytes(25600)) # 0.8 seconds between utterances.
    with wave.open(str(out/'reference.wav'),'wb') as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(16000);wav.writeframes(pcm)
    values=struct.unpack('<%dh'%(len(pcm)//2),pcm)
    lines=['/* Generated English TTS: Zira, 16 kHz mono s16le. */','#include <stdint.h>','const int16_t selftest_pcm[] = {']
    lines += ['    '+','.join(map(str,values[i:i+16]))+',' for i in range(0,len(values),16)]
    lines += ['};','const uint32_t selftest_samples = %du;'%len(values),'const uint32_t selftest_speech_ms = %du;'%(len(values)//16)]
    (out/'selftest_pcm.c').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    record={'voice':'Microsoft Zira Desktop','language':'en-US','sentences':SENTENCES,'sample_rate':16000,'channels':1,'seconds':len(values)/16000,'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'scope':'generated synthetic English input, never recorded from a microphone'}
    (out/'reference.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('PASS_ENGLISH_TTS',len(values),'samples',record['seconds'],'seconds')
if __name__=='__main__':main()
