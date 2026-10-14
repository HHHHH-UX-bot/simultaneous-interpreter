#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_model.h"

static unsigned int checks;
static const uint32_t source_a[] = {'H', 'e', 'l', 'l', 'o', 0};
static const uint32_t source_b[] = {'H', 'e', 'l', 'l', 'o', '!', 0};
static const uint32_t chinese_a[] = {0x4f60u, 0x597du, 0};
static const uint32_t chinese_b[] = {0x8bf7u, 0x7ee7u, 0x7eedu, 0};

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
        return 0; \
    } \
} while (0)

static struct ui_event make_event(const struct ui_state *state,
                                 enum ui_event_type type, uint32_t segment,
                                 uint32_t revision, uint32_t source_revision,
                                 const uint32_t *text)
{
    struct ui_event event;
    event.type = type;
    event.epoch = state->epoch;
    event.segment_id = segment;
    event.revision = revision;
    event.source_revision = source_revision;
    event.text = text;
    return event;
}

static int apply(struct ui_state *state, enum ui_event_type type,
                 uint32_t segment, uint32_t revision, uint32_t source_revision,
                 const uint32_t *text)
{
    struct ui_event event = make_event(state, type, segment, revision,
                                      source_revision, text);
    return ui_apply_event(state, &event);
}

static int equal_text(const uint32_t *a, const uint32_t *b)
{
    unsigned int i;
    for (i = 0; i < UI_MAX_TEXT; ++i) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 0;
}

static int reject_is_atomic(struct ui_state *state, struct ui_event event)
{
    struct ui_state before = *state;
    int accepted = ui_apply_event(state, &event);
    ++before.rejected_events;
    return accepted == 0 && memcmp(&before, state, sizeof(before)) == 0;
}

static int finish_segment(struct ui_state *state, uint32_t segment,
                          const uint32_t *translation)
{
    return apply(state, UI_ASR_FINAL, segment, 1, 0, source_a) &&
           apply(state, UI_TRANSLATING_EVENT, segment, 1, 1, 0) &&
           apply(state, UI_TRANSLATION_FINAL, segment, 1, 1, translation);
}

static int test_initial_controls(void)
{
    struct ui_state state;
    struct ui_state before;
    ui_init(&state);
    CHECK(state.epoch == 1 && state.session == UI_IDLE && state.stage == UI_LISTENING);
    CHECK(state.font_index == 1 && state.show_source == 1);
    CHECK(state.history_count == 0 && state.final_count == 0 && state.change_sequence == 0);
    CHECK(state.source[0] == 0 && state.translated[0] == 0);
    before = state;
    CHECK(ui_control(&state, UI_RESUME) == 0);
    CHECK(ui_control(&state, UI_PAUSE) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_control(&state, UI_FONT_MINUS) == 1 && state.font_index == 0);
    before = state;
    CHECK(ui_control(&state, UI_FONT_MINUS) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_control(&state, UI_FONT_PLUS) == 1 && state.font_index == 1);
    CHECK(ui_control(&state, UI_FONT_PLUS) == 1 && state.font_index == 2);
    before = state;
    CHECK(ui_control(&state, UI_FONT_PLUS) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_control(&state, UI_TOGGLE_SOURCE) == 1 && state.show_source == 0);
    CHECK(ui_control(&state, UI_TOGGLE_SOURCE) == 1 && state.show_source == 1);
    CHECK(state.session == UI_IDLE);
    CHECK(ui_control(&state, UI_START) == 1 && state.session == UI_RUNNING);
    before = state;
    CHECK(ui_control(&state, UI_START) == 0);
    CHECK(ui_control(&state, UI_RESUME) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_control(&state, UI_FONT_MINUS) == 1 && state.session == UI_RUNNING);
    CHECK(ui_control(&state, UI_TOGGLE_SOURCE) == 1 && state.session == UI_RUNNING);
    return 1;
}

static int test_drafts_and_revisions(void)
{
    struct ui_state state;
    struct ui_event event;
    uint32_t sequence;
    ui_init(&state);
    CHECK(ui_control(&state, UI_START) == 1);
    CHECK(apply(&state, UI_ASR_PARTIAL, 1, 1, 0, source_a) == 1);
    CHECK(state.stage == UI_RECOGNIZING && state.session == UI_RUNNING);
    CHECK(state.history_count == 0 && state.final_count == 0 && state.translated[0] == 0);
    CHECK(equal_text(state.source, source_a));
    event = make_event(&state, UI_ASR_PARTIAL, 1, 1, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_ASR_PARTIAL, 1, 2, 0, source_b) == 1);
    event = make_event(&state, UI_ASR_PARTIAL, 1, 1, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_PARTIAL, 2, 1, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_FINAL, 1, 2, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_TRANSLATING_EVENT, 1, 1, 2, 0);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_ASR_FINAL, 1, 2, 0, source_b) == 1);
    CHECK(state.source_final == 1 && state.source_revision == 2);
    event = make_event(&state, UI_ASR_FINAL, 1, 2, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_PARTIAL, 1, 3, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_TRANSLATING_EVENT, 1, 1, 1, 0);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 1, 2, chinese_a);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_TRANSLATING_EVENT, 1, 2, 2, 0) == 1);
    CHECK(state.stage == UI_TRANSLATING && state.session == UI_RUNNING);
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 1, 2, chinese_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 2, 1, chinese_a);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_TRANSLATING_EVENT, 1, 3, 2, 0) == 1);
    event = make_event(&state, UI_TRANSLATING_EVENT, 1, 2, 2, 0);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_ASR_FINAL, 1, 3, 0, source_a) == 1);
    CHECK(state.stage == UI_RECOGNIZING && state.translation_revision == 0);
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 3, 2, chinese_a);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_TRANSLATING_EVENT, 1, 1, 3, 0) == 1);
    sequence = state.change_sequence;
    CHECK(apply(&state, UI_TRANSLATION_FINAL, 1, 2, 3, chinese_a) == 1);
    CHECK(state.change_sequence == sequence + 1 && state.stage == UI_FINAL);
    CHECK(state.history_count == 1 && state.final_count == 1);
    CHECK(state.history[0].epoch == state.epoch && state.history[0].segment_id == 1);
    CHECK(state.history[0].revision == 2 && state.history[0].source_revision == 3);
    CHECK(equal_text(state.source, source_a) && equal_text(state.translated, chinese_a));
    CHECK(equal_text(state.history[0].source, source_a));
    CHECK(equal_text(state.history[0].translated, chinese_a));
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 2, 3, chinese_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 9, 3, chinese_b);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_FINAL, 1, 9, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_ASR_PARTIAL, 2, 1, 0, source_b) == 1);
    CHECK(equal_text(state.translated, chinese_a) && state.history_count == 1);
    return 1;
}

static int test_pause_resume_clear(void)
{
    struct ui_state state;
    struct ui_event delayed;
    struct ui_state before;
    uint32_t original_epoch;
    ui_init(&state);
    CHECK(ui_control(&state, UI_START) == 1);
    CHECK(finish_segment(&state, 1, chinese_a));
    CHECK(apply(&state, UI_ASR_FINAL, 2, 1, 0, source_b) == 1);
    CHECK(apply(&state, UI_TRANSLATING_EVENT, 2, 1, 1, 0) == 1);
    delayed = make_event(&state, UI_TRANSLATION_FINAL, 2, 1, 1, chinese_b);
    original_epoch = state.epoch;
    CHECK(ui_control(&state, UI_PAUSE) == 1 && state.session == UI_PAUSED);
    CHECK(state.processing_segment == 0 && state.stage == UI_FINAL);
    CHECK(equal_text(state.source, source_a) && equal_text(state.translated, chinese_a));
    CHECK(state.history_count == 1 && state.final_count == 1);
    CHECK(reject_is_atomic(&state, delayed));
    before = state;
    CHECK(ui_control(&state, UI_START) == 0 && ui_control(&state, UI_PAUSE) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_control(&state, UI_FONT_PLUS) == 1 && state.session == UI_PAUSED);
    CHECK(ui_control(&state, UI_TOGGLE_SOURCE) == 1 && state.session == UI_PAUSED);
    CHECK(ui_control(&state, UI_RESUME) == 1 && state.session == UI_RUNNING);
    CHECK(state.epoch == original_epoch + 1 && state.stage == UI_LISTENING);
    CHECK(equal_text(state.translated, chinese_a) && state.history_count == 1);
    CHECK(reject_is_atomic(&state, delayed));
    CHECK(finish_segment(&state, 1, chinese_b));
    CHECK(state.history_count == 2 && state.final_count == 2);
    CHECK(state.history[0].segment_id == 1 && state.history[1].segment_id == 1);
    CHECK(state.history[0].epoch != state.history[1].epoch);
    delayed = make_event(&state, UI_ASR_FINAL, 2, 1, 0, source_a);
    original_epoch = state.epoch;
    CHECK(ui_control(&state, UI_CLEAR) == 1);
    CHECK(state.epoch == original_epoch + 1 && state.session == UI_IDLE);
    CHECK(state.history_count == 0 && state.final_count == 0 && state.rejected_events == 0);
    CHECK(state.source[0] == 0 && state.translated[0] == 0 && state.stage == UI_LISTENING);
    CHECK(state.font_index == 2 && state.show_source == 0);
    CHECK(reject_is_atomic(&state, delayed));
    CHECK(ui_control(&state, UI_START) == 1);
    CHECK(reject_is_atomic(&state, delayed));
    CHECK(finish_segment(&state, 1, chinese_a));
    CHECK(state.history_count == 1 && state.final_count == 1);
    CHECK(ui_control(&state, UI_PAUSE) == 1);
    CHECK(ui_control(&state, UI_CLEAR) == 1 && state.session == UI_IDLE);
    CHECK(ui_control(&state, UI_RESUME) == 0);
    CHECK(ui_control(&state, UI_START) == 1);
    CHECK(apply(&state, UI_ASR_PARTIAL, 1, 1, 0, source_a) == 1);
    CHECK(ui_control(&state, UI_PAUSE) == 1 && state.stage == UI_LISTENING);
    CHECK(state.source[0] == 0 && state.translated[0] == 0);
    return 1;
}

static int test_history_capacity(void)
{
    struct ui_state state;
    struct ui_event event;
    uint32_t segment;
    ui_init(&state);
    CHECK(ui_control(&state, UI_START) == 1);
    for (segment = 1; segment <= 12; ++segment) {
        CHECK(finish_segment(&state, segment, chinese_a));
        CHECK(state.final_count == segment);
        CHECK(state.history_count == (segment < UI_HISTORY_MAX ? segment : UI_HISTORY_MAX));
        CHECK(state.history[state.history_count - 1].segment_id == segment);
        CHECK(state.history[0].segment_id == (segment < UI_HISTORY_MAX ? 1u : segment - 3u));
    }
    for (segment = 0; segment < UI_HISTORY_MAX; ++segment) {
        CHECK(state.history[segment].segment_id == segment + 9);
        CHECK(equal_text(state.history[segment].source, source_a));
        CHECK(equal_text(state.history[segment].translated, chinese_a));
    }
    event = make_event(&state, UI_ASR_FINAL, 1, 50, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_FINAL, 11, 50, 0, source_b);
    CHECK(reject_is_atomic(&state, event));
    return 1;
}

static int test_text_bounds_and_atomic_rejection(void)
{
    struct ui_state state;
    struct ui_event event;
    uint32_t oversized[UI_MAX_TEXT + 1];
    uint32_t maximum[UI_MAX_TEXT];
    uint32_t surrogate[] = {0xd800u, 0};
    uint32_t too_large[] = {0x110000u, 0};
    uint32_t empty[] = {0};
    uint32_t supplementary[] = {0x1f642u, 0};
    uint32_t i;
    for (i = 0; i < UI_MAX_TEXT; ++i) oversized[i] = 'A';
    oversized[UI_MAX_TEXT] = 0;
    for (i = 0; i < UI_MAX_TEXT - 1; ++i) maximum[i] = 0x4e00u;
    maximum[UI_MAX_TEXT - 1] = 0;
    ui_init(&state);
    CHECK(ui_control(&state, UI_START) == 1);
    event = make_event(&state, UI_ASR_PARTIAL, 1, 1, 0, oversized);
    CHECK(reject_is_atomic(&state, event));
    event.text = 0;
    CHECK(reject_is_atomic(&state, event));
    event.text = empty;
    CHECK(reject_is_atomic(&state, event));
    event.text = surrogate;
    CHECK(reject_is_atomic(&state, event));
    event.text = too_large;
    CHECK(reject_is_atomic(&state, event));
    CHECK(apply(&state, UI_ASR_PARTIAL, 1, 1, 0, supplementary) == 1);
    CHECK(equal_text(state.source, supplementary));
    CHECK(apply(&state, UI_ASR_FINAL, 1, 2, 0, maximum) == 1);
    CHECK(equal_text(state.source, maximum));
    CHECK(apply(&state, UI_TRANSLATING_EVENT, 1, 1, 2, 0) == 1);
    event = make_event(&state, UI_TRANSLATION_FINAL, 1, 1, 2, oversized);
    CHECK(reject_is_atomic(&state, event));
    CHECK(state.stage == UI_TRANSLATING && state.history_count == 0);
    CHECK(apply(&state, UI_TRANSLATION_FINAL, 1, 1, 2, maximum) == 1);
    CHECK(equal_text(state.translated, maximum));
    CHECK(equal_text(state.history[0].source, maximum));
    CHECK(equal_text(state.history[0].translated, maximum));
    event = make_event(&state, (enum ui_event_type)99, 2, 1, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_PARTIAL, 0, 1, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    event = make_event(&state, UI_ASR_PARTIAL, 2, 0, 0, source_a);
    CHECK(reject_is_atomic(&state, event));
    return 1;
}

static int test_epoch_exhaustion_and_invalid_api(void)
{
    struct ui_state state;
    struct ui_state before;
    ui_init(0);
    CHECK(ui_control(0, UI_START) == 0);
    CHECK(ui_apply_event(0, 0) == 0);
    ui_init(&state);
    before = state;
    CHECK(ui_control(&state, (enum ui_command)99) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(ui_apply_event(&state, 0) == 0 && state.rejected_events == 1);
    state.epoch = 0xffffffffu;
    CHECK(ui_control(&state, UI_START) == 1);
    CHECK(ui_control(&state, UI_PAUSE) == 1);
    before = state;
    CHECK(ui_control(&state, UI_RESUME) == 0);
    CHECK(ui_control(&state, UI_CLEAR) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    state.session = UI_BLOCKED;
    before = state;
    CHECK(ui_control(&state, UI_START) == 0 && ui_control(&state, UI_RESUME) == 0);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    state.rejected_events = 0xffffffffu;
    CHECK(ui_apply_event(&state, 0) == 0 && state.rejected_events == 0xffffffffu);
    state.change_sequence = 0xffffffffu;
    CHECK(ui_control(&state, UI_TOGGLE_SOURCE) == 1 && state.change_sequence == 0xffffffffu);
    return 1;
}

int main(void)
{
    int passed = test_initial_controls() && test_drafts_and_revisions() &&
                 test_pause_resume_clear() && test_history_capacity() &&
                 test_text_bounds_and_atomic_rejection() &&
                 test_epoch_exhaustion_and_invalid_api();
    if (!passed) return 1;
    printf("PASS_SUBTITLE_MODEL groups=6 checks=%u state_bytes=%u\n",
           checks, (unsigned int)sizeof(struct ui_state));
    return 0;
}
