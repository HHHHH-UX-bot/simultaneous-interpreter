#include <stdio.h>
#include <stdlib.h>
#include "../software/subtitle_ui/ui_touch.h"
static struct ui_state s;static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"remote pointer line %d\n",__LINE__);exit(1);}}while(0)
static uint32_t click(unsigned x,unsigned y){return ui_touch_remote(&s,x,y,x,y);}
int main(void){
    ui_init(&s);CHECK(ui_control(&s,UI_START));unsigned epoch=s.epoch;
    CHECK(click(910,554)==UI_OPEN_SETTINGS+1u && s.settings_page);
    CHECK(click(350,230)==UI_VOLUME_PLUS+1u && s.output_volume==80);
    CHECK(click(200,230)==UI_VOLUME_MINUS+1u && s.output_volume==70);
    CHECK(click(100,420)==UI_TOGGLE_MUTE+1u && s.output_muted);
    CHECK(click(100,420)==UI_TOGGLE_MUTE+1u && !s.output_muted);
    CHECK(click(350,165)==UI_TOGGLE_TTS+1u && !s.tts_enabled);
    CHECK(click(350,165)==UI_TOGGLE_TTS+1u && s.tts_enabled);
    CHECK(!ui_touch_remote(&s,350,165,70,550) && s.settings_page && s.tts_enabled);
    CHECK(!ui_touch_remote(&s,1024,100,100,100));CHECK(s.epoch==epoch && s.session==UI_RUNNING);
    CHECK(click(70,555)==UI_CLOSE_SETTINGS+1u && !s.settings_page);
    CHECK(click(270,555)==UI_OPEN_HISTORY+1u && s.history_page);
    CHECK(click(70,555)==UI_CLOSE_HISTORY+1u && !s.history_page);
    CHECK(click(910,554)==UI_OPEN_SETTINGS+1u);
    CHECK(click(850,380)==UI_SELECT_ZH_EN+1u && s.language==UI_ZH_EN && s.epoch==epoch+1);
    printf("PASS_REMOTE_POINTER checks=%u shared hit test, settings, real preferences, history, drag guard, epoch\n",checks);return 0;
}
