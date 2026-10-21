#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../software/subtitle_ui/ui_model.h"
#include "../software/subtitle_ui/ui_render.h"
#include "../software/subtitle_ui/ui_touch.h"
#include "../software/subtitle_ui/ui_layout.h"
#include "../software/subtitle_ui/ui_assets.h"

static struct ui_state state;
static struct touch_link link;
static struct touch_feedback feedback;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"history check failed line %d\n",__LINE__);exit(1);}}while(0)
static void append(unsigned id,unsigned sample) {
    const struct ui_demo_text *p=ui_demo_samples+sample%ui_demo_sample_count;
    const uint32_t *source=state.language==UI_EN_ZH?p->source:p->translated;
    const uint32_t *target=state.language==UI_EN_ZH?p->translated:p->source;
    struct ui_event e={UI_ASR_FINAL,state.epoch,id,1,0,source};CHECK(ui_apply_event(&state,&e));
    e.type=UI_TRANSLATING_EVENT;e.source_revision=1;CHECK(ui_apply_event(&state,&e));
    e.type=UI_TRANSLATION_FINAL;e.source_revision=1;e.text=target;CHECK(ui_apply_event(&state,&e));
    ui_history_prepare(&state);
}
static uint32_t tap(uint32_t x,uint32_t y,uint32_t end_x,uint32_t end_y) {
    link.taps[link.tap_write%TOUCH_TAPS]=(struct touch_tap){x,y,end_x,end_y};
    ++link.tap_write;link.sequence+=2;link.pressed=0;link.x=end_x;link.y=end_y;
    return ui_touch_poll(&link,&feedback,&state);
}
static uint32_t click(uint32_t x,uint32_t y) {return tap(x,y,x,y);}
static void position(int down,unsigned x,unsigned y) {
    link.pressed=(uint32_t)down;link.x=x;link.y=y;link.sequence+=2;
    (void)ui_touch_poll(&link,&feedback,&state);
}
static void render(uint32_t *pixels,const char *name) {
    struct ui_render_stats stats;ui_render(pixels,&state,93400,0,&stats);
    ui_render_latency(pixels,ui_latency_pack(62,1,850,1),&stats);
    CHECK(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
    const char *folder=getenv("HISTORY_PREVIEW");
    if(folder && name) {char path[1024];snprintf(path,sizeof(path),"%s/%s.rgbx",folder,name);
        FILE *f=fopen(path,"wb");CHECK(f!=0);CHECK(fwrite(pixels,4,UI_WIDTH*UI_HEIGHT,f)==UI_WIDTH*UI_HEIGHT);fclose(f);}
}
int main(void) {
    const unsigned count=UI_WIDTH*UI_HEIGHT,pad=64;
    uint32_t *memory=malloc((count+pad*2)*4u),*other=malloc(count*4u);CHECK(memory && other);
    uint32_t *pixels=memory+pad;
    for(unsigned i=0;i<pad;++i)memory[i]=memory[pad+count+i]=0xa5c35a3c;
    ui_init(&state);CHECK(ui_control(&state,UI_START));
    link.magic=TOUCH_LINK_MAGIC;link.generation=42;ui_touch_init(&link,&feedback);
    CHECK(sizeof(state)==135768u && sizeof(struct touch_feedback)==64u);
    for(unsigned i=1;i<=72;++i) {
        if(i%7==0) {uint32_t old_count=state.history_count,old_epoch=state.epoch;
            CHECK(ui_control(&state,state.language==UI_EN_ZH?UI_SELECT_ZH_EN:UI_SELECT_EN_ZH));
            CHECK(state.history_count==old_count && state.epoch==old_epoch+1 && state.session==UI_RUNNING);}
        append(i,i%ui_demo_sample_count);
    }
    CHECK(state.history_count==64 && state.final_count==72 && state.history_discarded==8);
    CHECK(state.history[0].segment_id==9 && state.history[63].segment_id==72);
    render(pixels,"main");uint32_t epoch=state.epoch;
    CHECK(click(280,555)==UI_OPEN_HISTORY+1u);ui_history_prepare(&state);
    CHECK(state.history_page && state.epoch==epoch && state.session==UI_RUNNING);
    CHECK(state.history_scroll==state.history_limit && state.history_limit>348);
    render(pixels,"latest");
    uint32_t offsets[]={0,1,23,347,state.history_limit/2,state.history_limit-1,state.history_limit};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        state.history_follow=0;state.history_scroll=offsets[i];ui_history_prepare(&state);render(pixels,i==0?"oldest":(i==4?"middle":0));
    }
    state.history_scroll=state.history_limit;state.history_follow=1;
    position(1,300,250);position(1,300,350);
    CHECK(state.history_scroll==state.history_limit-100 && !state.history_follow);
    uint32_t old_scroll=state.history_scroll;position(1,300,390);CHECK(state.history_scroll==old_scroll-40);
    CHECK(!tap(300,250,300,390));CHECK(state.history_page && state.epoch==epoch);
    /* Release and next Back press can both arrive during one render. */
    state.history_scroll=state.history_limit;state.history_follow=1;
    position(1,300,250);position(1,300,350);old_scroll=state.history_scroll;
    link.taps[link.tap_write%TOUCH_TAPS]=(struct touch_tap){300,250,300,350};++link.tap_write;
    link.pressed=1;link.x=70;link.y=555;link.sequence+=2;
    CHECK(!ui_touch_poll(&link,&feedback,&state) && state.history_scroll==old_scroll);
    position(1,70,555);CHECK(click(70,555)==UI_CLOSE_HISTORY+1u && !state.history_page);
    CHECK(click(280,555)==UI_OPEN_HISTORY+1u);ui_history_prepare(&state);
    CHECK(ui_history_scroll(&state,-500));old_scroll=state.history_scroll;
    CHECK(!tap(300,350,300,200) && state.history_scroll==old_scroll+150u);
    CHECK(ui_history_scroll(&state,-140));
    old_scroll=state.history_scroll;unsigned removed=state.history[0].pixel_height;append(73,0);
    CHECK(state.history_scroll==(old_scroll>removed?old_scroll-removed:0) && !state.history_follow);
    CHECK(click(580,555)==UI_HISTORY_OLDER+1u);CHECK(state.history_scroll<old_scroll);
    CHECK(click(700,555)==UI_HISTORY_NEWER+1u);
    CHECK(ui_history_scroll(&state,-1000000) && state.history_scroll==0);
    CHECK(!ui_history_scroll(&state,-1));CHECK(ui_history_scroll(&state,1000000));
    CHECK(state.history_follow && state.history_scroll==state.history_limit);
    CHECK(!ui_history_scroll(&state,1));
    CHECK(click(880,555)==UI_HISTORY_CLEAR+1u && state.history_confirm);render(pixels,"confirm");
    CHECK(!click(70,555) && state.history_page && state.history_confirm); /* Modal blocks Back. */
    CHECK(click(410,350)==UI_HISTORY_CANCEL_CLEAR+1u && state.history_count==64);
    CHECK(click(880,555)==UI_HISTORY_CLEAR+1u);
    enum ui_session session=state.session;uint32_t source=state.source[0],target=state.translated[0],finals=state.final_count;
    CHECK(click(600,350)==UI_HISTORY_CONFIRM_CLEAR+1u);
    CHECK(!state.history_count && !state.history_confirm && state.history_page);
    CHECK(state.epoch==epoch && state.session==session && state.source[0]==source && state.translated[0]==target && state.final_count==finals);
    CHECK(!click(880,555));render(pixels,"empty");
    CHECK(click(70,555)==UI_CLOSE_HISTORY+1u && !state.history_page);
    CHECK(ui_control(&state,UI_PAUSE));CHECK(click(280,555)==UI_OPEN_HISTORY+1u);
    CHECK(state.session==UI_PAUSED && state.epoch==epoch);CHECK(click(70,555)==UI_CLOSE_HISTORY+1u);
    CHECK(ui_control(&state,UI_RESUME));append(1,0);CHECK(state.history_count==1);
    CHECK(ui_control(&state,UI_OPEN_HISTORY));ui_history_prepare(&state);render(pixels,"single");
    /* Swapping stored source/translation together with its language preserves
       left English / right Chinese pixels, independently of current direction. */
    memcpy(other,pixels,count*4u);uint32_t temp[UI_MAX_TEXT];
    memcpy(temp,state.history[0].source,sizeof(temp));
    memcpy(state.history[0].source,state.history[0].translated,sizeof(temp));
    memcpy(state.history[0].translated,temp,sizeof(temp));
    state.history[0].language=state.history[0].language==UI_EN_ZH?UI_ZH_EN:UI_EN_ZH;
    state.history[0].pixel_height=0;ui_history_prepare(&state);render(pixels,0);
    CHECK(memcmp(other,pixels,count*4u)==0);
    for(unsigned i=0;i<pad;++i) CHECK(memory[i]==0xa5c35a3c && memory[pad+count+i]==0xa5c35a3c);
    free(other);free(memory);
    printf("PASS_HISTORY_UI checks=%u capacity=%u model_bytes=%u feedback_bytes=%u\n",checks,UI_HISTORY_MAX,(unsigned)sizeof(state),(unsigned)sizeof(feedback));return 0;
}
