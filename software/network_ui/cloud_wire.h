#ifndef NETWORK_UI_CLOUD_WIRE_H
#define NETWORK_UI_CLOUD_WIRE_H
#include <stdint.h>
struct cloud_wire_stats {uint32_t error,messages,pings,closed,mask_used,close_code;};
extern uint8_t cloud_masks[128];
void cloud_wire_init(int (*message)(const char *,uint32_t,void *),void *user);
void cloud_wire_byte(uint8_t byte);
void cloud_wire_bytes(const uint8_t *data,uint32_t bytes);
const struct cloud_wire_stats *cloud_wire_status(void);
const uint8_t *cloud_wire_reply(uint32_t *bytes);
void cloud_wire_reply_sent(void);
#endif
