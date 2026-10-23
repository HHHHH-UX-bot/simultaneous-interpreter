#include <stdint.h>
#include <string.h>
#define LOCAL_AI_READ(o) 0u
#include "../software/network_ui/local_ai.c"
#include "../software/subtitle_ui/ui_touch.h"
#include "../software/subtitle_ui/ui_render.h"
static struct ui_state host;
static uint32_t voice_samples,cloud_controls;
void cloud_live_control(enum ui_command c) {(void)c;++cloud_controls;}
void speech_playback_stop(void) {voice_samples=0;}
int speech_playback_local(const int8_t *pcm,uint32_t n) {(void)pcm;voice_samples=n;return 1;}
void host_init(uint32_t lang,uint32_t local) {
    ui_init(&host);host.language=(enum ui_language)lang;ui_control(&host,UI_START);local_ai_init(&host);
    if(local){ui_control(&host,UI_AI_LOCAL);local_ai_control(UI_AI_LOCAL);}
    voice_samples=0;
}
int host_command(uint32_t command) {
    int ok=ui_control(&host,(enum ui_command)command);if(ok)local_ai_control((enum ui_command)command);return ok;
}
uint32_t host_get(uint32_t field) {
    const uint32_t fields[]={host.final_count,host.local_result,host.local_phrase,host.local_cost,
        host.speaker_id,host.speaker_score,host.speaker_mask,host.speaker_progress,host.speaker_enroll,
        host.epoch,host.model_mode,host.ai_page,voice_samples,cloud_controls,host.session,host.speaker_status};
    return field<sizeof(fields)/sizeof(fields[0])?fields[field]:0;
}
void host_feed(const int16_t *pcm,uint32_t n) {local_ai_feed(pcm,n);}
void host_tick(uint32_t now) {local_ai_tick(now);}
uint32_t host_size(void) {return sizeof(host);}
void host_external_epoch(void) {ui_control(&host,UI_PAUSE);ui_control(&host,UI_RESUME);}
void host_state(void *out) {memcpy(out,&host,sizeof(host));}
void host_render(uint32_t *pixels,struct ui_render_stats *stats) {ui_history_prepare(&host);ui_render(pixels,&host,1000,0,stats);}
int host_touch(uint32_t x,uint32_t y) {uint32_t c=ui_touch_remote(&host,x,y,x,y);if(c)local_ai_control((enum ui_command)(c-1u));return (int)c;}
