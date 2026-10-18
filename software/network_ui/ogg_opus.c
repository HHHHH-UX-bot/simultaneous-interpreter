#include "ogg_opus.h"
#include <string.h>

static uint32_t crc_table[OGG_CRC_TABLE];
static int crc_ready;

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

void ogg_opus_init(void) {
    if (crc_ready) return;
    for (uint32_t i = 0; i < OGG_CRC_TABLE; ++i) {
        uint32_t value = i << 24;
        for (uint32_t bit = 0; bit < 8; ++bit)
            value = (value & 0x80000000u) ? ((value << 1) ^ 0x04C11DB7u) : (value << 1);
        crc_table[i] = value;
    }
    crc_ready = 1;
}

static uint32_t crc32(const uint8_t *data, uint32_t bytes) {
    uint32_t crc = 0;
    while (bytes--) crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *data++) & 0xFFu];
    return crc;
}

/* Builds the page header and gathers packet bytes into the body. */
static uint32_t build_page(uint8_t *out, uint32_t capacity, int header_type,
                           uint32_t granule_low, uint32_t serial, uint32_t sequence,
                           const uint8_t *const *packets, const uint8_t *lengths,
                           uint32_t count) {
    uint32_t body = 0;
    for (uint32_t i = 0; i < count; ++i) body += lengths[i];
    uint32_t total = 27u + count + body;
    if (total > capacity) return 0;
    memcpy(out, "OggS", 4);
    out[4] = 0;
    out[5] = (uint8_t)header_type;
    put_u32(out + 6, granule_low);
    put_u32(out + 10, 0);              /* granule high 32 bits */
    put_u32(out + 14, serial);
    put_u32(out + 18, sequence);
    put_u32(out + 22, 0);              /* checksum placeholder */
    out[26] = (uint8_t)count;
    uint32_t at = 27;
    for (uint32_t i = 0; i < count; ++i) out[at++] = lengths[i];
    for (uint32_t i = 0; i < count; ++i) {
        memcpy(out + at, packets[i], lengths[i]);
        at += lengths[i];
    }
    put_u32(out + 22, crc32(out, total));
    return total;
}

uint32_t ogg_opus_head(uint8_t *out, uint32_t capacity, uint32_t serial) {
    static const uint8_t header[19] = {
        'O','p','u','s','H','e','a','d', 1, 1,
        0x38, 0x01,                    /* pre-skip 312 */
        0x80, 0x3E, 0x00, 0x00,        /* input sample rate 16000 */
        0x00, 0x00, 0x00
    };
    const uint8_t *packets[1] = {header};
    const uint8_t lengths[1] = {sizeof(header)};
    ogg_opus_init();
    return build_page(out, capacity, 0x02, 0, serial, 0, packets, lengths, 1);
}

uint32_t ogg_opus_tags(uint8_t *out, uint32_t capacity, uint32_t serial) {
    static const uint8_t tags[22] = {
        'O','p','u','s','T','a','g','s',
        6, 0, 0, 0, 'z','7','n','a','n','o',
        0, 0, 0, 0
    };
    const uint8_t *packets[1] = {tags};
    const uint8_t lengths[1] = {sizeof(tags)};
    ogg_opus_init();
    return build_page(out, capacity, 0x00, 0, serial, 1, packets, lengths, 1);
}

uint32_t ogg_opus_page(uint8_t *out, uint32_t capacity, int end_of_stream,
                       uint32_t granule, uint32_t serial, uint32_t sequence,
                       const uint8_t *const *packets, const uint8_t *lengths,
                       uint32_t count) {
    return build_page(out, capacity, end_of_stream ? 0x04 : 0x00,
                      granule, serial, sequence, packets, lengths, count);
}
