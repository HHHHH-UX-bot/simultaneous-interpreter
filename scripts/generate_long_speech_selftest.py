"""Six continuous complex sentences for automatic board/PC latency comparison."""
import hashlib,json,re,struct,wave
from pathlib import Path
import generate_selftest_audio as speech
ROOT=Path(__file__).resolve().parents[1]
SENTENCES=[
    '虽然今天的道路比较拥堵，我们仍然需要按时到达会议地点。',
    '如果设备能够及时处理连续语音，后面的字幕就不应该越来越慢。',
    '请把刚才讨论的技术方案整理清楚，然后说明具体的实施步骤。',
    '为了保证翻译内容完整，系统不能在网络繁忙的时候丢掉任何句子。',
    '即使一个人连续讲了很多内容，英文译音也应该按照正确的顺序输出。',
    '测试结束以后，请比较前后几句话的等待时间，并检查有没有明显卡顿。',
]
def main():
    folder=ROOT/'output/tts/long_speech_20261022/input';folder.mkdir(parents=True,exist_ok=True)
    speech.TMP=str(ROOT/'tmp/long_speech_20261022');speech.SENTENCES=SENTENCES
    paths=speech.synthesize(rate=1);pcm=bytearray();items=[]
    for text,path in zip(SENTENCES,paths):
        with wave.open(path,'rb') as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
            raw=w.readframes(w.getnframes())
        count=len(re.findall('[\u4e00-\u9fff]',text));assert count>=6
        start=len(pcm)//32;pcm.extend(raw);end=len(pcm)//32
        items.append(dict(text=text,chinese_characters=count,start_ms=start,end_ms=end))
        pcm.extend(bytes(16000*2*120//1000))
    samples=len(pcm)//2
    if samples*2>1600000:raise RuntimeError('Fixture too large for the existing application window')
    with wave.open(str(folder/'reference.wav'),'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(16000);w.writeframes(pcm)
    values=struct.unpack('<%dh'%samples,pcm)
    with (folder/'selftest_pcm.c').open('w',encoding='utf-8',newline='\n') as f:
        f.write('/* Synthetic six-sentence stress fixture; no microphone recording. */\n#include <stdint.h>\nconst int16_t selftest_pcm[]={\n')
        for i in range(0,samples,16):f.write(','.join(map(str,values[i:i+16]))+',\n')
        f.write('};\nconst uint32_t selftest_samples=%du;\nconst uint32_t selftest_speech_ms=%du;\n'%(samples,samples//16))
    record=dict(scope='synthetic six complex Chinese sentences, Windows Huihui rate 1, 120 ms extra gap; real-time upload, no fast-forward',sentences=items,sentence_count=len(items),speech_ms=samples//16,pcm_sha256=hashlib.sha256(pcm).hexdigest())
    (folder/'fixture.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS_LONG_SPEECH_FIXTURE',len(items),'sentences',samples//16,'ms',[i['chinese_characters'] for i in items])
if __name__=='__main__':main()
