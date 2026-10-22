#include "speech_playback.h"
#ifdef Z7_TRANSLATE_AUDIO
#include <string.h>
#ifdef Z7_MMU_CACHE
#include "../ps_bringup/cache.h"
#endif
#define SPEECH_SAMPLES 240000u /* Ten seconds at the model's 24 kHz, no speed-up. */
#define PREFILL_SAMPLES 48000u /* 2 s per response; completed short phrases start immediately. */
#define HW_SAMPLES 8192u
#ifdef Z7_TEXT_ONLY_RESPONSE
#define SPEECH_DAC_SELECT 0u /* Keep the existing PL microphone loopback on LINE OUT. */
#else
#define SPEECH_DAC_SELECT 1u
#endif
#ifndef SPEECH_REG
#define SPEECH_REG(offset) (*(volatile uint32_t *)(0x43c20000u+(offset)))
#endif
#ifndef SPEECH_WRITE
#define SPEECH_WRITE(offset,value) (SPEECH_REG(offset)=(value))
#endif
__attribute__((section(".playback_runtime"))) static int16_t speech_pcm[SPEECH_SAMPLES];
struct speech_diagnostics {
    uint32_t magic,chunks,samples,to_hardware,queue_peak,overflow_chunks,invalid_chunks;
    uint32_t starts,underruns,played,hardware_count,software_count,unsupported;
    uint32_t last_chunk_ms,max_gap_ms,last_chunk_samples,min_chunk_samples,done_events;
};
__attribute__((section(".cloud_runtime"),used)) static volatile struct speech_diagnostics speech_diag;
static uint32_t play_now;
static uint32_t output_enabled=1,output_volume=100,output_speed=100,speed_phase;
static uint32_t voice_ms,voice_valid;
/* Bounded metadata timeline, read only after a quiet playback test. */
#ifdef Z7_LATENCY_PROBE
#define SPEECH_TRACE_ENTRIES 2048u
#else
#define SPEECH_TRACE_ENTRIES 128u
#endif
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t speech_timeline[1+SPEECH_TRACE_ENTRIES*4];
static void speech_trace(uint32_t kind,uint32_t value,uint32_t depth) {
    uint32_t i=speech_timeline[0];
    if(i>=SPEECH_TRACE_ENTRIES) return;
    speech_timeline[1+i*4]=play_now;
    speech_timeline[2+i*4]=kind;
    speech_timeline[3+i*4]=value;
    speech_timeline[4+i*4]=depth;
    speech_timeline[0]=i+1u;
}
static uint32_t read_at,write_at,queued,finished,playing,available,report_ms;
/* Keep sentence boundaries even when the next response arrives before the
   previous one has drained. Only the current response may enter the DAC FIFO. */
static uint32_t stream_ends[128],stream_audio_ms[128],end_head,end_tail,stream_start;
#ifdef Z7_LOW_LATENCY_PLAYBACK
#define FAST_PREFILL_SAMPLES 30720u /* 1280 ms, still prime the physical FIFO. */
static uint32_t stream_received[128],stream_first_size[128],stream_prefill[128],stream_rebuffer[128];
#endif
static int b64(unsigned c) {
    if(c>='A' && c<='Z') return (int)c-'A';
    if(c>='a' && c<='z') return (int)c-'a'+26;
    if(c>='0' && c<='9') return (int)c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}
void speech_playback_init(void) {
    read_at=write_at=queued=finished=playing=report_ms=play_now=0;
    end_head=end_tail=stream_start=0;
    speed_phase=voice_ms=voice_valid=0;
    memset((void *)&speech_diag,0,sizeof(speech_diag));speech_diag.magic=0x31594c50u;
    memset((void *)speech_timeline,0,sizeof(speech_timeline));
    available=SPEECH_REG(0x98)==0x31594c50u;
    speech_diag.unsupported=!available;
    if(available) SPEECH_WRITE(0x9c,SPEECH_DAC_SELECT|4u); /* Clear translated FIFO and select the output. */
}
void speech_playback_utterance(void) {
    voice_valid=0;
}
void speech_playback_settings(uint32_t enabled,uint32_t volume,uint32_t speed_percent) {
    if(volume>100u) volume=100u;
    if(speed_percent!=80u && speed_percent!=100u && speed_percent!=120u) speed_percent=100u;
    if(output_enabled && !enabled) {
        /* Explicit speech-off discards its tail instead of replaying stale words. */
        read_at=write_at=queued=finished=playing=0;speed_phase=0;
        end_head=end_tail=stream_start=0;
        speech_diag.samples=speech_diag.to_hardware=0;
        voice_valid=0;
        if(available) SPEECH_WRITE(0x9c,SPEECH_DAC_SELECT|4u);
    }
    output_enabled=!!enabled;output_volume=volume;output_speed=speed_percent;
}
void speech_playback_status(struct speech_status *status) {
    uint32_t count=available?SPEECH_REG(0xa4):0;
    status->state=!available?4u:(!output_enabled?0u:(playing?3u:((queued||count)?2u:1u)));
    status->latency_ms=voice_ms;status->latency_valid=voice_valid;
    status->buffer_ms=(queued*100u/output_speed+count)/24u;
}
int speech_playback_audio(const char *s,uint32_t n) {
    if(!n) return 1;
    if(n%4u) {++speech_diag.invalid_chunks;return 0;}
    uint32_t padding=(s[n-1u]=='=')+(n>1u && s[n-2u]=='=');
    uint32_t bytes=n/4u*3u-padding;
    if(bytes%2u) {++speech_diag.invalid_chunks;return 0;}
    /* Validate before writing: malformed/oversized chunks cannot partially
       poison queued PCM or read past the event's JSON string. */
    for(uint32_t i=0;i<n;++i) {
        if(i>=n-padding) {if(s[i]!='=') {++speech_diag.invalid_chunks;return 0;}}
        else if(b64((uint8_t)s[i])<0) {++speech_diag.invalid_chunks;return 0;}
    }
    if((padding==1u && (b64((uint8_t)s[n-2u])&3)) ||
       (padding==2u && (b64((uint8_t)s[n-3u])&15))) {++speech_diag.invalid_chunks;return 0;}
    if(!output_enabled) return 1;
    if(bytes/2u>SPEECH_SAMPLES-queued) {++speech_diag.overflow_chunks;return 0;}
    uint32_t stream=end_tail%128u;
    if(finished || !speech_diag.samples) {
        stream_audio_ms[stream]=play_now;
#ifdef Z7_LOW_LATENCY_PLAYBACK
        stream_received[stream]=0;stream_first_size[stream]=bytes/2u;
        stream_prefill[stream]=PREFILL_SAMPLES;stream_rebuffer[stream]=0;
#endif
    }
#ifdef Z7_LOW_LATENCY_PLAYBACK
    stream_received[stream]+=bytes/2u;
    /* Use the actual receive cadence after the first chunk. A slow cloud/link
       retains the two-second reserve; fast delivery can start after 1280 ms.
       Nothing changes sample values, ordering, playback speed or pitch. */
    uint32_t after_first=stream_received[stream]-stream_first_size[stream];
    uint32_t supplied_ms=(after_first*100u/output_speed)/24u;
    stream_prefill[stream]=!stream_rebuffer[stream] && after_first &&
        play_now-stream_audio_ms[stream]<=supplied_ms+80u?FAST_PREFILL_SAMPLES:PREFILL_SAMPLES;
#endif
    uint32_t low=0,decoded=0;
    for(uint32_t i=0;i<n;i+=4u) {
        uint32_t v=(uint32_t)b64((uint8_t)s[i])<<18|(uint32_t)b64((uint8_t)s[i+1u])<<12;
        if(s[i+2u]!='=') v|=(uint32_t)b64((uint8_t)s[i+2u])<<6;
        if(s[i+3u]!='=') v|=(uint32_t)b64((uint8_t)s[i+3u]);
        for(uint32_t j=0;j<3u && decoded<bytes;++j,++decoded) {
            uint32_t value=(v>>(16u-j*8u))&255u;
            if(!(decoded&1u)) low=value;
            else {
                speech_pcm[write_at]=(int16_t)(low|(value<<8));
                if(++write_at==SPEECH_SAMPLES) write_at=0;
                ++queued;
            }
        }
    }
    if(speech_diag.chunks && !finished) {
        uint32_t gap=play_now-speech_diag.last_chunk_ms;
        if(gap>speech_diag.max_gap_ms) speech_diag.max_gap_ms=gap;
    }
    speech_diag.last_chunk_ms=play_now;speech_diag.last_chunk_samples=bytes/2u;
    if(!speech_diag.chunks || bytes/2u<speech_diag.min_chunk_samples) speech_diag.min_chunk_samples=bytes/2u;
    ++speech_diag.chunks;speech_diag.samples+=bytes/2u;
    if(queued>speech_diag.queue_peak) speech_diag.queue_peak=queued;
    finished=0;
    speech_trace(1u,bytes/2u,queued+SPEECH_REG(0xa4));
    return 1;
}
static void finish_stream(void) {
    uint32_t end=speech_diag.samples;
    uint32_t previous=end_tail!=end_head?stream_ends[(end_tail-1u)%128u]:stream_start;
    if(end!=previous) {
        if(end_tail-end_head>=128u) {++speech_diag.overflow_chunks;return;}
        stream_ends[end_tail++%128u]=end;
    }
    finished=1;
}
void speech_playback_done(void) {finish_stream();++speech_diag.done_events;speech_trace(2u,0u,queued+SPEECH_REG(0xa4));}
void speech_playback_disconnect(void) {finish_stream();}
/* A user stop discards both queues; the next session gets fresh playback
   counters. A network disconnect can still drain its completed phrase. */
void speech_playback_stop(void) {speech_playback_init();}
void speech_playback_tick(uint32_t now) {
    play_now=now;
    if(!available || !output_enabled) return;
    uint32_t count=SPEECH_REG(0xa4);
    uint32_t complete=end_head!=end_tail;
    uint32_t limit=complete?stream_ends[end_head%128u]:speech_diag.samples;
    if(complete && speech_diag.to_hardware==limit && !count) {
        speech_trace(5u,limit,queued);
        stream_start=limit;++end_head;playing=0;speed_phase=0;
        /* Finish the old sample's stereo frame before starting another voice. */
        SPEECH_WRITE(0x9c,SPEECH_DAC_SELECT);return;
    }
    if(playing && !count && !complete) {
        playing=0;speech_trace(4u,0u,queued);
#ifdef Z7_LOW_LATENCY_PLAYBACK
        stream_rebuffer[end_head%128u]=1;stream_prefill[end_head%128u]=PREFILL_SAMPLES;
#endif
    }
    uint32_t budget=HW_SAMPLES-count;
    if(budget>512u) budget=512u;
    uint32_t remaining=limit-speech_diag.to_hardware;
    while(budget-- && queued && remaining) {
        int32_t value=speech_pcm[read_at];
        SPEECH_WRITE(0xa0,(uint16_t)(int16_t)(value*(int32_t)output_volume/100));
        ++count;speed_phase+=output_speed;
        uint32_t consume=speed_phase/100u;speed_phase%=100u;
        if(consume>remaining) consume=remaining;
        while(consume--) {
            if(++read_at==SPEECH_SAMPLES) read_at=0;
            --queued;--remaining;++speech_diag.to_hardware;
        }
    }
    /* The software reservoir cannot feed the DAC during a busy TLS/render
       slice. Prime the physical FIFO before run; 512 samples cover only 21 ms. */
    uint32_t prefill=PREFILL_SAMPLES;
#ifdef Z7_LOW_LATENCY_PLAYBACK
    prefill=stream_prefill[end_head%128u];
    if(!prefill) prefill=PREFILL_SAMPLES;
#endif
    if(!playing && (count+remaining*100u/output_speed>=prefill || (complete && count)) &&
       (!remaining || count==HW_SAMPLES)) {
        playing=1;++speech_diag.starts;
        /* T2 is precisely first received PCM -> DAC run, not acoustic latency. */
        voice_ms=now-stream_audio_ms[end_head%128u];voice_valid=1;
        speech_trace(3u,0u,count+remaining);
    }
    SPEECH_WRITE(0x9c,SPEECH_DAC_SELECT|(playing?2u:0u)|((complete && !remaining)?8u:0u));
    if(now-report_ms<200u) return;
    report_ms=now;speech_diag.hardware_count=count;speech_diag.software_count=queued;
    speech_diag.underruns=SPEECH_REG(0xa8);speech_diag.played=SPEECH_REG(0xac);
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)&speech_diag,sizeof(speech_diag));
    cache_clean_range((const void *)speech_timeline,sizeof(speech_timeline));
#endif
}
#else
void speech_playback_init(void) {}
int speech_playback_audio(const char *s,uint32_t n) {(void)s;(void)n;return 1;}
void speech_playback_done(void) {}
void speech_playback_disconnect(void) {}
void speech_playback_stop(void) {}
void speech_playback_tick(uint32_t now) {(void)now;}
void speech_playback_settings(uint32_t e,uint32_t v,uint32_t s) {(void)e;(void)v;(void)s;}
void speech_playback_utterance(void) {}
void speech_playback_status(struct speech_status *s) {s->state=s->latency_ms=s->latency_valid=s->buffer_ms=0;}
#endif
