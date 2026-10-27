#ifndef LOCAL_AI_H
#define LOCAL_AI_H
#include "../subtitle_ui/ui_model.h"
void local_ai_init(struct ui_state *state);
void local_ai_control(enum ui_command command);
void local_ai_tick(uint32_t now);
#endif
