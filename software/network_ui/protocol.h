#ifndef NETWORK_UI_PROTOCOL_H
#define NETWORK_UI_PROTOCOL_H
#include <stdint.h>
#include "../subtitle_ui/ui_model.h"
#define NET_HEADER_BYTES 36u
#define NET_TEXT_BYTES 768u
#define NET_FRAME_BYTES (NET_HEADER_BYTES+NET_TEXT_BYTES)
/* LE u32: SUB1,version,request,type,epoch,segment,revision,source_revision,bytes.
 * type0 hello,1 UI control,2 ASR partial,3 ASR final,4 translating,5 final.
 * ACK1/version/request/type/code/current_epoch/history/final_count (32 bytes).
 */
enum net_code {NET_OK,NET_HEADER,NET_LENGTH,NET_UTF8,NET_GLYPH,NET_EPOCH,NET_MODEL,NET_SEQUENCE,NET_BUSY};
uint32_t net_word(const uint8_t *p);
void net_put(uint8_t *p,uint32_t value);
uint32_t net_apply(struct ui_state *state,const uint8_t *frame,uint32_t bytes,uint32_t *last_request);
void net_ack(uint8_t *ack,const uint8_t *frame,uint32_t code,const struct ui_state *state);
#endif
