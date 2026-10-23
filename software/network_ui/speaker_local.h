#ifndef SPEAKER_LOCAL_H
#define SPEAKER_LOCAL_H
#include "../subtitle_ui/ui_model.h"
void speaker_local_init(struct ui_state *state);
void speaker_local_tick(uint32_t now);
int speaker_local_control(enum ui_command command);
int speaker_local_enrolling(void);
/* Validation transport: rejected unless JTAG explicitly arms PL test mode. */
int speaker_local_test_pcm(uint32_t offset,const uint8_t *pcm,uint32_t bytes);
/* Public numeric diagnostics only. No waveform, features or voice profiles. */
extern volatile uint32_t speaker_diag[32];
#endif
