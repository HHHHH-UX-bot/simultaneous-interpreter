/* Pin upstream framing unchanged; one fixed context replaces its heap calls. */
#define WSLAY_VERSION "1.1.1"
#include "../../third_party/wslay/lib/wslay_frame.h"
__attribute__((section(".cloud_runtime"))) static struct wslay_frame_context cloud_frame_storage;
static void *cloud_frame_alloc(size_t size) {return size==sizeof(cloud_frame_storage)?&cloud_frame_storage:NULL;}
static void cloud_frame_free(void *pointer) {(void)pointer;}
uint64_t wslay_byteswap64(uint64_t value) {return __builtin_bswap64(value);}
#define malloc cloud_frame_alloc
#define free cloud_frame_free
#define htons(value) __builtin_bswap16((uint16_t)(value))
#define NDEBUG
#include "../../third_party/wslay/lib/wslay_frame.c"
