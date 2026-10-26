#ifndef TINY_MODEL_TRANSPORT_H
#define TINY_MODEL_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/sha256.h"

#define TMT_PORT 4178u
#define TMT_MAGIC 0x314d4c51u /* QLM1, little endian */
#define TMT_HEADER_BYTES 32u
#define TMT_CHUNK_BYTES 1400u
enum { TMT_BEGIN=1, TMT_DATA=2, TMT_STATUS=3, TMT_FINISH=4, TMT_ABORT=5 };
enum { TMT_OK=0, TMT_READY=1, TMT_BAD_FORMAT=2, TMT_WRONG_MODEL=3,
       TMT_OFFSET=4, TMT_BAD_CRC=5, TMT_BUSY=6, TMT_BAD_DIGEST=7,
       TMT_BAD_SESSION=8, TMT_HASH_ERROR=9, TMT_NOT_COMPLETE=10 };

/* All writable addresses and the accepted digest are supplied by firmware.
   The network packet contains only a checked offset into this one buffer.
   owner is a stable tag for the sender's source IPv4 address and UDP port.
   This is an integrity protocol for a trusted direct cable, not authentication. */
struct tiny_model_transport {
    uint8_t *buffer;
    uint32_t capacity, expected_bytes, received, session_lo, session_hi;
    uint64_t owner;
    uint8_t expected_sha256[32];
    uint8_t active, ready, locked, configured;
    mbedtls_sha256_context sha;
};

int tmt_init(struct tiny_model_transport *t, void *fixed_buffer,
             uint32_t capacity, uint32_t expected_bytes,
             const uint8_t expected_sha256[32]);
/* Returns a 32-byte ACK, or 0 for packets not belonging to this protocol.
   Invoke only after IPv4/UDP length and destination validation. No heap, OS,
   arbitrary memory reads, DMA or Ethernet MMIO are performed here. */
size_t tmt_handle(struct tiny_model_transport *t, uint64_t owner,
                  const uint8_t *packet, size_t bytes,
                  uint8_t reply[TMT_HEADER_BYTES]);
/* Lock before an inference job borrows the weights; unlock after it finishes. */
void tmt_set_locked(struct tiny_model_transport *t, int locked);
int tmt_is_ready(const struct tiny_model_transport *t);
uint32_t tmt_crc32(const void *data, size_t bytes);
#endif
