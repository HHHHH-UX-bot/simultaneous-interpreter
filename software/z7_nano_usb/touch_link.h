#ifndef Z7_TOUCH_LINK_H
#define Z7_TOUCH_LINK_H
#include <stdint.h>
#define TOUCH_LINK_ADDRESS 0x0000d000u
#define TOUCH_LINK_MAGIC 0x544f5531u
#define TOUCH_FEEDBACK_ADDRESS 0x0001e040u
#define TOUCH_FEEDBACK_MAGIC 0x544f5532u
#define TOUCH_TAPS 8u
struct touch_tap {uint32_t press_x,press_y,x,y;};
/* CPU1 owns this entire block; CPU0 publishes acknowledgements separately. */
struct touch_link {
    uint32_t magic,sequence,reports,pressed,x,y,raw_x,raw_y,contact_id;
    uint32_t downs,ups,moves,min_x,max_x,min_y,max_y;
    uint32_t tap_write,tap_read,dropped_taps,multitouch,generation;
    uint32_t reserved[11];
    struct touch_tap taps[TOUCH_TAPS];
};
_Static_assert(sizeof(struct touch_link)==256,"Touch link ABI");
/* CPU0 owns this block. Counts use enum ui_command order. */
struct touch_feedback {
    uint32_t magic,sequence,generation,read_taps;
    uint32_t accepted[7],ignored,last_command,last_x,last_y,reserved;
};
_Static_assert(sizeof(struct touch_feedback)==64,"Touch feedback ABI");
#endif
