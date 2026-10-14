#ifndef NETWORK_UI_NETWORK_H
#define NETWORK_UI_NETWORK_H
#include "../ps_wifi/wifi_at_probe.h"
#include "protocol.h"
struct network_status {
    uint32_t magic,sequence,phase,error,config_ack,sta_ip,client,accepted;
    uint32_t rejected,last_request,last_code,received_bytes,overflow,framing,echo_verified,epoch;
};
struct network_configuration {
    uint32_t magic,request_sequence,action,ssid_bytes,password_bytes;
    uint8_t credentials[100];
    uint32_t port,reserved;
};
extern volatile struct network_status network_info;
extern volatile struct network_configuration network_config;
#ifdef Z7_NETWORK_CAPABILITIES
/* Public numeric query results; published under network_info.sequence. */
struct network_capability_status {
    uint32_t magic,phase,error,query_index,queried_mask,ok_mask,error_mask,invalid_mask;
    uint32_t ssl_buffer,auth_count,auth_args[4],response_bytes,firmware_hash;
};
extern volatile struct network_capability_status network_capabilities;
#endif
void network_init(wifi_read_fn read,wifi_write_fn write);
void network_tick(struct ui_state *state,uint32_t now);
int network_owns_ui(void);
#endif
