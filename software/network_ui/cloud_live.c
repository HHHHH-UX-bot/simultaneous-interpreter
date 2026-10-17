#include "cloud_live.h"
#include "cloud_wire.h"
#include "cloud_events.h"
#include "network.h"
#include "stream_pilot.h"
#include "cloud_client.h"
#include "protocol.h"
#include <string.h>
#define CLOUD_PAIRS 4u
struct cloud_pair {
    char source_id[CLOUD_ID_BYTES],target_id[CLOUD_ID_BYTES],response_id[CLOUD_ID_BYTES];
    uint32_t source[UI_MAX_TEXT],translated[UI_MAX_TEXT],source_ready,target_ready,complete,order;
};
struct cloud_link {char item[CLOUD_ID_BYTES],previous[CLOUD_ID_BYTES];};
struct cloud_recent {char source[CLOUD_ID_BYTES],target[CLOUD_ID_BYTES],response[CLOUD_ID_BYTES];};
struct cloud_context {
    struct ui_state *model;
    struct cloud_status live;
    struct cloud_event event;
    struct cloud_pair pairs[CLOUD_PAIRS];
    struct cloud_link links[8];
    struct cloud_recent recent[CLOUD_PAIRS];
    uint32_t order,next_segment,recent_cursor,report_ms;
};
__attribute__((section(".cloud_runtime"))) static struct cloud_context cloud;
__attribute__((section(".cloud_mailbox"),used)) volatile struct cloud_status cloud_report;
__attribute__((section(".cloud_final"),used)) volatile struct cloud_caption cloud_caption;
static void cloud_fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static int cloud_same(const char *a,const char *b) {while(*a && *a==*b) {++a;++b;}return *a==*b;}
static void cloud_id_copy(char *to,const char *from) {do {*to++=*from;} while(*from++);}
static void cloud_fail(uint32_t error) {if(!cloud.live.error) cloud.live.error=error;cloud.live.phase=6;}
static struct cloud_pair *cloud_pair_find(const char *id,int target,int create) {
    struct cloud_pair *empty=0;
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        struct cloud_pair *p=cloud.pairs+i;
        if(!p->order) {empty=p;continue;}
        if(cloud_same(target?p->target_id:p->source_id,id)) return p;
    }
    if(!create || !empty) return 0;
    empty->order=++cloud.order;cloud_id_copy(target?empty->target_id:empty->source_id,id);return empty;
}
static void cloud_commit(void) {
    for(;;) {
        struct cloud_pair *p=0;
        for(uint32_t i=0;i<CLOUD_PAIRS;++i) if(cloud.pairs[i].order && (!p || cloud.pairs[i].order<p->order)) p=cloud.pairs+i;
        if(!p || !p->source_ready || !p->target_ready || !p->complete) return;
        if(cloud.model->epoch!=cloud.live.epoch || cloud.model->session!=UI_RUNNING) {++cloud.live.ignored;memset(p,0,sizeof(*p));continue;}
        for(uint32_t i=0;i<UI_MAX_TEXT;++i) {
            if(p->source[i] && !net_glyph_available(p->source[i])) {cloud_fail(0x01000000u|p->source[i]);return;}
            if(p->translated[i] && !net_glyph_available(p->translated[i])) {cloud_fail(0x01000000u|p->translated[i]);return;}
        }
        uint32_t segment=++cloud.next_segment;
        struct ui_event event={UI_ASR_FINAL,cloud.live.epoch,segment,1,0,p->source};
        if(!ui_apply_event(cloud.model,&event)) {cloud_fail(20);return;}
        event.type=UI_TRANSLATING_EVENT;event.source_revision=1;event.text=0;
        if(!ui_apply_event(cloud.model,&event)) {cloud_fail(21);return;}
        event.type=UI_TRANSLATION_FINAL;event.source_revision=1;event.text=p->translated;
        if(!ui_apply_event(cloud.model,&event)) {cloud_fail(22);return;}
        ++cloud_caption.sequence;cloud_fence();
        for(uint32_t i=0;i<UI_MAX_TEXT;++i) {cloud_caption.source[i]=p->source[i];cloud_caption.translated[i]=p->translated[i];}
        cloud_fence();++cloud_caption.sequence;cloud_fence();
        struct cloud_recent *recent=cloud.recent+cloud.recent_cursor++%CLOUD_PAIRS;
        cloud_id_copy(recent->response,p->response_id);cloud_id_copy(recent->source,p->source_id);cloud_id_copy(recent->target,p->target_id);
        ++cloud.live.committed;memset(p,0,sizeof(*p));
    }
}
int cloud_live_message(const char *json,uint32_t size,void *user) {
    (void)user;
    struct cloud_event *e=&cloud.event;
    enum cloud_parse_code parsed=cloud_event_parse(json,size,e);
    if(parsed) {cloud_fail(100u+(uint32_t)parsed);return 1;}
    ++cloud.live.messages;
    if(cloud.live.phase!=1) {cloud_fail(2);return 1;}
    if(e->kind==CLOUD_IGNORE) return 0;
    if(e->kind==CLOUD_SESSION_CREATED) {cloud_client_session_created();return 0;}
    if(e->kind==CLOUD_SESSION_UPDATED) {cloud_client_session_updated();return 0;}
    if(e->kind==CLOUD_SPEECH_STARTED) {cloud_client_speech_started();return 0;}
    if(e->kind==CLOUD_TRANSLATION_DELTA) {cloud_client_translation(0);return 0;}
    if(e->kind==CLOUD_ERROR) {cloud_client_stop();cloud_fail(3);return 1;}
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        const struct cloud_recent *recent=cloud.recent+i;
        if((e->response_id[0] && cloud_same(e->response_id,recent->response)) ||
            (e->item_id[0] && (cloud_same(e->item_id,recent->source) || cloud_same(e->item_id,recent->target)))) return 0;
    }
    if(e->kind==CLOUD_ITEM_LINK) {
        for(uint32_t i=0;i<8;++i) if(!cloud.links[i].item[0] || cloud_same(cloud.links[i].item,e->item_id)) {
            cloud_id_copy(cloud.links[i].item,e->item_id);cloud_id_copy(cloud.links[i].previous,e->previous_id);return 0;
        }
        cloud_fail(4);return 1;
    }
    if(e->kind==CLOUD_FINISHED) {
        cloud_commit();
        for(uint32_t i=0;i<CLOUD_PAIRS;++i) if(cloud.pairs[i].order) {cloud_fail(5);return 1;}
        cloud.live.phase=2;return 0;
    }
    struct cloud_pair *p=0;
    if(e->kind==CLOUD_ASR_FINAL) {
        ++cloud.live.asr_final;p=cloud_pair_find(e->item_id,0,1);
        if(p) {memcpy(p->source,e->text,sizeof(p->source));p->source_ready=1;}
        for(uint32_t i=0;i<8;++i) if(cloud_same(cloud.links[i].item,e->item_id)) cloud.links[i].item[0]=0;
    } else if(e->kind==CLOUD_TRANSLATION) {
        ++cloud.live.translations;cloud_client_translation(0);p=cloud_pair_find(e->item_id,1,0);
        if(!p) {
            for(uint32_t i=0;i<8;++i) if(cloud_same(cloud.links[i].item,e->item_id)) {
                p=cloud_pair_find(cloud.links[i].previous,0,1);
                if(p) cloud_id_copy(p->target_id,e->item_id);
                cloud.links[i].item[0]=0;break;
            }
        }
        if(p) {cloud_id_copy(p->response_id,e->response_id);memcpy(p->translated,e->text,sizeof(p->translated));p->target_ready=1;}
    } else if(e->kind==CLOUD_RESPONSE_DONE) {
        for(uint32_t i=0;i<CLOUD_PAIRS;++i) if(cloud.pairs[i].order && cloud_same(cloud.pairs[i].response_id,e->response_id)) {p=cloud.pairs+i;break;}
        if(!e->completed) {cloud_fail(6);return 1;}
        if(p) p->complete=1;
        cloud_client_response_done();
    }
    if(!p) {cloud_fail(7);return 1;}
    cloud_commit();return cloud.live.error!=0;
}
static void cloud_receive(uint8_t byte) {++cloud.live.received;cloud_wire_byte(byte);}
void cloud_live_init(struct ui_state *model) {
    memset(&cloud,0,sizeof(cloud));cloud.model=model;cloud.live.magic=0x31564c43u;
    memset((void *)&cloud_report,0,sizeof(cloud_report));memset((void *)&cloud_caption,0,sizeof(cloud_caption));
    cloud.report_ms=(uint32_t)-200;
}
/* The board's cloud client owns the link and calls this once when the TLS
   session is ready, so the model/session state normally set by the host's
   cloud_report.request=1 path is entered here instead. */
void cloud_live_begin(uint32_t now) {
    struct ui_state *model=cloud.model;
    memset(&cloud,0,sizeof(cloud));cloud.model=model;
    cloud.live.magic=0x31564c43u;cloud.live.phase=1;cloud.live.ack=1;
    cloud.report_ms=now-200u;
    (void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);
    /* The product shows only the Chinese translation; the ASR source line is
       off unless the user turns it on with the original-text button. */
    if(model->show_source) (void)ui_control(model,UI_TOGGLE_SOURCE);
    cloud.live.epoch=model->epoch;
    memset((void *)&cloud_caption,0,sizeof(cloud_caption));
    cloud_wire_init(cloud_live_message,0);
}
void cloud_live_tick(uint32_t now) {
    if(cloud_report.request!=cloud.live.ack) {
        uint32_t request=cloud_report.request;
        if(network_info.phase==0 && (stream_pilot.phase==0 || stream_pilot.phase==3)) {
            if(request==1) {
                struct ui_state *model=cloud.model;memset(&cloud,0,sizeof(cloud));cloud.model=model;
                cloud.live.magic=0x31564c43u;cloud.live.phase=1;cloud.live.ack=1;cloud.report_ms=now-200;
                (void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);cloud.live.epoch=model->epoch;
                memset((void *)&cloud_caption,0,sizeof(cloud_caption));cloud_wire_init(cloud_live_message,0);
                stream_pilot_set_receiver(cloud_receive);
            } else if(request==2) {cloud.live.phase=0;cloud.live.error=0;cloud.live.ack=2;stream_pilot_set_receiver(0);}
        }
    }
    if(cloud.live.phase==1 || cloud.live.phase==2) {
        const struct cloud_wire_stats *wire=cloud_wire_status();cloud.live.pings=wire->pings;
        cloud.live.closed=wire->closed;cloud.live.mask_used=wire->mask_used;
        if(wire->error) {cloud_client_stop();cloud_fail(200u+wire->error);}
        if(wire->closed && cloud.live.phase==1) {cloud_client_stop();cloud_fail(8);}
        uint32_t bytes;const uint8_t *reply=cloud_wire_reply(&bytes);
        if(bytes && network_info.phase==4 && network_send(reply,bytes)) cloud_wire_reply_sent();
    }
    if(now-cloud.report_ms<200 && cloud_report.phase==cloud.live.phase && cloud_report.ack==cloud.live.ack) return;
    cloud.report_ms=now;++cloud_report.sequence;cloud_fence();
    volatile uint32_t *out=(volatile uint32_t *)&cloud_report;const uint32_t *in=(const uint32_t *)&cloud.live;
    for(uint32_t i=0;i<16;++i) if(i!=1 && i!=2) out[i]=in[i];
    cloud_fence();++cloud_report.sequence;cloud_fence();
}
int cloud_live_owns_ui(void) {return cloud.live.phase!=0;}
