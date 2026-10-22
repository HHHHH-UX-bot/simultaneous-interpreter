#include "language_profile.h"
#include "cloud_live.h"
#ifdef Z7_ARM_TLS
#include "arm_tls.h"
#define network_send arm_tls_send
#endif
#include "cloud_wire.h"
#include "cloud_events.h"
#include "network.h"
#include "stream_pilot.h"
#include "cloud_client.h"
#include "speech_playback.h"
#include "protocol.h"
#ifdef Z7_MMU_CACHE
#include "../ps_bringup/cache.h"
#endif
#include <string.h>
#ifdef Z7_BILINGUAL
#define CLOUD_PAIRS 16u
#define CLOUD_LINKS 32u
#else
#define CLOUD_PAIRS 4u
#define CLOUD_LINKS 8u
#endif
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
    struct cloud_link links[CLOUD_LINKS];
    struct cloud_recent recent[CLOUD_PAIRS];
    uint32_t order,next_segment,recent_cursor,report_ms;
};
__attribute__((section(".cloud_runtime"))) static struct cloud_context cloud;
/* Every committed line is mirrored here so the host can read back the exact
   text that reached the screen and compare it with what was actually said.
   The struct lives in cached DDR, so each record cleans its line. */
#define CLOUD_LOG_ENTRIES 8u
#define CLOUD_LOG_CHARS 96u
struct cloud_text_log {
    uint32_t magic,count;
    uint32_t text[CLOUD_LOG_ENTRIES][CLOUD_LOG_CHARS];
};
#define CLOUD_TEXT_LOG_MAGIC 0x474C5443u   /* 'CTLG' */
__attribute__((section(".cloud_runtime"),used)) static volatile struct cloud_text_log text_log;
static void text_log_record(const uint32_t *source) {
    uint32_t slot=text_log.count%CLOUD_LOG_ENTRIES;
    for(uint32_t i=0;i<CLOUD_LOG_CHARS;++i)
        text_log.text[slot][i]=(i<UI_MAX_TEXT)?source[i]:0u;
    ++text_log.count;
    text_log.magic=CLOUD_TEXT_LOG_MAGIC;
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)&text_log,(unsigned)sizeof(text_log));
#endif
}
__attribute__((section(".cloud_mailbox"),used)) volatile struct cloud_status cloud_report;
__attribute__((section(".cloud_final"),used)) volatile struct cloud_caption cloud_caption;
/* [6] empty ASR finals; [7] explicitly empty final targets; [8] response.done
   final text used without a separate transcript.done. Empty results never
   become fabricated paired history. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t session_diag[9];
/* Boot-lifetime counters survive a reconnect; no utterances or credentials. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t cloud_health[16];
__attribute__((section(".cloud_runtime"),used)) static volatile char cloud_error_code[CLOUD_ID_BYTES];
static uint32_t health_now;
static int capture_enabled;
#ifdef Z7_EVENT_ID_TRACE
/* Optional event-order evidence: hashed identifiers and lengths, never text. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t cloud_id_trace[1+2048*8];
static uint32_t id_hash(const char *s) {
    if(!*s) return 0;
    uint32_t h=2166136261u;while(*s) {h^=(uint8_t)*s++;h*=16777619u;}return h;
}
static void id_trace(const struct cloud_event *e,uint32_t parsed,const char *json,uint32_t bytes) {
    if(!parsed && e->kind==CLOUD_IGNORE) return;
#ifndef Z7_LATENCY_PROBE
    if(!parsed && (e->kind==CLOUD_AUDIO_DELTA || e->kind==CLOUD_AUDIO_DONE)) return;
#endif
    uint32_t at=cloud_id_trace[0],base=1u+(at%2048u)*8u,n=0;
    while(n<UI_MAX_TEXT && e->text[n]) ++n;
    if(e->kind==CLOUD_AUDIO_DELTA) n=e->audio_bytes;
    cloud_id_trace[base]=(uint32_t)e->kind|(e->cumulative<<8)|(e->completed<<9)|(e->output_final<<10)|(parsed<<16);
    cloud_id_trace[base+1]=id_hash(e->item_id);cloud_id_trace[base+2]=id_hash(e->previous_id);
    if(parsed && !e->kind) {
        /* Identify a rejected event type without storing any payload text. */
        for(uint32_t j=0;j+6u<bytes;++j) if(!memcmp(json+j,"\"type\"",6)) {
            j+=6u;while(j<bytes && (json[j]==' ' || json[j]==':' || json[j]=='\t')) ++j;
            if(j<bytes && json[j]=='\"') {
                uint32_t h=2166136261u;for(++j;j<bytes && json[j]!='\"';++j) {h^=(uint8_t)json[j];h*=16777619u;}
                cloud_id_trace[base+1]=h;
            }
            break;
        }
    }
    cloud_id_trace[base+3]=id_hash(e->response_id);cloud_id_trace[base+4]=n;
    cloud_id_trace[base+5]=cloud.live.phase;cloud_id_trace[base+6]=cloud.live.epoch;
    cloud_id_trace[base+7]=health_now;cloud_id_trace[0]=at+1u;
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)&cloud_id_trace[base],32);
    cache_clean_range((const void *)cloud_id_trace,4);
#endif
}
#endif
void cloud_frame_debug(uint32_t *out);
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t first_wire_fault[8];
static void health_clean(void) {
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)cloud_health,sizeof(cloud_health));
    cache_clean_range((const void *)cloud_error_code,sizeof(cloud_error_code));
    cache_clean_range((const void *)first_wire_fault,sizeof(first_wire_fault));
#endif
}
#ifdef Z7_BILINGUAL
/* Queue state only; no identifiers or recognised text in diagnostics. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t caption_queue_diag[36];
#endif
static void session_diag_clean(void) {
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)session_diag,sizeof(session_diag));
#endif
}
static void cloud_fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static int cloud_same(const char *a,const char *b) {while(*a && *a==*b) {++a;++b;}return *a==*b;}
static void cloud_id_copy(char *to,const char *from) {do {*to++=*from;} while(*from++);}
static void cloud_fail(uint32_t error) {
    if(!cloud.live.error) {
        cloud.live.error=error;++cloud_health[6];
        if(!cloud_health[7]) {
            uint32_t debug[8];cloud_frame_debug(debug);
            for(uint32_t i=0;i<8u;++i) first_wire_fault[i]=debug[i];
            cloud_health[7]=error;cloud_health[9]=health_now;
        }
        cloud_health[8]=error;cloud_health[10]=health_now;
        cloud_health[12]=network_info.phase;cloud_health[13]=network_info.error;
        cloud_health[14]=cloud_wire_status()->error;cloud_health[15]=cloud_wire_status()->close_code;
        health_clean();
    }
    cloud.live.phase=6;
    /* A close/protocol failure otherwise leaves TLS "online" with an idle
       client forever. Explicit service errors retain their code for diagnosis. */
    if(error!=3u) network_stream_failed();
}
static struct cloud_pair *cloud_pair_find(const char *id,int target,int create) {
    struct cloud_pair *empty=0,*oldest=0;
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        struct cloud_pair *p=cloud.pairs+i;
        if(!p->order) {empty=p;continue;}
        if(cloud_same(target?p->target_id:p->source_id,id)) return p;
        if(!oldest || p->order<oldest->order) oldest=p;
    }
    if(!create) return 0;
    struct cloud_pair *slot=empty;
    if(!slot) {
        ++cloud.live.ignored;
#ifdef Z7_BILINGUAL
        ++session_diag[5];session_diag_clean();
        cloud_client_stop();cloud_fail(9);return 0; /* Never overwrite an untranslated sentence. */
#else
        slot=oldest;
#endif
    }
    memset(slot,0,sizeof(*slot));
    slot->order=++cloud.order;cloud_id_copy(target?slot->target_id:slot->source_id,id);return slot;
}
#ifdef Z7_BILINGUAL
static void cloud_preview(void) {
    if(cloud.model->epoch!=cloud.live.epoch || cloud.model->session!=UI_RUNNING) return;
    struct cloud_pair *pending=0,*latest=0;
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        struct cloud_pair *candidate=cloud.pairs+i;
        if(!candidate->order) continue;
        if(!latest || candidate->order>latest->order) latest=candidate;
        if(!candidate->target_ready && (!pending || candidate->order<pending->order)) pending=candidate;
    }
    /* Text can advance once translated, without waiting for response.done
       after TTS. The oldest untranslated source still takes precedence. */
    struct cloud_pair *p=pending?pending:latest;
    caption_queue_diag[0]=0x31515043u;++caption_queue_diag[1];
    caption_queue_diag[2]=p?p->order:0;caption_queue_diag[3]=0;
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        const struct cloud_pair *q=cloud.pairs+i;
        caption_queue_diag[4u+2u*i]=q->order;
        caption_queue_diag[5u+2u*i]=q->source_ready|(q->target_ready<<1)|(q->complete<<2)|
            (q->source[0]?8u:0u)|(q->translated[0]?16u:0u);
        if(q->order) ++caption_queue_diag[3];
    }
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)caption_queue_diag,sizeof(caption_queue_diag));
#endif
    if(!p) return;
    if(p->source[0]) for(uint32_t i=0;i<UI_MAX_TEXT;++i)
        cloud.model->source[i]=(p->source[i] && !net_glyph_available(p->source[i]))?' ':p->source[i];
    struct cloud_pair *english=p->translated[0]?p:0;
    if(!english) for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        struct cloud_pair *candidate=cloud.pairs+i;
        if(candidate->order && candidate->order<p->order && candidate->target_ready &&
           (!english || candidate->order>english->order)) english=candidate;
    }
    if(english) memcpy(cloud.model->translated,english->translated,sizeof(english->translated));
    cloud.model->stage=UI_TRANSLATING;++cloud.model->change_sequence;
}
#endif
static void cloud_remember(const char *source,const char *target,const char *response) {
    struct cloud_recent *recent=cloud.recent+cloud.recent_cursor++%CLOUD_PAIRS;
    cloud_id_copy(recent->source,source);cloud_id_copy(recent->target,target);cloud_id_copy(recent->response,response);
}
static void cloud_commit(void) {
    for(;;) {
        struct cloud_pair *p=0;
        for(uint32_t i=0;i<CLOUD_PAIRS;++i) if(cloud.pairs[i].order && (!p || cloud.pairs[i].order<p->order)) p=cloud.pairs+i;
        if(!p || !p->source_ready || !p->target_ready || !p->complete) {
#ifdef Z7_BILINGUAL
            cloud_preview();
#endif
            return;
        }
        if(cloud.model->epoch!=cloud.live.epoch || cloud.model->session!=UI_RUNNING) {++cloud.live.ignored;memset(p,0,sizeof(*p));continue;}
        uint32_t display_text[UI_MAX_TEXT];
        for(uint32_t i=0;i<UI_MAX_TEXT;++i) {
            // The prototype font is a bounded demo subset. A live translation
            // may contain characters outside it; showing those as spaces keeps
            // the subtitle (and the T1 readout) usable instead of failing the
            // whole cloud session on one missing glyph.
            display_text[i]=(p->source[i] && !net_glyph_available(p->source[i]))?' ':p->source[i];
        }
        uint32_t segment=++cloud.next_segment;
        struct ui_event event={UI_ASR_FINAL,cloud.live.epoch,segment,1,0,display_text};
        if(!ui_apply_event(cloud.model,&event)) {++cloud.live.ignored;memset(p,0,sizeof(*p));continue;}
        event.type=UI_TRANSLATING_EVENT;event.source_revision=1;event.text=0;
        if(!ui_apply_event(cloud.model,&event)) {++cloud.live.ignored;memset(p,0,sizeof(*p));continue;}
        for(uint32_t i=0;i<UI_MAX_TEXT;++i)
            display_text[i]=(p->translated[i] && !net_glyph_available(p->translated[i]))?' ':p->translated[i];
        event.type=UI_TRANSLATION_FINAL;event.source_revision=1;event.text=display_text;
        if(!ui_apply_event(cloud.model,&event)) {++cloud.live.ignored;memset(p,0,sizeof(*p));continue;}
        ++cloud_caption.sequence;cloud_fence();
        for(uint32_t i=0;i<UI_MAX_TEXT;++i) {cloud_caption.source[i]=p->source[i];cloud_caption.translated[i]=p->translated[i];}
        cloud_fence();++cloud_caption.sequence;cloud_fence();
        text_log_record(p->source);
#ifdef Z7_MMU_CACHE
        /* cloud_caption lives in cached DDR but is read back over JTAG. */
        cache_clean_range((const void *)&cloud_caption,(unsigned)sizeof(cloud_caption));
#endif
        cloud_remember(p->source_id,p->target_id,p->response_id);
        ++cloud.live.committed;memset(p,0,sizeof(*p));
    }
}
int cloud_live_message(const char *json,uint32_t size,void *user) {
    (void)user;
    struct cloud_event *e=&cloud.event;
    enum cloud_parse_code parsed=cloud_event_parse(json,size,e);
#ifdef Z7_EVENT_ID_TRACE
    id_trace(e,(uint32_t)parsed,json,size);
#endif
    /* One unparsable or oversized event (for example a server error whose
       message is not ASCII) must not take down an otherwise healthy session. */
    if(parsed) {++cloud.live.ignored;++cloud_health[4];cloud_health[5]=(uint32_t)parsed;health_clean();return 0;}
    ++cloud_health[2];cloud_health[3]=(uint32_t)e->kind;
    ++cloud.live.messages;
    if(cloud.live.phase!=1) return 0;
    if(e->kind==CLOUD_AUDIO_DELTA) {(void)speech_playback_audio(e->audio,e->audio_bytes);return 0;}
    if(e->kind==CLOUD_AUDIO_DONE) {speech_playback_done();return 0;}
    if(e->kind==CLOUD_IGNORE) return 0;
    if(e->kind==CLOUD_SESSION_CREATED) {cloud_client_session_created();return 0;}
    if(e->kind==CLOUD_SESSION_UPDATED) {
        ++session_diag[1];session_diag[2]=e->interrupt_known;session_diag[3]=e->interrupt_response;session_diag_clean();
        cloud_client_session_updated();return 0;
    }
    if(e->kind==CLOUD_SPEECH_STARTED) {
        cloud_client_speech_started(e->audio_start_ms);
#ifdef Z7_BILINGUAL
        if(cloud.model->epoch==cloud.live.epoch && cloud.model->session==UI_RUNNING) {
            /* Keep the previous subtitle readable until the next English text
               arrives; a new speech segment only changes the status. */
            cloud.model->stage=UI_RECOGNIZING;
            ++cloud.model->change_sequence;
        }
#endif
        return 0;
    }
    if(e->kind==CLOUD_SPEECH_STOPPED) {cloud_client_speech_stopped();return 0;}
    /* Reject committed IDs before a preview can allocate an old sentence. */
    for(uint32_t i=0;i<CLOUD_PAIRS;++i) {
        const struct cloud_recent *recent=cloud.recent+i;
        if((e->response_id[0] && cloud_same(e->response_id,recent->response)) ||
            (e->item_id[0] && (cloud_same(e->item_id,recent->source) || cloud_same(e->item_id,recent->target))) ||
            (e->kind==CLOUD_ITEM_LINK && e->previous_id[0] && cloud_same(e->previous_id,recent->source))) return 0;
    }
    /* ASR previews are Chinese source; bilingual mode keeps them below the
       independent English translation, including before either final arrives. */
    if(e->kind==CLOUD_ASR_PARTIAL) {
#ifdef Z7_BILINGUAL
        if(!e->text[0]) return 0; /* Empty VAD previews must not reserve queue heads. */
#endif
        struct cloud_pair *p=cloud_pair_find(e->item_id,0,1);
        if(p && p->source_ready) return 0;
        if(p) {
            for(uint32_t i=0;i<UI_MAX_TEXT;++i) p->source[i]=e->text[i];
            p->source[UI_MAX_TEXT-1u]=0;
            if(cloud.model->epoch==cloud.live.epoch && cloud.model->session==UI_RUNNING) {
#ifdef Z7_ASR_SUBTITLE
                for(uint32_t i=0;i<UI_MAX_TEXT;++i) cloud.model->translated[i]=e->text[i];
#elif defined(Z7_BILINGUAL)
                cloud_preview();
#else
                for(uint32_t i=0;i<UI_MAX_TEXT;++i)
                    cloud.model->source[i]=(e->text[i] && !net_glyph_available(e->text[i]))?' ':e->text[i];
#endif
                cloud.model->stage=UI_TRANSLATING;
                ++cloud.model->change_sequence;
            }
        }
#ifdef Z7_ASR_SUBTITLE
        cloud_client_translation(0);
#endif
        return 0;
    }
#ifdef Z7_ASR_SUBTITLE
    if(e->kind==CLOUD_TRANSLATION_DELTA || e->kind==CLOUD_TRANSLATION) return 0;
#endif
    if(e->kind==CLOUD_TRANSLATION_DELTA) {
        cloud_client_translation(0);
        /* Show the subtitle while it is being generated: append each delta to
           the pair's translation buffer and mirror it into the live caption.
           The final response.text.done replaces it with the complete text. */
        struct cloud_pair *p=cloud_pair_find(e->item_id,1,0);
        if(!p) {
            for(uint32_t i=0;i<CLOUD_LINKS;++i) if(e->item_id[0] && cloud_same(cloud.links[i].item,e->item_id)) {
                p=cloud_pair_find(cloud.links[i].previous,0,1);
                if(p) cloud_id_copy(p->target_id,e->item_id);
                cloud.links[i].item[0]=0;break;
            }
        }
        if(p) {
            if(p->target_ready) return 0;
            if(e->response_id[0]) cloud_id_copy(p->response_id,e->response_id);
            if(e->cumulative) {
                /* qwen3.5 response.text.text is the full text seen so far. */
                for(uint32_t i=0;i<UI_MAX_TEXT;++i) p->translated[i]=e->text[i];
                p->translated[UI_MAX_TEXT-1u]=0;
            } else {
                uint32_t n=0;while(n<UI_MAX_TEXT-1u && p->translated[n]) ++n;
                for(uint32_t i=0;i<UI_MAX_TEXT-1u && n<UI_MAX_TEXT-1u && e->text[i];++i) p->translated[n++]=e->text[i];
                p->translated[n]=0;
            }
            if(cloud.model->epoch==cloud.live.epoch && cloud.model->session==UI_RUNNING) {
#ifdef Z7_BILINGUAL
                cloud_preview();
#else
                for(uint32_t i=0;i<UI_MAX_TEXT;++i) cloud.model->translated[i]=p->translated[i];
                cloud.model->stage=UI_TRANSLATING;
                ++cloud.model->change_sequence;
#endif
            }
        }
        return 0;
    }
    if(e->kind==CLOUD_ERROR) {
        for(uint32_t i=0;i<CLOUD_ID_BYTES;++i) cloud_error_code[i]=e->error_code[i];
        cloud_client_stop();cloud_fail(3);return 1;
    }
    if(e->kind==CLOUD_ITEM_LINK) {
        uint32_t slot=0;
        for(uint32_t i=0;i<CLOUD_LINKS;++i) if(!cloud.links[i].item[0] || cloud_same(cloud.links[i].item,e->item_id)) {slot=i;break;}
        cloud_id_copy(cloud.links[slot].item,e->item_id);cloud_id_copy(cloud.links[slot].previous,e->previous_id);return 0;
    }
    if(e->kind==CLOUD_FINISHED) {
        cloud_commit();
        memset(cloud.pairs,0,sizeof(cloud.pairs));
        cloud.live.phase=2;return 0;
    }
    struct cloud_pair *p=0;
    if(e->kind==CLOUD_ASR_FINAL) {
        cloud_client_asr_final();
#ifdef Z7_ASR_SUBTITLE
        cloud_client_translation(0);
#endif
        ++cloud.live.asr_final;
#ifdef Z7_BILINGUAL
        if(!e->text[0]) {
            p=cloud_pair_find(e->item_id,0,0);
            if(p && p->source_ready) return 0; /* Never replace a real final. */
            ++session_diag[6];session_diag_clean();
            /* Explicitly terminal, including translation-before-ASR ordering.
               Preserve no speculative text as a fabricated source/history. */
            cloud_remember(e->item_id,p?p->target_id:"",p?p->response_id:"");
            for(uint32_t i=0;i<CLOUD_LINKS;++i)
                if(cloud_same(cloud.links[i].previous,e->item_id) || cloud_same(cloud.links[i].item,e->item_id)) cloud.links[i].item[0]=0;
            if(p) memset(p,0,sizeof(*p));
            cloud_commit();return 0;
        }
#endif
        p=cloud_pair_find(e->item_id,0,1);
        cloud_client_capture_freeze();
        if(p) {
            memcpy(p->source,e->text,sizeof(p->source));p->source_ready=1;
#ifndef Z7_ASR_SUBTITLE
            if(cloud.model->epoch==cloud.live.epoch && cloud.model->session==UI_RUNNING) {
#ifdef Z7_BILINGUAL
                cloud_preview();
#else
                for(uint32_t i=0;i<UI_MAX_TEXT;++i)
                    cloud.model->source[i]=(e->text[i] && !net_glyph_available(e->text[i]))?' ':e->text[i];
                ++cloud.model->change_sequence;
#endif
            }
#endif
#ifdef Z7_ASR_SUBTITLE
            /* Same-language mode finalises from the transcript alone. */
            memcpy(p->translated,p->source,sizeof(p->translated));
            p->target_ready=1;p->complete=1;
#endif
        }
        for(uint32_t i=0;i<CLOUD_LINKS;++i) if(cloud_same(cloud.links[i].item,e->item_id)) cloud.links[i].item[0]=0;
    } else if(e->kind==CLOUD_TRANSLATION) {
        ++cloud.live.translations;cloud_client_translation(0);p=cloud_pair_find(e->item_id,1,0);
        if(!p) {
            for(uint32_t i=0;i<CLOUD_LINKS;++i) if(cloud_same(cloud.links[i].item,e->item_id)) {
                p=cloud_pair_find(cloud.links[i].previous,0,1);
                if(p) cloud_id_copy(p->target_id,e->item_id);
                cloud.links[i].item[0]=0;break;
            }
        }
        if(p) {cloud_id_copy(p->response_id,e->response_id);memcpy(p->translated,e->text,sizeof(p->translated));p->target_ready=1;}
    } else if(e->kind==CLOUD_RESPONSE_DONE) {
        for(uint32_t i=0;i<CLOUD_PAIRS;++i) if(cloud.pairs[i].order && cloud_same(cloud.pairs[i].response_id,e->response_id)) {p=cloud.pairs+i;break;}
        if(!e->completed) {
            /* The service cancels an in-flight response when the speaker starts
               again; that is normal, not a session failure. Drop the partial pair
               and keep listening. */
            ++cloud.live.ignored;
            ++session_diag[4];session_diag_clean();
            if(p) memset(p,0,sizeof(*p));
            cloud_client_response_done();
            return 0;
        }
        if(e->output_final) {
            if(!p) p=cloud_pair_find(e->item_id,1,0);
            if(!p) for(uint32_t i=0;i<CLOUD_LINKS;++i) if(cloud_same(cloud.links[i].item,e->item_id)) {
                p=cloud_pair_find(cloud.links[i].previous,0,1);
                if(p) cloud_id_copy(p->target_id,e->item_id);
                cloud.links[i].item[0]=0;break;
            }
            if(p && cloud_same(p->target_id,e->item_id) && !p->target_ready) {
                cloud_id_copy(p->response_id,e->response_id);
                if(e->text[0]) {
                    memcpy(p->translated,e->text,sizeof(p->translated));p->target_ready=1;
                    ++cloud.live.translations;++session_diag[8];session_diag_clean();cloud_client_translation(0);
                } else {
                    ++session_diag[7];session_diag_clean();
                    cloud_remember(p->source_id,p->target_id,p->response_id);
                    memset(p,0,sizeof(*p));cloud_client_response_done();cloud_commit();return 0;
                }
            }
        }
        if(p) p->complete=1;
        cloud_client_response_done();
    }
    if(!p) {++cloud.live.ignored;return 0;}
    cloud_commit();return cloud.live.error!=0;
}
static void cloud_receive(uint8_t byte) {++cloud.live.received;cloud_wire_byte(byte);}
void cloud_live_init(struct ui_state *model) {
    capture_enabled=1;
#ifdef Z7_EVENT_ID_TRACE
    cloud_id_trace[0]=0;
#endif
    memset(&cloud,0,sizeof(cloud));cloud.model=model;cloud.live.magic=0x31564c43u;
    memset((void *)cloud_health,0,sizeof(cloud_health));cloud_health[0]=0x31484c43u;
    memset((void *)cloud_error_code,0,sizeof(cloud_error_code));health_now=0;health_clean();
    memset((void *)first_wire_fault,0,sizeof(first_wire_fault));health_clean();
#ifdef Z7_BILINGUAL
    memset((void *)caption_queue_diag,0,sizeof(caption_queue_diag));
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)caption_queue_diag,sizeof(caption_queue_diag));
#endif
#endif
    memset((void *)session_diag,0,sizeof(session_diag));session_diag[0]=0x31534553u;session_diag_clean();
    /* .cloud_runtime is NOLOAD, so the log header must be set explicitly:
       an uninitialised count sends records to arbitrary slots. */
    text_log.magic=CLOUD_TEXT_LOG_MAGIC;text_log.count=0;
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)&text_log,(unsigned)sizeof(text_log));
#endif
    memset((void *)&cloud_report,0,sizeof(cloud_report));memset((void *)&cloud_caption,0,sizeof(cloud_caption));
    cloud.report_ms=(uint32_t)-200;
}
int cloud_live_capture_enabled(void) {return capture_enabled;}
enum ui_language cloud_live_language(void) {return cloud.model->language;}
void cloud_live_connection(int ready) {
    if(!capture_enabled) return;
    struct ui_state *model=cloud.model;
    enum ui_stage next;
    if(network_info.phase==6u || (network_info.phase==4u && cloud.live.phase==6u)) {
        if(model->session==UI_RUNNING) (void)ui_control(model,UI_PAUSE);
        next=UI_OFFLINE;
    } else if(ready && network_info.phase==4u) {
        if(model->stage!=UI_CONNECTING && model->stage!=UI_OFFLINE) return;
        next=UI_LISTENING;
    } else {
        if(model->session==UI_IDLE) {
            if(network_info.phase==0u) return;
            (void)ui_control(model,UI_START);
        }
        next=UI_CONNECTING;
    }
    if(model->stage!=next) {model->stage=next;++model->change_sequence;}
}
void cloud_live_control(enum ui_command command) {
    int language_change=command==UI_SELECT_EN_ZH || command==UI_SELECT_ZH_EN;
    if(command==UI_PAUSE || command==UI_CLEAR || language_change) {
        capture_enabled=0;cloud.live.phase=0;cloud_client_stop();speech_playback_stop();
#ifdef Z7_ARM_TLS
        network_cloud_run(0);
#endif
    }
    if(command==UI_START || command==UI_RESUME ||
       (language_change && cloud.model->session==UI_RUNNING)) {
        capture_enabled=1;
        cloud.model->stage=UI_CONNECTING;++cloud.model->change_sequence;
#ifdef Z7_ARM_TLS
        /* Explicit user resume also re-arms a fault-paused session whose
           automatic retry budget was exhausted while run intent stayed set. */
        network_cloud_run(0);network_cloud_run(1);
#endif
    }
}
/* The board's cloud client owns the link and calls this once when the TLS
   session is ready, so the model/session state normally set by the host's
   cloud_report.request=1 path is entered here instead. */
void cloud_live_begin(uint32_t now) {
    health_now=now;++cloud_health[1];health_clean();
    struct ui_state *model=cloud.model;
    uint32_t retained_source[UI_MAX_TEXT],retained_translation[UI_MAX_TEXT];
    int reconnect=cloud.live.ack==1u && (model->session==UI_RUNNING || model->session==UI_PAUSED);
    if(reconnect) {
        memcpy(retained_source,model->source,sizeof(retained_source));
        memcpy(retained_translation,model->translated,sizeof(retained_translation));
    }
    memset(&cloud,0,sizeof(cloud));cloud.model=model;
    cloud.live.magic=0x31564c43u;cloud.live.phase=1;cloud.live.ack=1;
    cloud.report_ms=now-200u;
    if(reconnect) {
        if(model->session==UI_RUNNING) (void)ui_control(model,UI_PAUSE);
        (void)ui_control(model,UI_RESUME);
        memcpy(model->source,retained_source,sizeof(retained_source));
        memcpy(model->translated,retained_translation,sizeof(retained_translation));
    } else
    {(void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);}
#ifndef Z7_HISTORY_UI
    /* Legacy profiles pin the live Chinese font. The settings product scales
       the 28 px GB2312 subset and preserves the user's font/source choices. */
    /* Live Chinese uses the 28 px font, the only size carrying the GB2312
       level-1 extension, so real translations are not reduced to spaces. */
    if(!reconnect) {
        while(model->font_index>0) (void)ui_control(model,UI_FONT_MINUS);
#ifdef Z7_BILINGUAL
        /* Translation above, source below. */
        if(!model->show_source) (void)ui_control(model,UI_TOGGLE_SOURCE);
#else
        if(model->show_source) (void)ui_control(model,UI_TOGGLE_SOURCE);
#endif
    }
#endif
    cloud.live.epoch=model->epoch;
#ifdef Z7_ARM_TLS
    model->stage=UI_CONNECTING;
#endif
    if(!reconnect) memset((void *)&cloud_caption,0,sizeof(cloud_caption));
    cloud_wire_init(cloud_live_message,0);
}
void cloud_live_tick(uint32_t now) {
    health_now=now;
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
    cloud_health[11]=now;health_clean();
    volatile uint32_t *out=(volatile uint32_t *)&cloud_report;const uint32_t *in=(const uint32_t *)&cloud.live;
    for(uint32_t i=0;i<16;++i) if(i!=1 && i!=2) out[i]=in[i];
    cloud_fence();++cloud_report.sequence;cloud_fence();
}
int cloud_live_owns_ui(void) {return cloud.live.phase!=0;}
