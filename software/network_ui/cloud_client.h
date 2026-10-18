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
    uint32_t phase, session_sent, update_pending, update_ms, update_retries, response_used, sample_toggle, decimate, pcm_bytes;
    uint32_t utterance_started, utterance_reported, utterance_ms, now_ms, latency_ms, mask_sequence, event_sequence;
    /* T1 is measured from the server's speech-start event to the first
       recognised characters: the service's own audio timeline drops long
       silences, so mapping it back to capture wall time is not reliable. */
    uint32_t opus_ready;
    uint32_t opus_frames, opus_bytes;
    uint32_t ogg_header_step, ogg_seq, ogg_granule;
    /* Diagnostics visible to the host: the last append's JSON size and head. */
    uint32_t tx_len, tx_frames;
    char tx_head[40];
    uint8_t pcm[1440];
    char response[512];
};

enum { CLOUD_IDLE, CLOUD_HANDSHAKE, CLOUD_WAITING_SESSION, CLOUD_WAITING_UPDATE, CLOUD_STREAMING };

void cloud_client_init(void);
void cloud_client_start(uint32_t now);
/* Called from the cloud event mapper: session.created arrives first, then the
   session.update is acknowledged by session.updated, and only then may audio be
   appended (matching the verified host-side sequence). */
void cloud_client_session_created(void);
void cloud_client_session_updated(void);
void cloud_client_speech_started(uint32_t audio_start_ms);
void cloud_client_capture_freeze(void);
void cloud_client_stop(void);
void cloud_client_receive(uint8_t byte);
void cloud_client_tick(uint32_t now);
void cloud_client_translation(uint32_t now);
void cloud_client_response_done(void);

#endif
