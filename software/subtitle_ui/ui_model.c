#include "ui_model.h"
#include "ui_layout.h"
#include "../network_ui/language_profile.h"

#define UI_U32_MAX 0xffffffffu

static void increment(uint32_t *value)
{
    if (*value != UI_U32_MAX) ++*value;
}

static void zero_bytes(void *destination, uint32_t bytes)
{
    unsigned char *p = (unsigned char *)destination;
    uint32_t i;
    for (i = 0; i < bytes; ++i) p[i] = 0;
}

static void copy_text(uint32_t *destination, const uint32_t *source)
{
    uint32_t i;
    for (i = 0; i < UI_MAX_TEXT; ++i) {
        destination[i] = source[i];
        if (source[i] == 0) {
            ++i;
            break;
        }
    }
    for (; i < UI_MAX_TEXT; ++i) destination[i] = 0;
}

static int text_valid(const uint32_t *text)
{
    uint32_t i;
    if (text == 0 || text[0] == 0) return 0;
    for (i = 0; i < UI_MAX_TEXT; ++i) {
        uint32_t codepoint = text[i];
        if (codepoint == 0) return 1;
        if (codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return 0;
    }
    return 0;
}

static int same_text(const uint32_t *a, const uint32_t *b)
{
    uint32_t i;
    for (i = 0; i < UI_MAX_TEXT; ++i) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
    }
    return 0;
}

static void copy_history(struct ui_history_entry *destination,
                         const struct ui_history_entry *source)
{
#ifdef Z7_HISTORY_UI
    destination->language=source->language;
    destination->pixel_height=source->pixel_height;
#endif
    destination->epoch = source->epoch;
    destination->segment_id = source->segment_id;
    destination->revision = source->revision;
    destination->source_revision = source->source_revision;
    copy_text(destination->source, source->source);
    copy_text(destination->translated, source->translated);
}

static void cancel_pending(struct ui_state *state)
{
    state->processing_segment = 0;
    state->source_revision = 0;
    state->source_final = 0;
    state->translation_revision = 0;
    copy_text(state->source, state->last_final_source);
    state->stage = state->translated[0] != 0 ? UI_FINAL : UI_LISTENING;
}

static int reject(struct ui_state *state)
{
    increment(&state->rejected_events);
    return 0;
}

#ifdef Z7_HISTORY_UI
void ui_wifi_identity(struct ui_state *state,uint32_t connected,const uint8_t name[33]) {
    uint32_t text[33]={0},n=0,i=0;
    if(connected)while(i<32u && name[i]) {
        uint32_t cp=name[i++],need=0,min=0;
        if(cp>=0xc2u && cp<=0xdfu){cp&=31u;need=1;min=0x80u;}
        else if(cp>=0xe0u && cp<=0xefu){cp&=15u;need=2;min=0x800u;}
        else if(cp>=0xf0u && cp<=0xf4u){cp&=7u;need=3;min=0x10000u;}
        else if(cp>=128u){text[n++]='?';continue;}
        uint32_t valid=1;
        for(uint32_t j=0;j<need;++j){if(i>=32u || (name[i]&0xc0u)!=0x80u){valid=0;break;}cp=(cp<<6)|(name[i++]&63u);}
        if(!valid || cp<min || cp>0x10ffffu || (cp>=0xd800u && cp<=0xdfffu) || cp<32u || cp==127u)cp='?';
        text[n++]=cp;
    }
    uint32_t changed=state->wifi_connected!=(connected!=0);
    for(uint32_t j=0;j<33u;++j){if(text[j]!=state->wifi_name[j])changed=1;state->wifi_name[j]=text[j];}
    state->wifi_connected=connected!=0;if(changed)++state->change_sequence;
}
#endif
void ui_init(struct ui_state *state) {
    if (state == 0) return;
    zero_bytes(state, (uint32_t)sizeof(*state));
    state->epoch = 1;
    state->session = UI_IDLE;
    state->stage = UI_LISTENING;
    state->font_index = 1;
    state->show_source = 1;
#ifdef Z7_HISTORY_UI
    state->history_follow=1;
    state->tts_enabled=1;state->output_volume=70;state->rate_index=1;
#endif
#ifdef Z7_TRANSLATE_EN
    state->language = UI_ZH_EN;
#endif
}

#ifdef Z7_HISTORY_UI
int ui_history_scroll(struct ui_state *state,int delta) {
    if(!state->history_page || state->history_confirm) return 0;
    int next=(int)state->history_scroll+delta;
    if(next<0) next=0;
    if((uint32_t)next>state->history_limit) next=(int)state->history_limit;
    if((uint32_t)next==state->history_scroll) return 0;
    state->history_scroll=(uint32_t)next;
    state->history_follow=state->history_scroll==state->history_limit;
    increment(&state->change_sequence);return 1;
}
#endif
#ifdef Z7_HISTORY_UI
uint32_t ui_view_context(const struct ui_state *state)
{
#ifdef Z7_LOCAL_AI
    return (state->epoch<<6)|(state->settings_page?1u:0u)|(state->history_page?2u:0u)|
           (state->language_menu?4u:0u)|(state->history_confirm?8u:0u)|(state->ai_page?16u:0u)|
           (state->conversation?32u:0u);
#else
    return (state->epoch<<5)|(state->settings_page?1u:0u)|(state->history_page?2u:0u)|
           (state->language_menu?4u:0u)|(state->history_confirm?8u:0u)|(state->conversation?16u:0u);
#endif
}
#endif
int ui_control(struct ui_state *state, enum ui_command command)
{
    uint32_t next_epoch;
    uint32_t font_index;
    uint32_t show_source;
    uint32_t next_sequence;
    enum ui_language language;
    if (state == 0) return 0;
    switch (command) {
    case UI_START:
        if (state->session != UI_IDLE) return 0;
        state->session = UI_RUNNING;
        state->stage = UI_LISTENING;
        break;
    case UI_PAUSE:
        if (state->session != UI_RUNNING) return 0;
        cancel_pending(state);
        state->session = UI_PAUSED;
        break;
    case UI_RESUME:
        if (state->session != UI_PAUSED || state->epoch == UI_U32_MAX) return 0;
        ++state->epoch;
        state->last_final_segment = 0;
        cancel_pending(state);
        state->session = UI_RUNNING;
        state->stage = UI_LISTENING;
        break;
    case UI_CLEAR:
        if (state->epoch == UI_U32_MAX) return 0;
#ifdef Z7_HISTORY_UI
        uint32_t enabled=state->tts_enabled,volume=state->output_volume,muted=state->output_muted,rate=state->rate_index;
        uint32_t settings_page=state->settings_page;
        uint32_t wifi_connected=state->wifi_connected,wifi_name[33];
        for(uint32_t i=0;i<33u;++i)wifi_name[i]=state->wifi_name[i];
        uint32_t debug=state->show_debug,conversation=state->conversation,terms=state->glossary_terms;
#ifdef Z7_LOCAL_AI
        uint32_t ai_page=state->ai_page,mode=state->model_mode,mask=state->speaker_mask;
#ifdef Z7_TINYQWEN
        uint32_t load=state->model_load,percent=state->model_percent,notice=state->model_notice;
#endif
#endif
#endif
        next_epoch = state->epoch + 1;
        font_index = state->font_index;
        show_source = state->show_source;
        language = state->language;
        next_sequence = state->change_sequence;
        ui_init(state);
        state->epoch = next_epoch;
        state->font_index = font_index;
        state->show_source = show_source;
        state->language = language;
#ifdef Z7_HISTORY_UI
        state->tts_enabled=enabled;state->output_volume=volume;state->output_muted=muted;state->rate_index=rate;
        state->settings_page=settings_page;
        state->wifi_connected=wifi_connected;
        for(uint32_t i=0;i<33u;++i)state->wifi_name[i]=wifi_name[i];
        state->show_debug=debug;state->conversation=conversation;state->glossary_terms=terms;
#ifdef Z7_LOCAL_AI
        state->ai_page=ai_page;state->model_mode=mode;state->speaker_mask=mask;
#ifdef Z7_TINYQWEN
        state->model_load=load;state->model_percent=percent;state->model_notice=notice;
#endif
#endif
#endif
        state->change_sequence = next_sequence;
        break;
    case UI_FONT_MINUS:
        if (state->font_index == 0) return 0;
        --state->font_index;
        break;
    case UI_FONT_PLUS:
        if (state->font_index >= 2) return 0;
        ++state->font_index;
        break;
    case UI_TOGGLE_SOURCE:
        state->show_source = state->show_source == 0 ? 1u : 0u;
        break;
#ifdef Z7_LANGUAGE_SWITCH
    case UI_LANGUAGE_MENU:
        state->language_menu = !state->language_menu;
        break;
    case UI_DISMISS_LANGUAGE:
        if (!state->language_menu) return 0;
        state->language_menu = 0;
        break;
    case UI_SELECT_EN_ZH:
    case UI_SELECT_ZH_EN: {
        language = command == UI_SELECT_EN_ZH ? UI_EN_ZH : UI_ZH_EN;
        if (state->language == language || state->epoch == UI_U32_MAX) return 0;
        enum ui_session session = state->session;
#ifdef Z7_HISTORY_UI
        /* Start a fresh direction while keeping actual completed history. */
        ++state->epoch;state->language_menu=0;
        zero_bytes(state->source,sizeof(state->source));
        zero_bytes(state->translated,sizeof(state->translated));
        zero_bytes(state->last_final_source,sizeof(state->last_final_source));
        state->processing_segment=state->source_revision=state->source_final=0;
        state->translation_revision=state->last_final_segment=0;
        increment(&state->change_sequence);
#else
        if (!ui_control(state, UI_CLEAR)) return 0;
#endif
        state->language = language;
        state->session = session == UI_RUNNING ? UI_RUNNING :
            (session == UI_PAUSED ? UI_PAUSED : UI_IDLE);
        state->stage = state->session == UI_RUNNING ? UI_CONNECTING : UI_LISTENING;
        return 1;
    }
#endif
#ifdef Z7_HISTORY_UI
#ifdef Z7_LOCAL_AI
    case UI_OPEN_AI:
        state->ai_page=1;state->settings_page=state->history_page=state->language_menu=state->conversation=0;
        break;
    case UI_CLOSE_AI:
        if(!state->ai_page)return 0;
        state->ai_page=0;state->settings_page=1;
        break;
    case UI_AI_CLOUD:
    case UI_AI_LOCAL: {
        uint32_t mode=command==UI_AI_LOCAL;
        if(mode==state->model_mode || state->epoch==UI_U32_MAX)return 0;
        ++state->epoch;state->model_mode=mode;
        state->processing_segment=state->source_revision=state->source_final=0;
        state->translation_revision=state->last_final_segment=0;
        state->source[0]=state->translated[0]=state->last_final_source[0]=0;
        state->stage=UI_LISTENING;state->local_result=0;
        break;
    }
    case UI_AI_ENROLL_1: case UI_AI_ENROLL_2: case UI_AI_ENROLL_3: case UI_AI_ENROLL_4:
#ifndef Z7_SPEAKER_CNN
        if(state->session!=UI_RUNNING)return 0;
#endif
        state->speaker_enroll=(uint32_t)command-UI_AI_ENROLL_1+1u;
        break;
    case UI_AI_CANCEL_ENROLL:
        if(!state->speaker_enroll)return 0;
        state->speaker_enroll=0;
        break;
    case UI_AI_ERASE:
        if(!state->speaker_mask && !state->speaker_enroll)return 0;
        break;
#endif
    case UI_OPEN_HISTORY:
        state->settings_page=state->conversation=0;
        state->history_page=1;state->history_confirm=0;state->history_follow=1;state->language_menu=0;
        break;
    case UI_CLOSE_HISTORY:
        if(!state->history_page) return 0;
        state->history_page=state->history_confirm=0;
        break;
    case UI_HISTORY_CLEAR:
        if(!state->history_page || !state->history_count || state->history_confirm) return 0;
        state->history_confirm=1;
        break;
    case UI_HISTORY_CANCEL_CLEAR:
        if(!state->history_confirm) return 0;
        state->history_confirm=0;
        break;
    case UI_HISTORY_CONFIRM_CLEAR:
        if(!state->history_confirm) return 0;
        zero_bytes(state->history,sizeof(state->history));
        state->history_count=state->history_scroll=state->history_limit=0;
        state->history_confirm=state->history_discarded=0;state->history_follow=1;
        break;
    case UI_HISTORY_OLDER:
        return ui_history_scroll(state,-(UI_HISTORY_BODY_HEIGHT-60));
    case UI_HISTORY_NEWER:
        return ui_history_scroll(state,UI_HISTORY_BODY_HEIGHT-60);
    case UI_OPEN_SETTINGS:
        state->settings_page=1;state->history_page=state->history_confirm=state->language_menu=state->conversation=0;
        break;
    case UI_CLOSE_SETTINGS:
        if(!state->settings_page) return 0;
        state->settings_page=0;
        break;
    case UI_TOGGLE_TTS:
        state->tts_enabled=!state->tts_enabled;
        break;
    case UI_VOLUME_MINUS:
        if(!state->output_volume) return 0;
        state->output_volume=state->output_volume>=10u?state->output_volume-10u:0;
        break;
    case UI_VOLUME_PLUS:
        if(state->output_volume>=100u) return 0;
        state->output_volume=state->output_volume<=90u?state->output_volume+10u:100u;
        break;
    case UI_TOGGLE_MUTE:
        state->output_muted=!state->output_muted;
        break;
    case UI_RATE_MINUS:
        if(!state->rate_index) return 0;
        --state->rate_index;
        break;
    case UI_RATE_PLUS:
        if(state->rate_index>=2u) return 0;
        ++state->rate_index;
        break;
    case UI_TOGGLE_DEBUG:
        state->show_debug=!state->show_debug;
        break;
    case UI_OPEN_CONVERSATION:
        if(state->conversation) return 0;
        state->conversation=1;state->language_menu=state->settings_page=state->history_page=state->history_confirm=0;
#ifdef Z7_LOCAL_AI
        state->ai_page=0;
#endif
        break;
    case UI_CLOSE_CONVERSATION:
        if(!state->conversation) return 0;
        state->conversation=0;
        break;
#endif
    default:
        return 0;
    }
    increment(&state->change_sequence);
    return 1;
}

static void append_final(struct ui_state *state, const struct ui_event *event)
{
    struct ui_history_entry *entry;
    uint32_t i;
    if (state->history_count == UI_HISTORY_MAX) {
#ifdef Z7_HISTORY_UI
        uint32_t removed=state->history[0].pixel_height;
        if(!state->history_follow) state->history_scroll=state->history_scroll>removed?state->history_scroll-removed:0;
        increment(&state->history_discarded);
#endif
        for (i = 1; i < UI_HISTORY_MAX; ++i)
            copy_history(&state->history[i - 1], &state->history[i]);
    } else {
        ++state->history_count;
    }
    entry = &state->history[state->history_count - 1];
#ifdef Z7_HISTORY_UI
    entry->language=state->language;entry->pixel_height=0;
#endif
    entry->epoch = state->epoch;
    entry->segment_id = event->segment_id;
    entry->revision = event->revision;
    entry->source_revision = event->source_revision;
    copy_text(entry->source, state->source);
    copy_text(entry->translated, event->text);
    copy_text(state->last_final_source, state->source);
    copy_text(state->translated, event->text);
    state->last_final_segment = event->segment_id;
    state->processing_segment = 0;
    state->translation_revision = 0;
    state->source_final = 0;
    state->source_revision = 0;
    state->stage = UI_FINAL;
    increment(&state->final_count);
}

int ui_apply_event(struct ui_state *state, const struct ui_event *event)
{
    if (state == 0) return 0;
    if (event == 0 || state->session != UI_RUNNING ||
        event->epoch != state->epoch || event->segment_id == 0 ||
        event->revision == 0 || event->segment_id <= state->last_final_segment)
        return reject(state);
    if (event->type != UI_ASR_PARTIAL && event->type != UI_ASR_FINAL &&
        event->type != UI_TRANSLATING_EVENT && event->type != UI_TRANSLATION_FINAL)
        return reject(state);
    if (event->type != UI_TRANSLATING_EVENT && !text_valid(event->text))
        return reject(state);

    if (event->type == UI_ASR_PARTIAL || event->type == UI_ASR_FINAL) {
        if (state->processing_segment != 0 &&
            state->processing_segment != event->segment_id) return reject(state);
        if (event->revision < state->source_revision) return reject(state);
        if (event->type == UI_ASR_PARTIAL &&
            (state->source_final || event->revision == state->source_revision))
            return reject(state);
        if (event->type == UI_ASR_FINAL &&
            event->revision == state->source_revision &&
            (state->source_final || !same_text(state->source, event->text)))
            return reject(state);
        state->processing_segment = event->segment_id;
        state->source_revision = event->revision;
        state->source_final = event->type == UI_ASR_FINAL ? 1u : 0u;
        state->translation_revision = 0;
        state->stage = UI_RECOGNIZING;
        copy_text(state->source, event->text);
    } else {
        if (state->processing_segment != event->segment_id || !state->source_final ||
            event->source_revision != state->source_revision) return reject(state);
        if (event->type == UI_TRANSLATING_EVENT) {
            if (event->revision <= state->translation_revision) return reject(state);
            state->translation_revision = event->revision;
            state->stage = UI_TRANSLATING;
        } else {
            if (state->stage != UI_TRANSLATING ||
                event->revision < state->translation_revision) return reject(state);
            append_final(state, event);
        }
    }
    increment(&state->change_sequence);
    return 1;
}
