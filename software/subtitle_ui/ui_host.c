#include "ui_render.h"
#include "ui_assets.h"
#include "ui_demo.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static void final_sample(struct ui_state *s,uint32_t segment,uint32_t index) {
    const struct ui_demo_text *sample=&ui_demo_samples[index%ui_demo_sample_count];
    struct ui_event e={UI_ASR_FINAL,s->epoch,segment,1,0,sample->source};ui_apply_event(s,&e);
    e.type=UI_TRANSLATING_EVENT;e.source_revision=1;e.text=0;ui_apply_event(s,&e);
    e.type=UI_TRANSLATION_FINAL;e.source_revision=1;e.text=sample->translated;ui_apply_event(s,&e);
}
EXPORT uint32_t ui_host_state_size(void) {return sizeof(struct ui_state);}
EXPORT void ui_host_format_elapsed(uint32_t elapsed,uint32_t text[16]) {ui_format_elapsed(elapsed,text);}
EXPORT int ui_host_draw(uint32_t scenario,uint32_t font_index,uint32_t page,uint32_t elapsed,
                       uint32_t *pixels,struct ui_render_stats *stats) {
    if(!ui_font_count || !ui_demo_sample_count || font_index>2u) return 0;
    struct ui_state s;ui_init(&s);
    if(scenario) {
        ui_control(&s,UI_START);final_sample(&s,1,0);final_sample(&s,2,1);
        if(scenario==2u) {
            uint32_t longest=0,maximum=0;
            for(uint32_t i=0;i<ui_demo_sample_count;++i) {
                uint32_t n=0;while(n<UI_MAX_TEXT && ui_demo_samples[i].translated[n]) ++n;
                if(n>maximum) {maximum=n;longest=i;}
            }
            final_sample(&s,3,longest);
        } else if(scenario==3u) {
            struct ui_event e={UI_ASR_PARTIAL,s.epoch,3,1,0,ui_demo_samples[2u%ui_demo_sample_count].source};
            ui_apply_event(&s,&e);
        } else if(scenario==4u) ui_control(&s,UI_PAUSE);
        else if(scenario==5u) ui_control(&s,UI_CLEAR);
        else if(scenario==6u) ui_control(&s,UI_TOGGLE_SOURCE);
        else if(scenario==7u || scenario==8u) {
            static const uint32_t bearing_text[]={'j','u','s','t',' ','a',' ','s','h','o','r','t',' ',
                                                 's','e','n','t','e','n','c','e',0};
            uint32_t long_text[UI_MAX_TEXT];
            for(uint32_t i=0;i<UI_MAX_TEXT-1u;++i) long_text[i]=(i%3u==2u)?' ':'j';
            long_text[UI_MAX_TEXT-1u]=0;
            struct ui_event e={UI_ASR_PARTIAL,s.epoch,3,1,0,scenario==7u?bearing_text:long_text};
            ui_apply_event(&s,&e);
        }
    }
    s.font_index=font_index;ui_render(pixels,&s,elapsed,page,stats);return 1;
}
EXPORT int ui_host_from_state(const void *bytes,uint32_t size,uint32_t elapsed,uint32_t page,
                             uint32_t *pixels,struct ui_render_stats *stats) {
    if(size!=sizeof(struct ui_state) || !bytes || !pixels || !stats) return 0;
    const struct ui_state *s=(const struct ui_state *)bytes;
    if(s->history_count>UI_HISTORY_MAX || s->history_count>s->final_count ||
       s->font_index>2u || s->show_source>1u || s->session>UI_BLOCKED || s->stage>UI_FINAL) return 0;
    ui_render(pixels,s,elapsed,page,stats);return 1;
}
