"""Bounded local WAV -> English ASR -> simplified Chinese, with no network calls."""
import io,time,wave
import runtime
MAX_SECONDS=30
MAX_WAV_BYTES=16000*2*MAX_SECONDS+4096
VAD_OPTIONS={'threshold':0.5,'min_speech_duration_ms':0,
             'min_silence_duration_ms':2000,'speech_pad_ms':400}

def decode_wav(data):
    if not 44<=len(data)<=MAX_WAV_BYTES:raise ValueError('WAV byte limit')
    try:
        with wave.open(io.BytesIO(data),'rb') as audio:
            frames=audio.getnframes()
            if audio.getnchannels()!=1 or audio.getframerate()!=16000 or audio.getsampwidth()!=2 or audio.getcomptype()!='NONE':
                raise ValueError('Require 16 kHz mono 16-bit PCM WAV')
            if not 1<=frames<=16000*MAX_SECONDS:raise ValueError('Audio duration limit')
            raw=audio.readframes(frames+1)
            if len(raw)!=2*frames:raise ValueError('Truncated WAV')
    except (wave.Error,EOFError,RuntimeError) as error:
        # wave._Chunk.seek can raise RuntimeError for an invalid RIFF chunk.
        # Normalize container parsing errors before any model is invoked.
        raise ValueError('Invalid WAV container') from error
    return raw,frames/16000

class LocalInterpreter:
    def __init__(self,threads=8):
        runtime.bootstrap();self.manifest=runtime.verified_models()
        import numpy as np,torch
        from faster_whisper import WhisperModel
        from transformers import AutoTokenizer,AutoModelForSeq2SeqLM
        self.vad_asset=runtime.verified_vad()
        self.np=np;self.torch=torch;torch.set_num_threads(threads)
        started=time.perf_counter()
        self.asr=WhisperModel(str(runtime.OUT/'models/asr'),device='cpu',compute_type='int8',cpu_threads=threads,
                              num_workers=1,local_files_only=True)
        self.tokenizer=AutoTokenizer.from_pretrained(str(runtime.OUT/'models/mt'),local_files_only=True,trust_remote_code=False)
        mt_names={item['Name'] for item in self.manifest['Models']['mt']['Files']}
        self.mt=AutoModelForSeq2SeqLM.from_pretrained(str(runtime.OUT/'models/mt'),local_files_only=True,
            trust_remote_code=False,weights_only=True,use_safetensors='model.safetensors' in mt_names)
        if self.mt.config.model_type!='marian':raise RuntimeError('Unexpected translation architecture')
        self.mt.to(device='cpu',dtype=torch.float32);self.mt.eval()
        self.load_seconds=time.perf_counter()-started

    def transcribe_translate(self,data):
        raw,seconds=decode_wav(data)
        waveform=self.np.frombuffer(raw,dtype='<i2').astype(self.np.float32)/32768.0
        # Exact digital silence contains no speech information. This is an input
        # guard, not evidence of VAD quality in real microphone/background noise.
        if not self.np.any(waveform):
            return {'AudioSeconds':seconds,'NoSpeech':True,'SilenceGuard':'exact_zero_pcm',
                    'Transcript':'','Chinese':'','Segments':[],'ASRSeconds':0,'MTSeconds':0}
        started=time.perf_counter()
        segments,info=self.asr.transcribe(waveform,language='en',task='transcribe',beam_size=5,temperature=0,
            condition_on_previous_text=False,vad_filter=True,vad_parameters=dict(VAD_OPTIONS),max_new_tokens=128)
        segments=list(segments) # Consumption executes the lazy model generator.
        asr_seconds=time.perf_counter()-started
        source=' '.join(segment.text.strip() for segment in segments).strip()
        result={'AudioSeconds':seconds,'NoSpeech':not source,'Language':'en','LanguageForced':True,
                'VADEnabled':True,'VADAudioSeconds':info.duration_after_vad,
                'Transcript':source,'Segments':[{'Start':s.start,'End':s.end,'Text':s.text.strip()} for s in segments],
                'ASRSeconds':asr_seconds,'MTSeconds':0,'Chinese':''}
        if not source:return result
        started=time.perf_counter()
        inputs=self.tokenizer('>>cmn_Hans<< '+source,return_tensors='pt',truncation=False)
        if inputs['input_ids'].shape[-1]>512:raise ValueError('Translation input limit; segment the source first')
        with self.torch.inference_mode():
            output=self.mt.generate(**inputs,num_beams=4,do_sample=False,max_new_tokens=256)
        if output[0,-1].item()!=self.tokenizer.eos_token_id:raise RuntimeError('Translation did not finish within its token budget')
        result.update(Chinese=self.tokenizer.decode(output[0],skip_special_tokens=True).strip(),
                      MTSeconds=time.perf_counter()-started)
        if not result['Chinese']:raise RuntimeError('Empty translation for nonempty source')
        return result
