#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_demo.h"
#include "ui_assets.h"

static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#c);return 1;}} while(0)
static int equal(const uint32_t *a,const uint32_t *b) {
    for(unsigned i=0;i<UI_MAX_TEXT;++i) {if(a[i]!=b[i]) return 0;if(!a[i]) return 1;}return 0;
}
static uint32_t length(const uint32_t *a) {uint32_t n=0;while(n<UI_MAX_TEXT && a[n]) ++n;return n;}
int main(void) {
    struct ui_state s;
    struct ui_demo_clock d;
    uint32_t now=0;
    ui_demo_init(&d,&s,now);
    CHECK(s.session==UI_RUNNING && s.stage==UI_LISTENING && s.history_count==0);
    for(unsigned sample=0;sample<ui_demo_sample_count*2u;++sample) {
        const struct ui_demo_text *text=&ui_demo_samples[sample%ui_demo_sample_count];
        ui_demo_tick(&d,&s,now+1199u);CHECK(d.phase==0);
        now+=1200u;ui_demo_tick(&d,&s,now);CHECK(d.phase==1 && !s.source_final);
        CHECK(s.history_count==(sample<UI_HISTORY_MAX?sample:UI_HISTORY_MAX));
        now+=900u;ui_demo_tick(&d,&s,now);CHECK(d.phase==2 && s.source_final && equal(s.source,text->source));
        now+=900u;ui_demo_tick(&d,&s,now);CHECK(d.phase==3 && s.stage==UI_TRANSLATING);
        now+=1000u;ui_demo_tick(&d,&s,now);CHECK(d.phase==4 && s.stage==UI_FINAL);
        CHECK(s.final_count==sample+1u && equal(s.translated,text->translated));
        uint32_t hold=length(text->translated)>90u?28000u:6500u;
        ui_demo_tick(&d,&s,now+hold-1u);CHECK(d.phase==4);
        now+=hold;ui_demo_tick(&d,&s,now);CHECK(d.phase==0 && s.stage==UI_LISTENING);
        CHECK(s.rejected_events==0);
    }
    now+=1200u;ui_demo_tick(&d,&s,now);
    CHECK(d.phase==1 && ui_control(&s,UI_PAUSE));
    uint32_t finals=s.final_count,epoch=s.epoch;
    ui_demo_tick(&d,&s,now+60000u);
    CHECK(s.final_count==finals && s.session==UI_PAUSED);
    CHECK(equal(s.source,s.last_final_source));
    CHECK(ui_control(&s,UI_RESUME));now+=60000u;ui_demo_tick(&d,&s,now);
    CHECK(s.epoch>epoch && d.phase==0 && d.segment_id==1 && s.final_count==finals);
    now+=1200u;ui_demo_tick(&d,&s,now);CHECK(d.phase==1);
    CHECK(ui_control(&s,UI_CLEAR));ui_demo_tick(&d,&s,now);
    CHECK(s.session==UI_IDLE && s.history_count==0 && s.final_count==0 && d.phase==0);
    ui_demo_tick(&d,&s,now+60000u);CHECK(s.history_count==0 && !s.source[0] && !s.translated[0]);
    CHECK(ui_control(&s,UI_START));now+=60000u;ui_demo_tick(&d,&s,now);
    CHECK(d.phase==0 && d.epoch==s.epoch);
    /* Timer rollover must not skip or stall the first ASR event. */
    now=0xffffff00u;ui_demo_init(&d,&s,now);
    ui_demo_tick(&d,&s,now+1199u);CHECK(d.phase==0);
    now+=1200u;ui_demo_tick(&d,&s,now);CHECK(d.phase==1);
    /* One simulated hour exercises bounded history and repeated demo transitions. */
    now=0;ui_demo_init(&d,&s,now);
    for(now=500u;now<=3600000u;now+=500u) {
        ui_demo_tick(&d,&s,now);
        CHECK(s.history_count<=UI_HISTORY_MAX && s.rejected_events==0);
    }
    CHECK(s.final_count>150u && s.history_count==UI_HISTORY_MAX);
    printf("PASS_SUBTITLE_DEMO checks=%u simulated_ms=3600000 finals=%u\n",checks,s.final_count);
    return 0;
}
