#ifndef SPEECH_PLAYBACK_H
#define SPEECH_PLAYBACK_H
#include <stdint.h>
void speech_playback_init(void);
int speech_playback_audio(const char *base64,uint32_t bytes);
void speech_playback_done(void);
void speech_playback_disconnect(void);
void speech_playback_stop(void);
void speech_playback_tick(uint32_t now);
/* DAC-only controls: never alter the microphone / Opus upload branch. */
void speech_playback_settings(uint32_t enabled,uint32_t volume,uint32_t speed_percent);
void speech_playback_utterance(void);
struct speech_status {uint32_t state,latency_ms,latency_valid,buffer_ms;};
void speech_playback_status(struct speech_status *status);
#endif
