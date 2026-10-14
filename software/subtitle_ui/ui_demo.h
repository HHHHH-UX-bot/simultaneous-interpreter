#ifndef SUBTITLE_UI_DEMO_H
#define SUBTITLE_UI_DEMO_H
#include "ui_model.h"
struct ui_demo_clock {uint32_t epoch,phase,sample_index,segment_id,marked_ms,partial_size;};
void ui_demo_init(struct ui_demo_clock *clock,struct ui_state *state,uint32_t now_ms);
void ui_demo_tick(struct ui_demo_clock *clock,struct ui_state *state,uint32_t now_ms);
#endif
