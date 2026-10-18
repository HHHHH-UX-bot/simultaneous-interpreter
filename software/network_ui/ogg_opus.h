#ifndef Z7_OGG_OPUS_H
#define Z7_OGG_OPUS_H

#include <stdint.h>

/* Minimal Ogg Opus muxer for the board: 16 kHz mono, 20 ms frames. Every page
   carries one or more complete Opus packets; the qwen3.5 websocket endpoint
   expects an Ogg Opus byte stream, not bare Opus packets. */

#define OGG_CRC_TABLE 256u

void ogg_opus_init(void);
/* OPUSHEAD page (19-byte header packet) and OPUSTAGS page. Return page bytes. */
uint32_t ogg_opus_head(uint8_t *out, uint32_t capacity, uint32_t serial);
uint32_t ogg_opus_tags(uint8_t *out, uint32_t capacity, uint32_t serial);
/* One audio page with count packets (each < 255 bytes). granule is the
   cumulative sample position at 16 kHz after the last packet. */
uint32_t ogg_opus_page(uint8_t *out, uint32_t capacity, int end_of_stream,
                       uint32_t granule, uint32_t serial, uint32_t sequence,
                       const uint8_t *const *packets, const uint8_t *lengths,
                       uint32_t count);

#endif
