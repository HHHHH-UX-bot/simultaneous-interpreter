#include "local_ai.h"
#include "tiny_features.h"
#include "tiny_model.h"
#include "cloud_live.h"
#include "speech_playback.h"
#ifdef Z7_MMU_CACHE
#include "../ps_bringup/cache.h"
#endif
#include <string.h>
#include <limits.h>
/* Audio never leaves this Cortex-A9 pipeline in local mode. Templates and
   output recordings are fixed phrases, not a general ASR/MT/TTS network. */
static struct ui_state *view;
static int16_t window[TINY_FRAME],utterance[300*TINY_DIM];
static uint32_t used,frames,quiet,voiced,segment,mode_epoch,now_ms,last_publish,overflow;
static uint32_t noise=2500;
#ifndef LOCAL_AI_READ
#define LOCAL_AI_READ(o) (*(volatile uint32_t *)(0x43c20000u+(o)))
#endif
static int64_t sums[12],squares[12],enroll_sum[12],enroll_square[12];
static int32_t profiles[4][24];
static uint32_t enroll_frames,enroll_slot,enroll_deadline;
#ifdef Z7_LOCAL_SELFTEST
extern const int16_t selftest_pcm[];
extern const uint32_t selftest_samples;
static uint32_t test_at,test_next;
#endif
static uint32_t ticks(void) {
#if defined(__arm__)
    return *(volatile uint32_t *)0xf8f00200u;
#else
    return 0;
#endif
}
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t local_ai_diag[20];
static uint32_t root64(uint64_t n) {
    uint64_t result=0,bit=(uint64_t)1<<62;
    while(bit>n)bit>>=2;
    while(bit) {if(n>=result+bit) {n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (uint32_t)result;
}
static void vector(int32_t *out,const int64_t *sum,const int64_t *square,uint32_t n) {
    for(uint32_t i=0;i<12u;++i) {
        int64_t mean=sum[i]/n,variance=square[i]/n-mean*mean;
        out[i]=(int32_t)mean;out[12u+i]=(int32_t)root64(variance>0?(uint64_t)variance:0u);
    }
}
static uint32_t similarity(const int32_t *a,const int32_t *b) {
    int64_t dot=0;uint64_t aa=0,bb=0;
    for(uint32_t i=0;i<24u;++i) {dot+=(int64_t)a[i]*b[i];aa+=(uint64_t)((int64_t)a[i]*a[i]);bb+=(uint64_t)((int64_t)b[i]*b[i]);}
    uint64_t norm=(uint64_t)root64(aa)*root64(bb);
    if(dot<=0 || !norm)return 0;
    uint32_t score=(uint32_t)((uint64_t)dot*100u/norm);return score>100u?100u:score;
}
static void reset_audio(void) {
    used=frames=quiet=voiced=overflow=0;
    memset(sums,0,sizeof(sums));memset(squares,0,sizeof(squares));
}
static void changed(void) {++view->change_sequence;}
static void synchronize_epoch(void) {
    if(view->epoch==mode_epoch)return;
    mode_epoch=view->epoch;reset_audio();segment=0;
    enroll_slot=0;view->speaker_enroll=0;view->speaker_progress=0;
    view->speaker_id=view->speaker_score=0;view->speaker_status=3;
}
void local_ai_init(struct ui_state *state) {
    view=state;reset_audio();memset(profiles,0,sizeof(profiles));enroll_frames=enroll_slot=0;
    memset((void *)local_ai_diag,0,sizeof(local_ai_diag));local_ai_diag[0]=0x3149414cu;
    segment=0;mode_epoch=state->epoch;view->speaker_status=3;
#ifdef Z7_LOCAL_SELFTEST
    test_at=test_next=0;view->model_mode=1;
    (void)ui_control(view,UI_START);cloud_live_control(UI_PAUSE);view->stage=UI_LISTENING;
#endif
}
static void speaker_finish(void) {
    view->speaker_id=view->speaker_score=0;
    if(voiced<40u) {view->speaker_status=5;return;}
    int32_t emb[24];vector(emb,sums,squares,voiced);
    uint32_t best=0,second=0,id=0;
    for(uint32_t s=0;s<4u;++s) if(view->speaker_mask&(1u<<s)) {
        uint32_t score=similarity(emb,profiles[s]);
        if(score>best){second=best;best=score;id=s+1u;}else if(score>second)second=score;
    }
    view->speaker_score=best;
    view->speaker_id=best>=98u && best>=second+2u?id:0u;
    view->speaker_status=view->speaker_id?2u:3u;
}
static void recognize(void) {
    ++local_ai_diag[5];
    if(frames<8u || overflow) {view->local_result=2;return;}
    uint32_t best=UINT_MAX,second=UINT_MAX,id=0,lang=view->language==UI_EN_ZH?0u:1u;
    /* First minimize per phrase so alternate voices/rates cannot consume the
       second-best slot and accidentally suppress the ambiguity rejection. */
    uint32_t costs[12];for(uint32_t i=0;i<12u;++i)costs[i]=UINT_MAX;
    for(uint32_t t=0;t<tiny_template_count;++t) {
        const struct tiny_template *m=&tiny_templates[t];if(m->language!=lang)continue;
        uint32_t d=tiny_dtw(utterance,frames,m->data,m->frames);
        if(d<costs[m->phrase])costs[m->phrase]=d;
    }
    for(uint32_t i=0;i<12u;++i) {if(costs[i]<best){second=best;best=costs[i];id=i;}else if(costs[i]<second)second=costs[i];}
    view->local_cost=best;local_ai_diag[8]=best;
    /* Uncalibrated human microphone input is rejected conservatively. */
    if(best>180u || second<best+15u || best*100u>second*90u) {
        view->local_result=2;++local_ai_diag[7];return;
    }
    const uint32_t *source=tiny_text[lang][id],*target=tiny_text[1u-lang][id];
    struct ui_event e={UI_ASR_FINAL,view->epoch,++segment,1,1,source};
    if(!ui_apply_event(view,&e))return;
    e.type=UI_TRANSLATING_EVENT;e.text=0;
    if(!ui_apply_event(view,&e))return;
    e.type=UI_TRANSLATION_FINAL;e.text=target;
    if(!ui_apply_event(view,&e))return;
    view->local_result=1;view->local_phrase=id+1u;++local_ai_diag[6];
    const struct tiny_voice *v=&tiny_voices[1u-lang][id];
    (void)speech_playback_local(v->data,v->samples);
}
static void finish(void) {
    if(!voiced)return;
    speaker_finish();
    if(view->model_mode && !enroll_slot) {
        uint32_t start=ticks();recognize();uint32_t duration=ticks()-start;
        if(duration>local_ai_diag[16])local_ai_diag[16]=duration;
    }
    local_ai_diag[9]=view->speaker_id;local_ai_diag[10]=view->speaker_score;
    reset_audio();changed();
}
static void feature(void) {
    int16_t f[TINY_DIM];uint32_t start=ticks(),energy=tiny_features(window,f);
    uint32_t duration=ticks()-start;if(duration>local_ai_diag[17])local_ai_diag[17]=duration;
    ++local_ai_diag[3];
    uint32_t threshold=noise*4u;if(threshold<10000u)threshold=10000u;if(threshold>160000u)threshold=160000u;
    if(energy<threshold) {
        if(!voiced && energy<40000u)noise=(noise*31u+energy)/32u;
        if(voiced && ++quiet>=25u)finish();
        return;
    }
    quiet=0;
    if(!voiced){view->speaker_id=view->speaker_score=0;view->speaker_status=enroll_slot?1u:4u;changed();}
    ++voiced;
    if(frames<300u) {memcpy(utterance+frames*TINY_DIM,f,sizeof(f));++frames;}else overflow=1;
    for(uint32_t i=0;i<12u;++i) {sums[i]+=f[i];squares[i]+=(int64_t)f[i]*f[i];}
    if(enroll_slot) {
        ++enroll_frames;
        for(uint32_t i=0;i<12u;++i) {enroll_sum[i]+=f[i];enroll_square[i]+=(int64_t)f[i]*f[i];}
        uint32_t progress=enroll_frames*100u/150u;if(progress>100u)progress=100u;
        if(progress!=view->speaker_progress){view->speaker_progress=progress;changed();}
        if(enroll_frames>=150u) {
            vector(profiles[enroll_slot-1u],enroll_sum,enroll_square,enroll_frames);
            view->speaker_mask|=1u<<(enroll_slot-1u);view->speaker_id=enroll_slot;view->speaker_score=100;
            enroll_slot=0;view->speaker_enroll=0;view->speaker_status=2;reset_audio();changed();
        }
    }
    /* Long speech cannot create an unbounded buffer or a guessed phrase. */
    if(voiced>=300u){overflow=1;++local_ai_diag[18];finish();}
}
void local_ai_feed(const int16_t *pcm,uint32_t count) {
    if(!view || view->session!=UI_RUNNING)return;
    for(uint32_t i=0;i<count;++i) {
        window[used++]=pcm[i];
        if(used==TINY_FRAME) {
            feature();
            /* Keep overlap only if feature() did not reset the utterance. */
            if(used==TINY_FRAME) {memmove(window,window+320,80u*sizeof(int16_t));used=80;}
        }
    }
    local_ai_diag[4]+=count;
}
void local_ai_control(enum ui_command command) {
    /* Cloud reconnects also advance the UI epoch without a user control.
       Retire old audio before starting a newly requested enrollment. */
    synchronize_epoch();
    if(command==UI_AI_ENROLL_1 || command==UI_AI_ENROLL_2 || command==UI_AI_ENROLL_3 || command==UI_AI_ENROLL_4) {
        enroll_slot=(uint32_t)command-UI_AI_ENROLL_1+1u;enroll_frames=0;enroll_deadline=now_ms+30000u;
        memset(enroll_sum,0,sizeof(enroll_sum));memset(enroll_square,0,sizeof(enroll_square));
        view->speaker_progress=0;view->speaker_enroll=enroll_slot;view->speaker_status=1;
        reset_audio();
    }
    if(command==UI_AI_CANCEL_ENROLL || command==UI_AI_ERASE) {
        enroll_slot=0;view->speaker_enroll=0;view->speaker_progress=0;view->speaker_id=0;view->speaker_status=3;
        if(command==UI_AI_ERASE){memset(profiles,0,sizeof(profiles));view->speaker_mask=0;}
    }
    if(command==UI_PAUSE || command==UI_CLEAR) {
        mode_epoch=view->epoch;reset_audio();segment=0;view->speaker_id=0;
        enroll_slot=0;view->speaker_enroll=0;view->speaker_progress=0;
    }
    if(command==UI_AI_CLOUD || command==UI_AI_LOCAL) {
        cloud_live_control(UI_PAUSE);speech_playback_stop();
        if(!view->model_mode && view->session==UI_RUNNING)cloud_live_control(UI_RESUME);
    } else if(!view->model_mode)cloud_live_control(command);
    if(view->model_mode && view->session==UI_RUNNING)view->stage=UI_LISTENING;
}
void local_ai_tick(uint32_t now) {
    now_ms=now;
    synchronize_epoch();
    if(enroll_slot && (int32_t)(now-enroll_deadline)>=0) {enroll_slot=0;view->speaker_enroll=0;view->speaker_status=6;changed();}
    if(view->model_mode) {
        uint32_t n=LOCAL_AI_READ(0x90u);if(n>local_ai_diag[15])local_ai_diag[15]=n;if(n>256u)n=256u;
#ifdef Z7_LOCAL_SELFTEST
        for(uint32_t i=0;i<n;++i)(void)LOCAL_AI_READ(0x8cu);
        if(!test_next)test_next=now+500u;
        if(view->session==UI_RUNNING && (int32_t)(now-test_next)>=0 && test_at<selftest_samples+12000u) {
            int16_t samples[320];for(uint32_t i=0;i<320u;++i)samples[i]=test_at+i<selftest_samples?selftest_pcm[test_at+i]:0;
            local_ai_feed(samples,320u);test_at+=320u;test_next+=20u;
        }
#else
        int16_t pcm[256];for(uint32_t i=0;i<n;++i)pcm[i]=(int16_t)LOCAL_AI_READ(0x8cu);
        if(view->session==UI_RUNNING)local_ai_feed(pcm,n);
#endif
    }
    if(now-last_publish>=200u) {
        last_publish=now;local_ai_diag[1]=now;local_ai_diag[2]=view->model_mode;
        local_ai_diag[11]=view->speaker_mask;local_ai_diag[12]=view->speaker_progress;
        local_ai_diag[13]=view->epoch;local_ai_diag[14]=frames;
#ifdef Z7_MMU_CACHE
        cache_clean_range((const void *)local_ai_diag,sizeof(local_ai_diag));
#endif
    }
}
