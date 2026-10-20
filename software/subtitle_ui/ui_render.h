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
/* Two 16-bit readings: valid[15], above-range[14], value[13:0]. Snapshot v2
   stores this word in its reserved header field for reproducible rendering. */
uint32_t ui_latency_pack(uint32_t loop_us,int loop_valid,uint32_t text_ms,int text_valid);
void ui_render_latency(uint32_t *pixels,uint32_t readings,struct ui_render_stats *stats);
void ui_render(uint32_t *pixels,const struct ui_state *state,uint32_t elapsed_ms,
               uint32_t requested_page,struct ui_render_stats *stats);
#endif
