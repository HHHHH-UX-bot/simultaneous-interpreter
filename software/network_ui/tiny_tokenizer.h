#ifndef TINY_TOKENIZER_H
#define TINY_TOKENIZER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Caller owns blob for the entire tokenizer lifetime. All formats little-endian.
 * init allocates a ~2MiB merge hash; encode allocates input-proportional scratch.
 * No libc file I/O or target-specific dependencies. */
typedef struct {
    const unsigned char *blob, *token_data;
    const uint32_t *tokens, *merges, *byte_ids, *categories, *ccc, *decomp, *compose;
    uint32_t vocab_size, merge_count, category_count, ccc_count, decomp_count, compose_count;
    uint32_t *merge_hash, hash_mask;
} TinyTokenizer;

enum { TINY_TOKENIZER_OK=0, TINY_TOKENIZER_INVALID=-1,
       TINY_TOKENIZER_NOMEM=-2, TINY_TOKENIZER_CAPACITY=-3 };

int tiny_tokenizer_init(TinyTokenizer *tk, const void *blob, size_t bytes);
void tiny_tokenizer_destroy(TinyTokenizer *tk);
/* Recognizes all model added tokens, matching its split_special_tokens=false.
 * count always returns required count, including on CAPACITY. No BOS is added. */
int tiny_tokenizer_encode(TinyTokenizer *tk, const char *utf8, size_t bytes,
                          int32_t *ids, size_t capacity, size_t *count);
/* Raw bytes may contain NUL or a partial UTF8 character. special is true for
 * special added tokens; callers can omit those for skip_special_tokens=true. */
int tiny_tokenizer_token_bytes(const TinyTokenizer *tk, int32_t token,
                               const unsigned char **bytes, size_t *length,
                               int *special);

typedef struct { unsigned char pending[4]; unsigned char length; } TinyUtf8Stream;
/* Writes only complete UTF8 sequences; replacement U+FFFD for invalid input.
 * output needs at least 3*bytes+12 capacity; returns bytes or SIZE_MAX if too small.
 * finish=1 flushes a trailing incomplete sequence as U+FFFD. */
size_t tiny_utf8_stream_push(TinyUtf8Stream *stream, const unsigned char *bytes,
                             size_t length, int finish, char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
