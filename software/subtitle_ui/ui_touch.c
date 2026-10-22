#include <string.h>
#include "ui_touch.h"
#include "ui_layout.h"
#include "../network_ui/language_profile.h"
#if defined(__arm__)
static void barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
#else
static void barrier(void) {__asm__ volatile("" ::: "memory");}
#endif
#ifdef Z7_HISTORY_UI
static struct {uint32_t active,allowed,dragged,start_x,start_y,last_y,suppress;} gesture;
static int inside(uint32_t x,int left,int width) {return x>=(uint32_t)left && x<(uint32_t)(left+width);}
#endif
static int button(uint32_t x,uint32_t y,const struct ui_state *state) {
#ifdef Z7_HISTORY_UI
    if(state->settings_page) {
        if(y>=530u && y<586u && inside(x,24,176)) return 16;
        if(y>=142u && y<198u) {
            if(inside(x,328,148)) return 17;
            if(inside(x,660,64)) return 3;
            if(inside(x,912,64)) return 4;
        }
        if(y>=212u && y<268u) {
            if(inside(x,168,64)) return 18;
            if(inside(x,328,64)) return 19;
            if(inside(x,824,152)) return 2;
        }
        if(y>=288u && y<344u) {
            if(inside(x,168,64)) return 21;
            if(inside(x,328,64)) return 22;
        }
        if(y>=360u && y<416u) {
            if(inside(x,540,204)) return 6;
            if(inside(x,764,212)) return 7;
        }
        if(y>=400u && y<456u && inside(x,44,180)) return 20;
        return -1;
    }
    if(state->history_page) {
        if(state->history_confirm) {
            if(y<UI_HISTORY_CONFIRM_Y || y>=UI_HISTORY_CONFIRM_Y+56u) return -1;
            if(inside(x,UI_HISTORY_CANCEL_X,UI_HISTORY_CONFIRM_WIDTH)) return 13;
            if(inside(x,UI_HISTORY_CONFIRM_X,UI_HISTORY_CONFIRM_WIDTH)) return 14;
            return -1;
        }
        if(y<530u || y>=586u) return -1;
        if(inside(x,UI_HISTORY_BACK_X,UI_HISTORY_BACK_WIDTH)) return 9;
        if(inside(x,UI_HISTORY_CLEAR_X,UI_HISTORY_CLEAR_WIDTH)) return 10;
        if(inside(x,UI_HISTORY_OLDER_X,UI_HISTORY_PAGE_WIDTH)) return 11;
        if(inside(x,UI_HISTORY_NEWER_X,UI_HISTORY_PAGE_WIDTH)) return 12;
        return -1;
    }
#endif
#ifdef Z7_LANGUAGE_SWITCH
    if(x>=UI_LANGUAGE_X && x<UI_LANGUAGE_X+UI_LANGUAGE_WIDTH &&
       y>=UI_LANGUAGE_Y && y<UI_LANGUAGE_Y+UI_LANGUAGE_HEIGHT) return 5;
    if(state->language_menu) {
        if(x>=UI_LANGUAGE_X && x<UI_LANGUAGE_X+UI_LANGUAGE_WIDTH &&
           y>=UI_LANGUAGE_MENU_Y && y<UI_LANGUAGE_MENU_Y+2u*UI_LANGUAGE_ROW_HEIGHT)
            return 6+(int)((y-UI_LANGUAGE_MENU_Y)/UI_LANGUAGE_ROW_HEIGHT);
        return 8; /* Dismiss only; never activate an obscured/underlying control. */
    }
#else
    (void)state;
#endif
    if(y<530u || y>=586u) return -1;
#ifdef Z7_HISTORY_UI
    if(inside(x,844,156)) return 15;
#endif
    static const uint32_t bounds[][2]={{24,200},{212,356},{368,488},{558,614},{690,746}};
    for(uint32_t i=0;i<5u;++i) if(x>=bounds[i][0] && x<bounds[i][1]) return (int)i;
    return -1;
}
static int pointer_command(const struct touch_tap *tap,const struct ui_state *state) {
        int pressed=button(tap->press_x,tap->press_y,state),released=button(tap->x,tap->y,state),command=-1;
        if(pressed>=0 && released==pressed) {
            static const enum ui_command controls[]={UI_START,
#ifdef Z7_HISTORY_UI
                UI_OPEN_HISTORY,
#else
                UI_CLEAR,
#endif
                UI_TOGGLE_SOURCE,UI_FONT_MINUS,UI_FONT_PLUS,
                UI_LANGUAGE_MENU,UI_SELECT_EN_ZH,UI_SELECT_ZH_EN,UI_DISMISS_LANGUAGE,
                UI_CLOSE_HISTORY,UI_HISTORY_CLEAR,UI_HISTORY_OLDER,UI_HISTORY_NEWER,
                UI_HISTORY_CANCEL_CLEAR,UI_HISTORY_CONFIRM_CLEAR,
                UI_OPEN_SETTINGS,UI_CLOSE_SETTINGS,UI_TOGGLE_TTS,UI_VOLUME_MINUS,UI_VOLUME_PLUS,
                UI_TOGGLE_MUTE,UI_RATE_MINUS,UI_RATE_PLUS};
            command=controls[pressed];
            if((command==UI_SELECT_EN_ZH && state->language==UI_EN_ZH) ||
               (command==UI_SELECT_ZH_EN && state->language==UI_ZH_EN)) {
#ifdef Z7_HISTORY_UI
                command=state->settings_page?-1:UI_LANGUAGE_MENU;
#else
                command=UI_LANGUAGE_MENU;
#endif
            }
            if(pressed==0) command=state->session==UI_RUNNING?UI_PAUSE:
                (state->session==UI_PAUSED?UI_RESUME:(state->session==UI_IDLE?UI_START:-1));
        }
    return command;
}
uint32_t ui_touch_remote(struct ui_state *state,uint32_t px,uint32_t py,uint32_t x,uint32_t y) {
    if(px>=1024u || x>=1024u || py>=600u || y>=600u)return 0;
    int dx=(int)x-(int)px,dy=(int)y-(int)py;
#ifdef Z7_HISTORY_UI
    if(state->history_page && !state->history_confirm && (dy>12 || dy<-12) &&
       px>=40u && px<972u && py>=UI_HISTORY_BODY_Y && py<UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT) {
        return ui_history_scroll(state,-dy)?(uint32_t)UI_HISTORY_OLDER+1u:0u;
    }
#endif
    if(dx>12 || dx<-12 || dy>12 || dy<-12)return 0;
    struct touch_tap tap={px,py,x,y};int command=pointer_command(&tap,state);
    return command>=0 && ui_control(state,(enum ui_command)command)?(uint32_t)command+1u:0u;
}
void ui_touch_init(const volatile struct touch_link *link,volatile struct touch_feedback *feedback) {
    memset((void *)feedback,0,sizeof(*feedback));
#ifdef Z7_HISTORY_UI
    memset(&gesture,0,sizeof(gesture));
#endif
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
#ifdef Z7_HISTORY_UI
    uint32_t down=link->pressed,x=link->x,y=link->y;
#endif
    if(write-read>TOUCH_TAPS) return 0;
    struct touch_tap tap={0,0,0,0};
    if(read!=write) tap=link->taps[read%TOUCH_TAPS];
    barrier();
    if(link->magic!=TOUCH_LINK_MAGIC || link->generation!=generation || link->sequence!=sequence) return 0;
#ifdef Z7_HISTORY_UI
    if(generation!=feedback->generation) memset(&gesture,0,sizeof(gesture));
    if(state->history_page && !state->history_confirm) {
        /* A queued release belongs to the old contact; latest x/y may already
           describe the next finger press. Finish it before following live x/y. */
        if(read!=write && gesture.active) {
            int dx=(int)tap.x-(int)gesture.start_x,dy=(int)tap.y-(int)gesture.start_y;
            if(dx>12 || dx<-12 || dy>12 || dy<-12) gesture.dragged=1;
            if(gesture.allowed && gesture.dragged) (void)ui_history_scroll(state,(int)gesture.last_y-(int)tap.y);
            gesture.suppress=gesture.dragged;gesture.active=0;
        } else if(read!=write && !gesture.suppress) {
            int dy=(int)tap.press_y-(int)tap.y;
            if((dy>12 || dy<-12) && tap.press_x>=40u && tap.press_x<972u &&
               tap.press_y>=UI_HISTORY_BODY_Y && tap.press_y<UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT)
                (void)ui_history_scroll(state,dy);
        } else if(read==write) {
            if(down && !gesture.active) {
                gesture.active=1;gesture.start_x=x;gesture.start_y=gesture.last_y=y;
                gesture.dragged=0;
                gesture.allowed=x>=40u && x<972u && y>=UI_HISTORY_BODY_Y && y<UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT;
            }
            if(gesture.active) {
                int dx=(int)x-(int)gesture.start_x,dy=(int)y-(int)gesture.start_y;
                if(dx>12 || dx<-12 || dy>12 || dy<-12) gesture.dragged=1;
                if(gesture.allowed && gesture.dragged) (void)ui_history_scroll(state,(int)gesture.last_y-(int)y);
                gesture.last_y=y;
                if(!down) {gesture.suppress=gesture.dragged;gesture.active=0;}
            }
        }
    } else if(!down) gesture.active=0;
#endif
    if(read==write && generation==feedback->generation) return 0;
    ++feedback->sequence;barrier();
    feedback->generation=generation;
    uint32_t result=0;
    if(read!=write) {
        int command=pointer_command(&tap,state);
#ifdef Z7_HISTORY_UI
        int dx=(int)tap.x-(int)tap.press_x,dy=(int)tap.y-(int)tap.press_y;
        if(gesture.suppress || dx>12 || dx<-12 || dy>12 || dy<-12) command=-1;
        gesture.suppress=0;
#endif
        if(command>=0 && ui_control(state,(enum ui_command)command)) {
            if(command<7) ++feedback->accepted[command];
            else ++feedback->reserved; /* Extended controls; keep the 64-byte CPU1 ABI. */
            result=(uint32_t)command+1u;
        } else ++feedback->ignored;
        feedback->last_command=command>=0?(uint32_t)command:0xffffffffu;
        feedback->last_x=tap.x;feedback->last_y=tap.y;++read;
    }
    feedback->read_taps=read;
    barrier();++feedback->sequence;barrier();
    return result;
}
