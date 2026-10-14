#ifndef SUBTITLE_UI_ASSETS_H
#define SUBTITLE_UI_ASSETS_H

#include <stdint.h>

/* Each glyph is row-major 4-bit coverage: high nibble first, 0..15. */
struct ui_glyph {
    uint32_t codepoint;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
    uint16_t advance;
    int16_t x_offset;
    int16_t y_offset;
};

/* y_offset is relative to the top of a line whose baseline is at size px. */
struct ui_font {
    uint16_t size;
    uint16_t count;
    const struct ui_glyph *glyphs;
    const uint8_t *pixels;
};

struct ui_label {
    const char *key;
    const uint32_t *text;
};

struct ui_demo_text {
    const uint32_t *source;
    const uint32_t *translated;
};

extern const struct ui_font ui_fonts[];
extern const uint32_t ui_font_count;
extern const struct ui_label ui_labels[];
extern const uint32_t ui_label_count;
extern const struct ui_demo_text ui_demo_samples[];
extern const uint32_t ui_demo_sample_count;

#endif
