#include "opus_codec.h"
#include "opus.h"
#include <stddef.h>

/* About 24.6 KiB is the encoder state; the scratch buffer is reused because the
   firmware encodes one frame at a time from a single service loop. */
#define OPUS_POOL_BYTES 49152u
#define OPUS_SCRATCH_BYTES 32768u

static unsigned char opus_pool[OPUS_POOL_BYTES] __attribute__((aligned(8)));
static unsigned char opus_scratch[OPUS_SCRATCH_BYTES] __attribute__((aligned(8)));
static unsigned opus_pool_used;
static OpusEncoder *encoder;

void *opus_alloc(size_t size) {
    size = (size + 7u) & ~(size_t)7u;
    if (opus_pool_used + size > sizeof(opus_pool)) return 0;
    void *pointer = opus_pool + opus_pool_used;
    opus_pool_used += (unsigned)size;
    return pointer;
}

void *opus_alloc_scratch(size_t size) {
    return size <= sizeof(opus_scratch) ? (void *)opus_scratch : 0;
}

void opus_free(void *pointer) { (void)pointer; }

/* libopus calls the C library abs() from its fixed-point code; the firmware is
   freestanding, so provide it here. */
int abs(int value) { return value < 0 ? -value : value; }

int opus_codec_init(void) {
    if (encoder) return 1;
    int error = 0;
    encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    if (!encoder || error) return 0;
    /* Retain the 24 kbit/s accuracy baseline. Uplink stalls are absorbed by
       the packet queue; pacing and queue diagnostics are in cloud_client.c. */
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    /* CBR caps voiced 20 ms packets at 60 bytes, keeping batch sizes bounded.
       Audio mode may return smaller DTX packets for silence. */
    opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
#ifdef Z7_TRANSLATE_AUDIO
    /* Keep 24 kbit/s voiced frames; send Opus comfort-noise packets during
       silence so translated PCM does not compete with padded silent uploads. */
    opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
#else
    opus_encoder_ctl(encoder, OPUS_SET_DTX(0));
#endif
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(0));
    return 1;
}

int opus_codec_encode(const int16_t *pcm, uint8_t *out, uint32_t capacity) {
    if (!encoder) return -1;
    return opus_encode(encoder, (const opus_int16 *)pcm, 320, (unsigned char *)out,
                       (opus_int32)capacity);
}
