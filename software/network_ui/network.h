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
void network_init(wifi_read_fn read,wifi_write_fn write);
void network_tick(struct ui_state *state,uint32_t now);
int network_owns_ui(void);
#endif
