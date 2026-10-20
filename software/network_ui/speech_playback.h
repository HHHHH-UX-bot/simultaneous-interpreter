#ifndef SPEECH_PLAYBACK_H
#define SPEECH_PLAYBACK_H
#include <stdint.h>
void speech_playback_init(void);
int speech_playback_audio(const char *base64,uint32_t bytes);
void speech_playback_done(void);
void speech_playback_disconnect(void);
void speech_playback_stop(void);
void speech_playback_tick(uint32_t now);
#endif
