#ifndef SUBTITLE_UI_RENDER_H
#define SUBTITLE_UI_RENDER_H
#include <stdint.h>
#include "ui_model.h"

#define UI_WIDTH 1024u
#define UI_HEIGHT 600u
struct ui_render_stats {
    uint32_t missing_glyphs,clipped_glyph_pixels,caption_lines,caption_pages,caption_page;
    uint32_t source_lines,history_lines,pixels_written;
};
void ui_format_elapsed(uint32_t elapsed_ms,uint32_t text[16]);
void ui_render_set_service(void (*service)(void));
void ui_render(uint32_t *pixels,const struct ui_state *state,uint32_t elapsed_ms,
               uint32_t requested_page,struct ui_render_stats *stats);
#endif
