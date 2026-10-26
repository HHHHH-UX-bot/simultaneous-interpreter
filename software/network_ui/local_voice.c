#include "local_voice.h"
#include "tiny_board_llm.h"
#include "cloud_client.h"
#include "cloud_live.h"
#include "network.h"
#include "speech_playback.h"
#include "remote_display.h"
#include <string.h>
#ifdef Z7_MMU_CACHE
extern void cache_clean_range(const void *,unsigned);
#endif
enum {V_OFF,V_ASR_CONNECT,V_LISTEN,V_LLM,V_TTS_CONNECT,V_SEND,V_PLAY,V_RETURN,V_ERROR};
static struct ui_state *view;
static uint32_t phase,clock_ms,phase_ms,enabled,role,send_offset,commit_sent,utterance;
static uint32_t tts_warming,tts_ready;
static uint32_t finish_sent,stream_finished,response_finished;
static const char *translation;
static char source[TBL_INPUT_BYTES+1u];
/* Public, credential-free timing/counter window. All times use board ms. */
volatile uint32_t local_voice_diag[24] __attribute__((aligned(32)));
char local_voice_error[80] __attribute__((aligned(32)));
char local_voice_error_message[80] __attribute__((aligned(32)));
static void enter(uint32_t p) {phase=p;phase_ms=clock_ms;}
static void quiesce(void) {cloud_live_quiesce();network_cloud_run(0);}
static void fault(uint32_t code) {
    tbl_cancel_local();
    utterance=0;
    local_voice_diag[3]=code;enter(V_ERROR);quiesce();
    if(view){view->stage=UI_OFFLINE;++view->change_sequence;}
}
static int same(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
void local_voice_init(struct ui_state *state) {
    view=state;phase=enabled=role=clock_ms=0;translation=0;
    memset((void *)local_voice_diag,0,sizeof(local_voice_diag));local_voice_diag[0]=0x31564c51u;
}
int local_voice_role(void) {return (int)role;}
int local_voice_uses_output(void) {return phase==V_TTS_CONNECT || phase==V_SEND || phase==V_PLAY;}
int local_voice_error_active(void) {return phase==V_ERROR;}
void local_voice_snapshot(uint32_t output[24]) {
    for(uint32_t i=0;i<24u;++i)output[i]=local_voice_diag[i];
    output[1]=phase;output[2]=role;output[3]=phase==V_ERROR?local_voice_diag[3]:0u;output[20]=clock_ms;
    speech_playback_counters(output+21);
}
int local_voice_network_run(void) {return enabled && (phase!=V_LLM || tts_warming) && phase!=V_RETURN && phase!=V_ERROR && !(phase==V_PLAY && stream_finished);}
const char *local_voice_path(void) {
    return role==2?"/api-ws/v1/realtime?model=qwen3-tts-flash-realtime":
        "/api-ws/v1/realtime?model=qwen3-asr-flash-realtime";
}
void local_voice_control(enum ui_command command) {
    (void)command;
    if(!view || (!view->model_mode && !enabled && !role))return;
    tbl_cancel_local();speech_playback_stop();quiesce();translation=0;
    tts_warming=tts_ready=0;
    finish_sent=stream_finished=response_finished=0;
    utterance=0;
    enabled=view && view->model_mode && view->session==UI_RUNNING;
    role=enabled?1u:0u;
    local_voice_diag[3]=0;
    enter(enabled?(network_info.phase==6u?V_ASR_CONNECT:V_RETURN):V_OFF);
    if(enabled){view->stage=UI_CONNECTING;++view->change_sequence;}
}
static int source_utf8(const uint32_t *p) {
    uint32_t n=0;
    while(*p) {
        uint32_t c=*p++;
        if(c>0x10ffffu || (c>=0xd800u&&c<=0xdfffu))return 0;
        uint32_t k=c<128u?1u:c<2048u?2u:c<65536u?3u:4u;
        if(n+k>TBL_INPUT_BYTES)return 0;
        if(k==1)source[n++]=(char)c;
        else {source[n++]=(char)((k==2?0xc0u:k==3?0xe0u:0xf0u)|(c>>(6u*(k-1u))));
            while(--k)source[n++]=(char)(0x80u|((c>>(6u*(k-1u)))&63u));}
    }
    source[n]=0;return n!=0;
}
int local_voice_event(const struct cloud_event *e) {
    if(!role)return 0;
    if(!enabled)return 1;
    if(e->kind==CLOUD_SESSION_CREATED) {cloud_client_session_created();return 1;}
    if(e->kind==CLOUD_SESSION_UPDATED) {
        cloud_client_session_updated();
        if(role==1 && phase==V_ASR_CONNECT){enter(V_LISTEN);view->stage=UI_LISTENING;++view->change_sequence;}
        else if(role==2 && phase==V_LLM){tts_ready=1;}
        else if(role==2 && phase==V_TTS_CONNECT){send_offset=commit_sent=0;enter(V_SEND);}
        return 1;
    }
    if(e->kind==CLOUD_ERROR) {memcpy(local_voice_error,e->error_code,sizeof(local_voice_error));
        memcpy(local_voice_error_message,e->error_message,sizeof(local_voice_error_message));
#ifdef Z7_MMU_CACHE
        cache_clean_range(local_voice_error,sizeof(local_voice_error));
        cache_clean_range(local_voice_error_message,sizeof(local_voice_error_message));
#endif
        ++local_voice_diag[19];fault(1);return 1;}
    if(e->kind==CLOUD_SPEECH_STARTED && role==1 && phase==V_LISTEN) {
        memset((void *)&local_voice_diag[4],0,12u*sizeof(uint32_t));
        cloud_client_speech_started(e->audio_start_ms);local_voice_diag[4]=clock_ms;
        view->stage=UI_RECOGNIZING;++view->change_sequence;return 1;
    }
    if(e->kind==CLOUD_SPEECH_STOPPED && role==1) {cloud_client_speech_stopped();local_voice_diag[5]=clock_ms;return 1;}
    if(e->kind==CLOUD_ASR_PARTIAL && role==1 && phase==V_LISTEN) {
        memcpy(view->source,e->text,sizeof(view->source));++view->change_sequence;return 1;
    }
    if(e->kind==CLOUD_ASR_FINAL && role==1 && phase==V_LISTEN) {
        cloud_client_asr_final();if(!e->text[0])return 1;
        local_voice_diag[6]=clock_ms;
        if(!source_utf8(e->text)){fault(2);return 1;}
        const char *system=view->language==UI_ZH_EN?"Translate Chinese to English. Output only translation.":
            "Translate English to Chinese. Output only translation.";
        if(!tbl_submit_local(source,system,96)){fault(3);return 1;}
        ++local_voice_diag[16];utterance=1;translation=0;enter(V_LLM);quiesce();
        return 1;
    }
    if(e->kind==CLOUD_AUDIO_DELTA && role==2 && (phase==V_SEND || phase==V_PLAY)) {
        if(!local_voice_diag[10])local_voice_diag[10]=clock_ms;
        if(!speech_playback_audio(e->audio,e->audio_bytes))fault(4);
        return 1;
    }
    if(role==2 && (e->kind==CLOUD_AUDIO_DONE || e->kind==CLOUD_RESPONSE_DONE)) {
        if(phase==V_PLAY){speech_playback_done();local_voice_diag[11]=clock_ms;commit_sent=2;
            if(e->kind==CLOUD_RESPONSE_DONE)response_finished=1;}
        return 1;
    }
    if(role==2 && e->kind==CLOUD_FINISHED && phase==V_PLAY) {
        if(!local_voice_diag[10]){fault(8);return 1;}
        speech_playback_done();commit_sent=2;stream_finished=1;quiesce();return 1;
    }
    return 1;
}
void local_voice_output(const char *input,const char *text,const char *status) {
    (void)input;
    if(!view || !view->model_mode || view->session!=UI_RUNNING)return;
    if(same(status,"llm_prefill")) {
        if(!utterance){memset((void *)&local_voice_diag[4],0,12u*sizeof(uint32_t));utterance=1;}
        local_voice_diag[7]=clock_ms;local_voice_diag[3]=0;translation=0;enabled=1;role=1;tts_warming=tts_ready=0;enter(V_LLM);quiesce();
    }
    if(phase!=V_LLM)return;
    if(text[0] && !local_voice_diag[8])local_voice_diag[8]=clock_ms;
    /* TLS and the TTS session can become ready while later model tokens are
       still being generated. Text is committed only after the actual EOS. */
    if(text[0] && view->tts_enabled && !tts_warming){tts_warming=1;role=2;}
    if(same(status,"llm_done_eos") || same(status,"llm_done_limit")) {
        local_voice_diag[9]=clock_ms;
        if(!text[0] || !view->tts_enabled){utterance=0;enter(V_RETURN);return;}
        translation=text;role=2;send_offset=commit_sent=finish_sent=stream_finished=response_finished=0;enter(tts_ready?V_SEND:V_TTS_CONNECT);
        speech_playback_utterance();++local_voice_diag[17];
    } else if(!same(status,"llm_prefill") && !same(status,"llm_generating") && !same(status,"llm_queued"))fault(5);
}
void local_voice_tick(uint32_t now) {
    clock_ms=now;if(!view)return;
    if(enabled && (!view->model_mode || view->session!=UI_RUNNING))local_voice_control(UI_PAUSE);
    if(enabled) {
        if(phase==V_RETURN && network_info.phase==0u){role=1;enter(V_ASR_CONNECT);}
        if(phase==V_SEND && translation) {
            uint32_t left=(uint32_t)strlen(translation+send_offset);
            if(left) {uint32_t n=left>240u?240u:left;while(n<left && ((uint8_t)translation[send_offset+n]&0xc0u)==0x80u)--n;
                if(cloud_client_tts_text(translation+send_offset,n)){send_offset+=n;local_voice_diag[14]+=n;}
            } else if(cloud_client_tts_commit()){commit_sent=1;enter(V_PLAY);}
        }
        if(phase==V_PLAY) {
            /* Finish only after the provider completes synthesis. An immediate
               finish after commit races the asynchronous synthesis request. */
            if(response_finished && !finish_sent && cloud_client_tts_finish()){finish_sent=1;local_voice_diag[15]=now;}
            struct speech_status s;speech_playback_status(&s);
            if(s.state==3u && !local_voice_diag[12])local_voice_diag[12]=now;
            if(commit_sent==2u && stream_finished && !s.buffer_ms && s.state!=3u) {
                local_voice_diag[13]=now;++local_voice_diag[18];utterance=0;translation=0;quiesce();role=1;
                enter(network_info.phase==6u?V_ASR_CONNECT:V_RETURN);
            }
        }
        if(network_info.phase==6u && phase!=V_LLM && phase!=V_RETURN && phase!=V_ERROR && !(phase==V_PLAY && stream_finished))fault(6);
        if((phase==V_TTS_CONNECT || phase==V_SEND || phase==V_PLAY) && now-phase_ms>90000u)fault(7);
    }
    static uint32_t report_ms;
    if(now-report_ms>=200u) {
        report_ms=now;local_voice_diag[1]=phase;local_voice_diag[2]=role;local_voice_diag[20]=now;
#ifdef Z7_MMU_CACHE
        cache_clean_range((const void *)local_voice_diag,sizeof(local_voice_diag));
#endif
    }
}
