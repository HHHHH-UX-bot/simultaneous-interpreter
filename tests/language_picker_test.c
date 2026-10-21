#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../software/subtitle_ui/ui_touch.h"
#include "../software/subtitle_ui/ui_layout.h"
#include "../software/network_ui/language_profile.h"

static struct touch_link link;
static struct touch_feedback feedback;
static struct ui_state state;
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"language picker failure line %d\n",__LINE__);exit(1);}} while(0)
static uint32_t tap(uint32_t x,uint32_t y,uint32_t end_x,uint32_t end_y) {
    link.taps[link.tap_write%TOUCH_TAPS]=(struct touch_tap){x,y,end_x,end_y};
    ++link.tap_write;link.sequence+=2;
    return ui_touch_poll(&link,&feedback,&state);
}
static uint32_t click(uint32_t x,uint32_t y) {return tap(x,y,x,y);}
int main(void) {
    link.magic=TOUCH_LINK_MAGIC;link.generation=7;
    ui_init(&state);ui_touch_init(&link,&feedback);
#ifndef Z7_LANGUAGE_SWITCH
    CHECK(!click(300,30) && !state.language_menu);
    CHECK(!ui_control(&state,UI_LANGUAGE_MENU) && !ui_control(&state,UI_SELECT_ZH_EN));
    puts("PASS_LANGUAGE_PICKER_LEGACY no unsupported runtime menu");
    return 0;
#else
    CHECK(ui_control(&state,UI_START));
    state.source[0]='a';state.translated[0]='b';state.final_count=state.history_count=1;
    CHECK(click(300,30)==UI_LANGUAGE_MENU+1 && state.language_menu);
    CHECK(click(300,UI_LANGUAGE_MENU_Y+10)==UI_LANGUAGE_MENU+1 && !state.language_menu);
    CHECK(state.final_count==1 && state.language==UI_EN_ZH); /* Same selection only dismisses. */
    CHECK(click(300,30)==UI_LANGUAGE_MENU+1);
    uint32_t font=state.font_index,epoch=state.epoch;
    CHECK(click(300,UI_LANGUAGE_MENU_Y+UI_LANGUAGE_ROW_HEIGHT+10)==UI_SELECT_ZH_EN+1);
    CHECK(state.language==UI_ZH_EN && state.epoch==epoch+1 && !state.language_menu);
    CHECK(!state.source[0] && !state.translated[0] && !state.history_count && !state.final_count);
    CHECK(state.font_index==font && state.session==UI_RUNNING && state.stage==UI_CONNECTING);
    CHECK(click(300,30)==UI_LANGUAGE_MENU+1);
    CHECK(click(715,555)==UI_DISMISS_LANGUAGE+1 && !state.language_menu && state.font_index==font);
    CHECK(click(715,555)==UI_FONT_PLUS+1 && state.font_index==font+1);
    CHECK(click(300,30)==UI_LANGUAGE_MENU+1);
    CHECK(!tap(300,UI_LANGUAGE_MENU_Y+10,300,UI_LANGUAGE_MENU_Y+UI_LANGUAGE_ROW_HEIGHT+10));
    CHECK(state.language==UI_ZH_EN && state.language_menu); /* Drag between rows cannot select. */
    CHECK(click(900,350)==UI_DISMISS_LANGUAGE+1 && !state.language_menu);
    CHECK(ui_control(&state,UI_PAUSE));
    CHECK(click(300,30)==UI_LANGUAGE_MENU+1);
    CHECK(click(300,UI_LANGUAGE_MENU_Y+10)==UI_SELECT_EN_ZH+1);
    CHECK(state.session==UI_PAUSED && state.language==UI_EN_ZH);
    CHECK(feedback.accepted[UI_FONT_PLUS]==1 && feedback.reserved==10 && feedback.ignored==1);
    CHECK(sizeof(state)==11392 && sizeof(feedback)==64);
    puts("PASS_LANGUAGE_PICKER touch, dismiss, drag, same selection, epoch and paused change");
    return 0;
#endif
}
