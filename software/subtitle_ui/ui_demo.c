#include "ui_demo.h"
#include "ui_assets.h"

static uint32_t length(const uint32_t *text) {uint32_t n=0;while(n<UI_MAX_TEXT && text[n]) ++n;return n;}
void ui_demo_init(struct ui_demo_clock *d,struct ui_state *s,uint32_t now) {
    ui_init(s);ui_control(s,UI_START);
    d->epoch=s->epoch;d->phase=0;d->sample_index=0;d->segment_id=1;d->marked_ms=now;d->partial_size=0;
}
void ui_demo_tick(struct ui_demo_clock *d,struct ui_state *s,uint32_t now) {
    if(d->epoch!=s->epoch) {d->epoch=s->epoch;d->phase=0;d->segment_id=1;d->marked_ms=now;}
    if(s->session!=UI_RUNNING) {d->marked_ms=now;return;}
    if(!ui_demo_sample_count) return;
    const struct ui_demo_text *sample=&ui_demo_samples[d->sample_index%ui_demo_sample_count];
    uint32_t elapsed=now-d->marked_ms;
    uint32_t wait=d->phase==0?1200u:(d->phase<3?900u:(d->phase==3?1000u:
                                (length(sample->translated)>90u?28000u:6500u)));
    if(elapsed<wait) return;
    struct ui_event event={UI_ASR_PARTIAL,s->epoch,d->segment_id,1,0,0};
    uint32_t draft[UI_MAX_TEXT];
    switch(d->phase) {
    case 0:
        d->partial_size=length(sample->source)/2u;if(!d->partial_size) d->partial_size=1;
        for(uint32_t i=0;i<d->partial_size;++i) draft[i]=sample->source[i];
        draft[d->partial_size]=0;
        event.text=draft;break;
    case 1:event.type=UI_ASR_FINAL;event.revision=2;event.text=sample->source;break;
    case 2:event.type=UI_TRANSLATING_EVENT;event.source_revision=2;break;
    case 3:event.type=UI_TRANSLATION_FINAL;event.source_revision=2;event.text=sample->translated;break;
    default:
        ++d->sample_index;++d->segment_id;d->phase=0;d->marked_ms=now;
        s->stage=UI_LISTENING;++s->change_sequence;return;
    }
    if(ui_apply_event(s,&event)) {++d->phase;d->marked_ms=now;}
}
