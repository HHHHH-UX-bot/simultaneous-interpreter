#ifndef SUBTITLE_UI_MODEL_H
#define SUBTITLE_UI_MODEL_H

#include <stdint.h>

#define UI_MAX_TEXT 256u
#if defined(Z7_ARM_TLS) && (defined(Z7_TRANSLATE_EN) || defined(Z7_TRANSLATE_ZH))
#define Z7_HISTORY_UI
#define UI_HISTORY_MAX 64u
#else
#define UI_HISTORY_MAX 4u
#endif

enum ui_session { UI_IDLE, UI_RUNNING, UI_PAUSED, UI_BLOCKED };
enum ui_stage { UI_LISTENING, UI_RECOGNIZING, UI_TRANSLATING, UI_FINAL,
                UI_CONNECTING, UI_OFFLINE };
enum ui_command {
    UI_START, UI_PAUSE, UI_RESUME, UI_CLEAR,
    UI_FONT_MINUS, UI_FONT_PLUS, UI_TOGGLE_SOURCE,
    UI_LANGUAGE_MENU, UI_SELECT_EN_ZH, UI_SELECT_ZH_EN, UI_DISMISS_LANGUAGE,
    UI_OPEN_HISTORY, UI_CLOSE_HISTORY, UI_HISTORY_CLEAR,
    UI_HISTORY_CONFIRM_CLEAR, UI_HISTORY_CANCEL_CLEAR, UI_HISTORY_OLDER, UI_HISTORY_NEWER,
    UI_COMMAND_COUNT
};
enum ui_language { UI_EN_ZH, UI_ZH_EN };
enum ui_event_type {
    UI_ASR_PARTIAL, UI_ASR_FINAL, UI_TRANSLATING_EVENT, UI_TRANSLATION_FINAL
};

struct ui_event {
    enum ui_event_type type;
    uint32_t epoch;
    uint32_t segment_id;
    uint32_t revision;
    uint32_t source_revision;
    const uint32_t *text;
};

struct ui_history_entry {
#ifdef Z7_HISTORY_UI
    uint32_t language, pixel_height;
#endif
    uint32_t epoch;
    uint32_t segment_id;
    uint32_t revision;
    uint32_t source_revision;
    uint32_t source[UI_MAX_TEXT];
    uint32_t translated[UI_MAX_TEXT];
};

struct ui_state {
    uint32_t epoch;
    enum ui_session session;
    enum ui_stage stage;
    uint32_t font_index;
    uint32_t show_source;
    uint32_t source[UI_MAX_TEXT];
    uint32_t translated[UI_MAX_TEXT];
    struct ui_history_entry history[UI_HISTORY_MAX];
    uint32_t history_count;
    uint32_t final_count;
    uint32_t rejected_events;
    uint32_t change_sequence;

    /* One active segment; completed segments cannot reopen in this epoch. */
    uint32_t processing_segment;
    uint32_t source_revision;
    uint32_t source_final;
    uint32_t translation_revision;
    uint32_t last_final_segment;
    uint32_t last_final_source[UI_MAX_TEXT];
    enum ui_language language;
    uint32_t language_menu;
#ifdef Z7_HISTORY_UI
    uint32_t history_page,history_scroll,history_limit,history_follow,history_confirm,history_discarded;
#endif
};

void ui_init(struct ui_state *state);
int ui_control(struct ui_state *state, enum ui_command command);
int ui_apply_event(struct ui_state *state, const struct ui_event *event);
#ifdef Z7_HISTORY_UI
/* Pixel scroll of the measured, fixed-size history view; never changes a session. */
int ui_history_scroll(struct ui_state *state,int delta);
#endif

/*
 * Text is UTF-32, at most 255 Unicode scalars plus a terminator. ASR revision
 * identifies the source version; ASR source_revision is ignored. Translation
 * events must name that exact finalized source version. TRANSLATING_EVENT
 * does not read text; the three text events require nonempty valid text.
 * history is oldest-first. Rejected results increment rejected_events only,
 * preserving all display state and change_sequence. Controls returning 0
 * do not mutate state. CLEAR stops to idle, changes epoch, clears results and
 * counters, and preserves font/source preferences. RESUME changes epoch and
 * preserves final text/history; callers obtain the new epoch after control.
 * The model assumes a single serialized caller, not concurrent callbacks.
 */

#endif
