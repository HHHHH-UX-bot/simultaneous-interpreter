#include <string.h>
#include "ui_touch.h"
#if defined(__arm__)
static void barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
#else
static void barrier(void) {__asm__ volatile("" ::: "memory");}
#endif
static int button(uint32_t x,uint32_t y) {
    if(y<530u || y>=586u) return -1;
    static const uint32_t bounds[][2]={{24,200},{212,356},{368,488},{558,614},{690,746}};
    for(uint32_t i=0;i<5u;++i) if(x>=bounds[i][0] && x<bounds[i][1]) return (int)i;
    return -1;
}
void ui_touch_init(const volatile struct touch_link *link,volatile struct touch_feedback *feedback) {
    memset((void *)feedback,0,sizeof(*feedback));
    /* Display startup runs while CPU1 is stopped; discard prior capture taps. */
    if(link->magic==TOUCH_LINK_MAGIC) {
        feedback->generation=link->generation;feedback->read_taps=link->tap_write;
    }
    feedback->last_command=0xffffffffu;
    barrier();feedback->magic=TOUCH_FEEDBACK_MAGIC;barrier();
}
uint32_t ui_touch_poll(const volatile struct touch_link *link,volatile struct touch_feedback *feedback,
                       struct ui_state *state) {
    uint32_t sequence=link->sequence;
    if(link->magic!=TOUCH_LINK_MAGIC || sequence&1u) return 0;
    barrier();
    uint32_t generation=link->generation;
    uint32_t read=generation==feedback->generation?feedback->read_taps:link->tap_read;
    uint32_t write=link->tap_write;
    if(write-read>TOUCH_TAPS) return 0;
    struct touch_tap tap={0,0,0,0};
    if(read!=write) tap=link->taps[read%TOUCH_TAPS];
    barrier();
    if(link->magic!=TOUCH_LINK_MAGIC || link->generation!=generation || link->sequence!=sequence) return 0;
    if(read==write && generation==feedback->generation) return 0;
    ++feedback->sequence;barrier();
    feedback->generation=generation;
    uint32_t result=0;
    if(read!=write) {
        int pressed=button(tap.press_x,tap.press_y),released=button(tap.x,tap.y),command=-1;
        if(pressed>=0 && released==pressed) {
            static const enum ui_command controls[]={UI_START,UI_CLEAR,UI_TOGGLE_SOURCE,UI_FONT_MINUS,UI_FONT_PLUS};
            command=controls[pressed];
            if(pressed==0) command=state->session==UI_RUNNING?UI_PAUSE:
                (state->session==UI_PAUSED?UI_RESUME:(state->session==UI_IDLE?UI_START:-1));
        }
        if(command>=0 && ui_control(state,(enum ui_command)command)) {
            ++feedback->accepted[command];result=(uint32_t)command+1u;
        } else ++feedback->ignored;
        feedback->last_command=command>=0?(uint32_t)command:0xffffffffu;
        feedback->last_x=tap.x;feedback->last_y=tap.y;++read;
    }
    feedback->read_taps=read;
    barrier();++feedback->sequence;barrier();
    return result;
}
