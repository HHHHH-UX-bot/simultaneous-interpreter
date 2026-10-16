#ifndef NETWORK_UI_NETWORK_H
#define NETWORK_UI_NETWORK_H
#include "../ps_wifi/wifi_at_probe.h"
#include "protocol.h"
struct network_status {
    uint32_t magic,sequence,phase,error,config_ack,sta_ip,client,accepted;
    uint32_t rejected,last_request,last_code,received_bytes,overflow,framing,echo_verified,epoch;
};
struct network_configuration {
    /* action 1: LAN; 2: queries; 3: cloud TLS (reserved 0/1 normal/wrong CN);
       4: close owned cloud TLS, no credentials. TLS ready reports phase 8. */
    uint32_t magic,request_sequence,action,ssid_bytes,password_bytes;
    uint8_t credentials[100];
    uint32_t port,reserved; /* action1: reserved=0 LAN server, otherwise TCP peer IPv4. */
};
extern volatile struct network_status network_info;
extern volatile struct network_status network_report;
extern volatile struct network_configuration network_config;
#ifdef Z7_NETWORK_CAPABILITIES
/* Live query state; the OCM copy is published under network_report.sequence. */
struct network_capability_status {
    uint32_t magic,phase,error,query_index,queried_mask,ok_mask,error_mask,invalid_mask;
    uint32_t ssl_buffer,auth_count,auth_args[4],response_bytes,firmware_hash;
};
extern volatile struct network_capability_status network_capabilities;
extern volatile struct network_capability_status network_capability_report;
#endif
void network_init(wifi_read_fn read,wifi_write_fn write);
void network_tick(struct ui_state *state,uint32_t now);
int network_owns_ui(void);
/* Queue at most 2048 bytes; the caller may reuse its input after success. */
int network_send(const uint8_t *data,uint32_t size);
/* Adopt an already established MUX=1 link 0. The AT owner hands over only
   after its last response is consumed. IPD bytes go to receive, not LAN UI. */
int network_stream_attach(void (*receive)(uint8_t));
int network_stream_busy(void);
int network_stream_detach(void);
/* Finish the queued raw block, escape passthrough and close its TCP link. */
void network_stop(void);
#endif
