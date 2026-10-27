#include "local_ai.h"
#include "network.h"
#include <stddef.h>
size_t tbl_network_info(uint8_t *out,size_t capacity) {
    if(capacity<40u)return 0;
    uint8_t name[33];uint32_t connected=network_wifi_name(name),n=0;
    while(n<32u && name[n])++n;
    for(uint32_t i=0;i<4u;++i){out[i]=(uint8_t)(connected>>(8u*i));out[4u+i]=(uint8_t)(n>>(8u*i));}
    for(uint32_t i=0;i<n;++i)out[8u+i]=name[i];
    return 8u+n;
}
#include "cloud_live.h"
#include "speech_playback.h"
#include "remote_display.h"
#include "local_voice.h"
#include "tiny_board_llm.h"
#ifdef Z7_SPEAKER_CNN
#include "speaker_local.h"
#endif
#ifdef Z7_SD_BOOT
#include "sd_model.h"
#endif
#include <stddef.h>
#include <string.h>

/* This build accepts arbitrary text through the board's LLM command port.
   It deliberately has no phrase dictionary or acoustic template recognizer. */
static struct ui_state *view;
static uint32_t job_segment;
int tbl_test_pcm(uint32_t offset,const uint8_t *pcm,uint32_t bytes){
#ifdef Z7_SPEAKER_CNN
    return speaker_local_test_pcm(offset,pcm,bytes);
#else
    (void)offset;(void)pcm;(void)bytes;return -1;
#endif
}
int tbl_ui_busy(void) {return local_voice_uses_output()
#ifdef Z7_SPEAKER_CNN
    || speaker_local_enrolling()
#endif
    ;}
int tbl_ui_error(void) {return local_voice_error_active();}
size_t tbl_voice_info(uint8_t *out,size_t capacity) {
    uint32_t words[24];if(capacity<sizeof(words))return 0;
    local_voice_snapshot(words);
    for(uint32_t i=0;i<24u;++i)for(uint32_t j=0;j<4u;++j)out[i*4u+j]=(uint8_t)(words[i]>>(j*8u));
    return sizeof(words);
}
static int equal(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
static void utf32(uint32_t *out,const char *text) {
    const unsigned char *p=(const unsigned char *)text;uint32_t n=0;
    while(*p && n<UI_MAX_TEXT-1u) {
        uint32_t c=*p++,extra=0;
        if(c>=0xc2u && c<=0xdfu) {c&=31u;extra=1;}
        else if(c>=0xe0u && c<=0xefu) {c&=15u;extra=2;}
        else if(c>=0xf0u && c<=0xf4u) {c&=7u;extra=3;}
        else if(c>=128u) {out[n++]='?';continue;}
        uint32_t good=1;
        for(uint32_t k=0;k<extra;++k) {
            if((p[k]&0xc0u)!=0x80u) {good=0;break;}
            c=(c<<6)|(p[k]&63u);
        }
        if(good) p+=extra;else c='?';
        out[n++]=c;
    }
    out[n]=0;
}
void local_ai_init(struct ui_state *state) {
    view=state;job_segment=0;view->speaker_status=3;
    local_voice_init(state);
#ifdef Z7_SPEAKER_CNN
    speaker_local_init(state);
#endif
}
/* Model page status, and a four-second notice the first time the engine is
   usable (TF card load, PC upload or warm restore alike). */
static void model_view(uint32_t now) {
    static uint32_t announced,notice_at;
    uint32_t load=UI_MODEL_IDLE,percent=0;
#ifdef Z7_SD_BOOT
    load=sd_model_status(&percent);
#endif
    int engine=tbl_engine_state();
    if(engine==1) {load=UI_MODEL_READY;percent=100u;}
    else if(engine==2) load=UI_MODEL_INIT_ERROR;
    else if(load==UI_MODEL_READY) load=UI_MODEL_LOADING;   /* verified, engine starting */
    view->model_load=load;view->model_percent=percent;
    if(load==UI_MODEL_READY && !announced) {announced=1;view->model_notice=1;notice_at=now;}
    if(view->model_notice && now-notice_at>=4000u) view->model_notice=0;
}
void local_ai_tick(uint32_t now) {
    local_voice_tick(now);
#ifdef Z7_SPEAKER_CNN
    speaker_local_tick(now);
#endif
    if(!view)return;
    model_view(now);
    if(!view->model_mode)return;
    uint32_t ready=remote_llm_assets_ready()?1u:0u;
    if(view->local_result!=ready && view->stage!=UI_TRANSLATING && view->stage!=UI_FINAL) {
        view->local_result=ready;++view->change_sequence;
    }
}
void local_ai_control(enum ui_command command) {
    if(!view)return;
#ifdef Z7_SPEAKER_CNN
    if(speaker_local_control(command))return;
#endif
    int relevant=command==UI_START || command==UI_RESUME || command==UI_PAUSE || command==UI_CLEAR ||
        command==UI_AI_CLOUD || command==UI_AI_LOCAL || command==UI_SELECT_EN_ZH || command==UI_SELECT_ZH_EN;
    if(relevant)local_voice_control(command);
    if(command==UI_AI_CLOUD || command==UI_AI_LOCAL) {
        cloud_live_control(UI_PAUSE);speech_playback_stop();
        if(!view->model_mode && view->session==UI_RUNNING)cloud_live_control(UI_RESUME);
    } else if(!view->model_mode)cloud_live_control(command);
    if(view->model_mode && view->session==UI_RUNNING && view->stage==UI_CONNECTING)
        view->stage=UI_LISTENING;
}
void tbl_ui_update(const char *source,const char *output,const char *status) {
    if(!view)return;
#ifdef Z7_SPEAKER_CNN
    if(speaker_local_enrolling())return;
#endif
    if(!source || !*source) {view->local_result=remote_llm_assets_ready()?1u:0u;++view->change_sequence;return;}
    if(!view->model_mode) {
        if(ui_control(view,UI_AI_LOCAL))local_ai_control(UI_AI_LOCAL);
    }
    if(view->session==UI_IDLE)(void)ui_control(view,UI_START);
    else if(view->session!=UI_RUNNING && status && equal(status,"llm_prefill"))(void)ui_control(view,UI_RESUME);
    local_voice_output(source,output,status?status:"");
    utf32(view->source,source?source:"");utf32(view->translated,output?output:"");
    view->settings_page=view->ai_page=view->history_page=view->language_menu=0;
    view->stage=UI_TRANSLATING;
    if(status && (equal(status,"llm_done_eos") || equal(status,"llm_done_limit"))) {
        view->stage=UI_FINAL;
        if(view->translated[0]) {
            struct ui_history_entry *entry;
            if(view->history_count>=UI_HISTORY_MAX) {
                memmove(view->history,view->history+1,(UI_HISTORY_MAX-1u)*sizeof(*view->history));
                view->history_count=UI_HISTORY_MAX-1u;++view->history_discarded;
            }
            entry=&view->history[view->history_count++];memset(entry,0,sizeof(*entry));
            entry->epoch=view->epoch;entry->segment_id=++job_segment;entry->revision=entry->source_revision=1;
            entry->language=view->language;
            memcpy(entry->source,view->source,sizeof(entry->source));
            memcpy(entry->translated,view->translated,sizeof(entry->translated));++view->final_count;
        }
    } else if(status && !equal(status,"llm_prefill") && !equal(status,"llm_generating") && !equal(status,"llm_queued"))view->stage=UI_OFFLINE;
    ++view->change_sequence;
}
