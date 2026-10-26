#include "local_voice.h"
#include "speech_playback.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern volatile uint32_t local_voice_diag[24];
static char submitted[4096],spoken[4096];static int cancellations,commits,stops,finishes;
static struct speech_status playback={1,0,0,0};
struct network_status_stub {uint32_t magic,sequence,phase,error,rest[12];} network_info;
void cache_clean_range(const void *p,unsigned n){(void)p;(void)n;}
void cloud_live_quiesce(void){++stops;}
void network_cloud_run(int x){(void)x;}
void speech_playback_stop(void){playback.state=1;playback.buffer_ms=0;}
void tbl_cancel_local(void){++cancellations;}
int tbl_submit_local(const char *s,const char *system,uint32_t maximum){assert(strstr(system,"Chinese to English"));assert(maximum==96);strcpy(submitted,s);return 1;}
void cloud_client_session_created(void){}
void cloud_client_session_updated(void){}
void cloud_client_speech_started(uint32_t n){(void)n;}
void cloud_client_speech_stopped(void){}
void cloud_client_asr_final(void){}
int speech_playback_audio(const char *p,uint32_t n){(void)p;(void)n;playback.state=3;playback.buffer_ms=100;return 1;}
void speech_playback_done(void){}
void speech_playback_utterance(void){}
void speech_playback_status(struct speech_status *s){*s=playback;}
void speech_playback_counters(uint32_t out[3]){out[0]=out[1]=123;out[2]=0;}
int cloud_client_tts_text(const char *p,uint32_t n){assert(n<=240);strncat(spoken,p,n);return 1;}
int cloud_client_tts_commit(void){++commits;return 1;}
int cloud_client_tts_finish(void){++finishes;return 1;}
static void event(enum cloud_kind kind){struct cloud_event e={0};e.kind=kind;assert(local_voice_event(&e)==1);}
int main(void){
 struct ui_state v={0};v.model_mode=1;v.session=UI_RUNNING;v.language=UI_ZH_EN;v.tts_enabled=1;
 local_voice_init(&v);local_voice_control(UI_START);local_voice_tick(200);assert(local_voice_diag[1]==1);
 event(CLOUD_SESSION_CREATED);event(CLOUD_SESSION_UPDATED);local_voice_tick(400);assert(local_voice_diag[1]==2);
 struct cloud_event e={0};e.kind=CLOUD_ASR_FINAL;e.text[0]=0x8bf7;e.text[1]=0x6253;e.text[2]=0x5f00;
 local_voice_event(&e);assert(!strcmp(submitted,"\xe8\xaf\xb7\xe6\x89\x93\xe5\xbc\x80"));
 local_voice_tick(600);assert(local_voice_diag[1]==3 && !local_voice_network_run());
 local_voice_output(submitted,"","llm_prefill");local_voice_tick(1000);local_voice_output(submitted,"Open","llm_generating");
 assert(local_voice_network_run());
 event(CLOUD_SESSION_CREATED);event(CLOUD_SESSION_UPDATED);
 local_voice_tick(1200);local_voice_output(submitted,"Open the window.","llm_done_eos");assert(local_voice_role()==2);
 local_voice_tick(1400);local_voice_tick(1600);
 assert(commits==1 && !finishes && !strcmp(spoken,"Open the window."));
 e.kind=CLOUD_AUDIO_DELTA;e.audio="AAAA";e.audio_bytes=4;local_voice_event(&e);local_voice_tick(1800);
 event(CLOUD_AUDIO_DONE);local_voice_tick(1801);assert(!finishes);
 event(CLOUD_RESPONSE_DONE);local_voice_tick(1802);assert(finishes==1);event(CLOUD_FINISHED);
 playback.state=1;playback.buffer_ms=0;local_voice_tick(2000);local_voice_tick(2200);assert(local_voice_diag[1]==1 && local_voice_diag[18]==1);
 uint32_t snapshot[24];local_voice_snapshot(snapshot);assert(snapshot[21]==123 && snapshot[22]==123 && snapshot[23]==0);
 // Pause cannot send further audio; changing mode relinquishes the ASR role.
 v.session=UI_PAUSED;local_voice_control(UI_PAUSE);local_voice_tick(2400);assert(!local_voice_network_run() && !local_voice_role());
 v.model_mode=0;local_voice_control(UI_AI_CLOUD);assert(!local_voice_role());
 assert(cancellations>=2 && stops>=3);puts("PASS voice state transitions, UTF8 source, TTS ordering, drain, pause and mode switch");return 0;
}
