#ifndef Z7_TOUCH_INPUT_H
#define Z7_TOUCH_INPUT_H
#include "touch_link.h"
struct touch_decoder {uint32_t press_x,press_y,armed,blocked;};
void touch_init(volatile struct touch_link *link,struct touch_decoder *decoder,uint32_t generation);
int touch_receive(volatile struct touch_link *link,struct touch_decoder *decoder,const uint8_t *data,uint32_t size);
void touch_acknowledge(volatile struct touch_link *link,const volatile struct touch_feedback *feedback);
#endif
