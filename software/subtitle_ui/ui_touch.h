#ifndef SUBTITLE_UI_TOUCH_H
#define SUBTITLE_UI_TOUCH_H
#include "ui_model.h"
#include "touch_link.h"
void ui_touch_init(const volatile struct touch_link *link,volatile struct touch_feedback *feedback);
/* Returns the accepted command plus one, or zero when no control changed. */
uint32_t ui_touch_poll(const volatile struct touch_link *link,volatile struct touch_feedback *feedback,
                       struct ui_state *state);
#endif
