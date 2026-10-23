/* Host protocol harness ONLY. Do not include this in firmware builds. */
#include <stdint.h>
extern const void *tbl_test_tokenizer_blob;
#define TBL_MODEL_ADDRESS ((const void *)(uintptr_t)1)
#define TBL_MODEL_SIZE 8192u
#define TBL_TOKENIZER_ADDRESS tbl_test_tokenizer_blob
#define TBL_ARENA_ADDRESS ((void *)(uintptr_t)1)
