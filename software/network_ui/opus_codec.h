#ifndef Z7_OPUS_CODEC_H
#define Z7_OPUS_CODEC_H

#include <stdint.h>

/* Fixed-point Opus encoder for the board: 16 kHz mono, 20 ms frames. */
int opus_codec_init(void);
/* Encodes 320 samples (20 ms) and returns the packet length in bytes, or -1. */
int opus_codec_encode(const int16_t *pcm, uint8_t *out, uint32_t capacity);

#endif
