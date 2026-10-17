#ifndef NETWORK_UI_CLOUD_CLIENT_H
#define NETWORK_UI_CLOUD_CLIENT_H
#include <stdint.h>

/* Runtime cloud configuration written to OCM .cloud_config by the host before
   start; the API key is never compiled into the ELF. */
struct cloud_client_config {
    uint32_t magic;          /* 'CCL1' */
    char path[96];           /* /api-ws/v1/realtime?model=... */
    char workspace[48];      /* ws-... (host prefix) */
    char key[192];           /* bearer key */
};
extern volatile struct cloud_client_config cloud_client_config;

struct cloud_client_state {
    uint32_t phase, session_sent, response_used, sample_toggle, decimate, pcm_bytes;
    uint32_t utterance_started, utterance_ms, last_tick, latency_ms, mask_sequence;
    uint8_t pcm[640];
    char response[512];
};

enum { CLOUD_IDLE, CLOUD_HANDSHAKE, CLOUD_WAITING_SESSION, CLOUD_STREAMING };

void cloud_client_init(void);
void cloud_client_start(void);
void cloud_client_receive(uint8_t byte);
void cloud_client_tick(uint32_t now);
void cloud_client_translation(uint32_t now);

#endif
