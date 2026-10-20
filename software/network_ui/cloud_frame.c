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
/* Protocol metadata only: never export the parser's payload buffer. */
void cloud_frame_debug(uint32_t *out) {
    out[0]=0x31444657u;out[1]=(uint32_t)cloud_frame_storage.istate;
    out[2]=cloud_frame_storage.iom.opcode;out[3]=cloud_frame_storage.iom.fin;
    out[4]=cloud_frame_storage.iom.rsv;out[5]=cloud_frame_storage.imask;
    out[6]=(uint32_t)cloud_frame_storage.ipayloadlen;out[7]=(uint32_t)cloud_frame_storage.ipayloadoff;
}
