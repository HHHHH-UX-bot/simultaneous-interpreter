#ifndef LOCAL_VOICE_H
#define LOCAL_VOICE_H
#include "cloud_events.h"
void local_voice_init(struct ui_state *state);
void local_voice_control(enum ui_command command);
void local_voice_tick(uint32_t now);
int local_voice_role(void); /* 0: LiveTranslate, 1: ASR, 2: TTS */
int local_voice_network_run(void);
int local_voice_uses_output(void);
int local_voice_error_active(void);
void local_voice_snapshot(uint32_t output[24]);
int local_voice_event(const struct cloud_event *event);
void local_voice_output(const char *source,const char *text,const char *status);
const char *local_voice_path(void);
#endif
