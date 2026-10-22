#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../software/subtitle_ui/ui_model.h"
#include "../software/subtitle_ui/ui_render.h"
#include "../software/subtitle_ui/ui_touch.h"
static struct ui_state s;
static struct touch_link link;
static struct touch_feedback feedback;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"settings failure line %d\n",__LINE__);exit(1);}}while(0)
static uint32_t tap(unsigned x,unsigned y,unsigned end_x,unsigned end_y) {
    link.taps[link.tap_write%TOUCH_TAPS]=(struct touch_tap){x,y,end_x,end_y};
    ++link.tap_write;link.sequence+=2;
    return ui_touch_poll(&link,&feedback,&s);
}
static uint32_t click(unsigned x,unsigned y) {return tap(x,y,x,y);}
static void render(const char *name) {
    static uint32_t pixels[1024*600];struct ui_render_stats stats;
    ui_render(pixels,&s,99000,0,&stats);
    ui_render_latency(pixels,ui_latency_pack(62,1,870,1),&stats);
    CHECK(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
    const char *folder=getenv("SETTINGS_PREVIEW");
    if(folder) {char path[1024];snprintf(path,sizeof(path),"%s/%s.rgbx",folder,name);
        FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(pixels,4,1024*600,f)==1024*600);fclose(f);}
}
int main(void) {
    ui_init(&s);CHECK(s.tts_enabled && s.output_volume==70 && s.rate_index==1);
    CHECK(ui_control(&s,UI_START));s.final_count=8;s.history_count=1;s.translated[0]='A';
    unsigned epoch=s.epoch;
    link.magic=TOUCH_LINK_MAGIC;link.generation=42;ui_touch_init(&link,&feedback);
    CHECK(click(910,554)==UI_OPEN_SETTINGS+1u && s.settings_page);
    CHECK(s.epoch==epoch && s.session==UI_RUNNING && s.final_count==8 && s.history_count==1 && s.translated[0]=='A');
    render("settings_default");
    CHECK(click(350,230)==UI_VOLUME_PLUS+1u && s.output_volume==80);
    CHECK(click(200,230)==UI_VOLUME_MINUS+1u && s.output_volume==70);
    CHECK(click(100,420)==UI_TOGGLE_MUTE+1u && s.output_muted);
    CHECK(click(100,420)==UI_TOGGLE_MUTE+1u && !s.output_muted);
    CHECK(click(200,306)==UI_RATE_MINUS+1u && s.rate_index==0);
    CHECK(click(350,306)==UI_RATE_PLUS+1u && s.rate_index==1);
    CHECK(click(350,306)==UI_RATE_PLUS+1u && s.rate_index==2);
    CHECK(!click(350,306) && s.rate_index==2);
    CHECK(click(350,165)==UI_TOGGLE_TTS+1u && !s.tts_enabled);
    CHECK(click(350,165)==UI_TOGGLE_TTS+1u && s.tts_enabled);
    CHECK(click(940,160)==UI_FONT_PLUS+1u && s.font_index==2);
    CHECK(click(680,160)==UI_FONT_MINUS+1u && s.font_index==1);
    CHECK(click(860,230)==UI_TOGGLE_SOURCE+1u && !s.show_source);
    CHECK(!tap(350,165,70,550) && s.settings_page && s.tts_enabled);
    CHECK(!click(280,555) && !s.history_page);
    CHECK(s.epoch==epoch && s.session==UI_RUNNING && s.history_count==1);
    CHECK(click(70,555)==UI_CLOSE_SETTINGS+1u && !s.settings_page);
    CHECK(!s.show_source && s.font_index==1 && s.rate_index==2);
    s.voice_status=3;s.voice_latency_ms=850;s.voice_latency_valid=1;render("speech_main");
    CHECK(ui_control(&s,UI_OPEN_SETTINGS));
    CHECK(click(850,380)==UI_SELECT_ZH_EN+1u && s.language==UI_ZH_EN && s.settings_page);
    CHECK(s.epoch==epoch+1 && s.history_count==1 && s.tts_enabled && s.output_volume==70);
    CHECK(!click(850,380) && !s.language_menu);
    CHECK(ui_control(&s,UI_CLEAR));CHECK(s.rate_index==2 && s.output_volume==70 && s.settings_page);
    s.output_volume=100;CHECK(!ui_control(&s,UI_VOLUME_PLUS));s.output_volume=0;CHECK(!ui_control(&s,UI_VOLUME_MINUS));
    render("settings_changed");
    printf("PASS_SETTINGS_UI checks=%u model_bytes=%u feedback_bytes=%u\n",checks,(unsigned)sizeof(s),(unsigned)sizeof(feedback));
    return 0;
}
