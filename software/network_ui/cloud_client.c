// ARM-side realtime cloud client: microphone PCM -> Bailian livetranslate WSS
// over the ESP8266 TLS link, and Chinese subtitle events back to the UI.
//
// The TLS link itself is owned by network.c (action 3 / phase 8). This module
// assumes an established multiplexed link and speaks the WebSocket + realtime
// JSON protocol on top of it. The API key never lives in the ELF: it is held in
// the .cloud_config OCM window, written by the host before start.
#include "cloud_client.h"
#include "network.h"
#include "cloud_wire.h"
#include "cloud_events.h"
#include "cloud_live.h"
#include <string.h>

#define AUDIO_SAMPLE   (*(volatile uint32_t *)0x43c20080u) // [16]=toggle, [15:0]=sample
#define AUDIO_COUNT    (*(volatile uint32_t *)0x43c20084u)
#define AUDIO_FIFO_DATA     0x8cu  // 16 kHz sample, reading pops it
#define AUDIO_FIFO_COUNT    0x90u  // samples buffered by the PL
#define AUDIO_FIFO_OVERFLOWS 0x94u // dropped-at-full counter (write clears)
#define audio_regs32(off) (*(volatile uint32_t *)(0x43c20000u + (off)))

// Latency register consumed by the HDMI overlay (audio_regs offset 0x88).
#define AUDIO_LATENCY_REG 0x88u
#define AUDIO_HISTORY 96u

__attribute__((section(".cloud_config"),used)) volatile struct cloud_client_config cloud_client_config;
int cloud_live_message(const char *json,uint32_t size,void *user);
static struct cloud_client_state client;
static void pcm_flush(void);

static void append(char *out,uint32_t *used,uint32_t limit,const char *text) {
    while(*text && *used<limit) out[(*used)++]=*text++;
}
static void append_decimal(char *out,uint32_t *used,uint32_t limit,uint32_t value) {
    char digits[10];uint32_t n=0;
    do {digits[n++]=(char)('0'+value%10u);value/=10u;} while(value);
    while(n && *used<limit) out[(*used)++]=digits[--n];
}

// RFC 6455 client frame: FIN|text, mask bit, optional extended length, 4-byte
// mask, masked payload. Masks are a self-incrementing pattern; masking is a
// protocol requirement, not a security boundary.
static uint32_t build_frame(uint8_t *out,uint32_t limit,const uint8_t *payload,uint32_t size) {
    uint32_t at=0;
    if(size+14u>limit) return 0;
    out[at++]=0x81u;
    if(size<126u) out[at++]=(uint8_t)(0x80u|size);
    else {out[at++]=(uint8_t)(0x80u|126u);out[at++]=(uint8_t)(size>>8);out[at++]=(uint8_t)size;}
    uint32_t mask=0x9e3779b9u ^ (++client.mask_sequence*0x85ebca6bu);
    out[at++]=(uint8_t)mask;out[at++]=(uint8_t)(mask>>8);out[at++]=(uint8_t)(mask>>16);out[at++]=(uint8_t)(mask>>24);
    for(uint32_t i=0;i<size;++i) out[at++]=(uint8_t)(payload[i]^((uint8_t *)&mask)[i&3u]);
    return at;
}

static int send_json(const char *json,uint32_t size) {
    static uint8_t frame[2048];
    uint32_t used=build_frame(frame,sizeof(frame),(const uint8_t *)json,size);
    if(!used) return 0;
    return network_send(frame,used);
}

static void send_session_update(void) {
    static const char update[] =
        "{\"event_id\":\"session_config\",\"type\":\"session.update\","
        "\"session\":{\"audio\":{\"input\":{\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":500}}},\"translation\":{\"language\":\"zh\"},\"output_modalities\":[\"text\"]}}";
    client.phase=CLOUD_WAITING_UPDATE;
    client.session_sent=1u;
    // The previous CIPSEND may still be finishing; keep retrying until the
    // network channel accepts the frame instead of silently losing the config.
    client.update_pending=send_json(update,(uint32_t)sizeof(update)-1u)?0u:1u;
}

/* session.created is the first server event; only then may the update be sent
   (sending it earlier makes the service answer with COMMON_ERROR). */
void cloud_client_session_created(void) {
    if(client.phase!=CLOUD_WAITING_SESSION) return;
    send_session_update();
}

void cloud_client_session_updated(void) {
    if(client.phase!=CLOUD_WAITING_UPDATE) return;
    pcm_flush();
    client.phase=CLOUD_STREAMING;
    client.utterance_started=0;
    client.utterance_reported=0;
    client.utterance_wall_valid=0;
    client.audio_ms=0;
    client.hist_pos=0;
}

/* The server's VAD reports where in the uploaded stream the speech began.
   Map that audio position back to the wall clock using the append history, so
   T1 measures from when the words were captured, not from when the event
   happened to arrive over Wi-Fi. */
void cloud_client_speech_started(uint32_t audio_start_ms) {
    client.utterance_started=1;
    client.utterance_reported=0;
    client.utterance_ms=client.now_ms;
    client.utterance_wall=client.now_ms;
    client.utterance_wall_valid=0;
    uint32_t count=client.hist_pos<AUDIO_HISTORY?client.hist_pos:AUDIO_HISTORY;
    const struct cloud_audio_stamp *best=0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t slot=(client.hist_pos-1u-i)%AUDIO_HISTORY;
        const struct cloud_audio_stamp *entry=&client.hist[slot];
        if(entry->audio<audio_start_ms) continue;
        if(!best || entry->audio<best->audio) best=entry;
    }
    if(!best && count) best=&client.hist[(client.hist_pos-count)%AUDIO_HISTORY];
    if(best) {
        client.utterance_wall=best->wall-(best->audio-audio_start_ms);
        client.utterance_wall_valid=1;
    }
}

/* Stop appending after a cloud error or close. A dead session must not leave a
   stale T1 number on the display, so the overlay goes back to dashes. */
void cloud_client_stop(void) {
    client.phase=CLOUD_IDLE;
    client.pcm_bytes=0;
    client.utterance_wall_valid=0;
    audio_regs32(AUDIO_LATENCY_REG)=0;
}

static void send_handshake(void) {
    static char request[768];uint32_t used=0;
    append(request,&used,sizeof(request),"GET ");
    append(request,&used,sizeof(request),(const char *)cloud_client_config.path);
    append(request,&used,sizeof(request)," HTTP/1.1\r\nHost: ");
    append(request,&used,sizeof(request),(const char *)cloud_client_config.workspace);
    append(request,&used,sizeof(request),".cn-beijing.maas.aliyuncs.com\r\nAuthorization: Bearer ");
    append(request,&used,sizeof(request),(const char *)cloud_client_config.key);
    append(request,&used,sizeof(request),"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
    append(request,&used,sizeof(request),"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
    append(request,&used,sizeof(request),"Sec-WebSocket-Version: 13\r\n\r\n");
    client.phase=CLOUD_HANDSHAKE;
    client.response_used=0;
    network_send((const uint8_t *)request,used);
}

void cloud_client_init(void) {
    memset(&client,0,sizeof(client));
    client.phase=CLOUD_IDLE;
    audio_regs32(AUDIO_LATENCY_REG)=0;
    pcm_flush();
}
void cloud_client_start(uint32_t now) {
    if(client.phase!=CLOUD_IDLE) return;
    cloud_live_begin(now);                     // model session + wire mapper
    network_stream_attach(cloud_client_receive);
    send_handshake();
}

// A complete HTTP response is recognised by the 101 status and the empty line.
int cloud_client_handshake_complete(const char *response,uint32_t bytes) {
    if(bytes<12u) return 0;
    for(uint32_t i=0;i+12u<bytes;++i) {
        if(response[i]==' ' && response[i+1]=='1' && response[i+2]=='0' && response[i+3]=='1' &&
           response[i+4]==' ') {
            for(uint32_t j=i+5;j+3u<bytes;++j)
                if(response[j]=='\r' && response[j+1]=='\n' && response[j+2]=='\r' && response[j+3]=='\n')
                    return 1;
            return 0;
        }
    }
    return 0;
}

void cloud_client_receive(uint8_t byte) {
    if(client.phase==CLOUD_HANDSHAKE) {
        if(client.response_used<sizeof(client.response)-1u) client.response[client.response_used++]=byte;
        if(cloud_client_handshake_complete(client.response,client.response_used)) {
            client.phase=CLOUD_WAITING_SESSION;
            client.response_used=0;
        }
        return;
    }
    if(client.phase==CLOUD_WAITING_SESSION || client.phase==CLOUD_WAITING_UPDATE ||
       client.phase==CLOUD_STREAMING) {
        cloud_wire_byte(byte);
    }
}

/* Discard everything the PL buffered before the cloud session started, so the
   first append does not contain microphone audio from before the handshake. */
static void pcm_flush(void) {
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>1024u) count=1024u;
    while(count--) (void)audio_regs32(AUDIO_FIFO_DATA);
    audio_regs32(AUDIO_FIFO_OVERFLOWS)=0;
    client.pcm_bytes=0;
}

/* One WebSocket append carries 45 ms (720 samples / 1440 B PCM) instead of the
   20 ms the protocol's PC reference used. At the ESP's per-CIPSEND round-trip
   the smaller packet cannot sustain 16 kHz real time; the larger one can, and
   the cloud accepts any append length. */
#define PCM_CHUNK 1440u

static void send_pcm(uint32_t now) {
    static char json[2100];uint32_t used=0;
    uint32_t bytes=client.pcm_bytes;
    client.pcm_bytes=0;
    if(!bytes) return;
    if(!client.utterance_started) {client.utterance_started=1u;client.utterance_ms=now;}
    // Server-side events need a unique event_id per message; a repeated literal
    // id makes the service answer COMMON_ERROR and close the session.
    append(json,&used,sizeof(json),"{\"event_id\":\"a");
    append_decimal(json,&used,sizeof(json),++client.event_sequence);
    append(json,&used,sizeof(json),"\",\"type\":\"input_audio_buffer.append\",\"audio\":\"");
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(uint32_t i=0;i<bytes;i+=3u) {
        uint32_t left=bytes-i;
        uint32_t v=((uint32_t)client.pcm[i]<<16);
        if(left>1u) v|=(uint32_t)client.pcm[i+1]<<8;
        if(left>2u) v|=(uint32_t)client.pcm[i+2];
        if(used+4u>=sizeof(json)) break;
        json[used++]=table[(v>>18)&63u];json[used++]=table[(v>>12)&63u];
        json[used++]=left>1u?table[(v>>6)&63u]:'=';
        json[used++]=left>2u?table[v&63u]:'=';
    }
    append(json,&used,sizeof(json),"\"}");
    client.tx_len=used;++client.tx_frames;
    for(uint32_t i=0;i<sizeof(client.tx_head)-1u && i<used;++i) client.tx_head[i]=json[i];
    client.tx_head[used<sizeof(client.tx_head)-1u?used:sizeof(client.tx_head)-1u]=0;
    if(send_json(json,used)) {
        /* The stream position advances by the audio actually queued for upload
           (16 kHz, 16-bit mono); its wall time is what T1 is measured against. */
        client.audio_ms+=(bytes*1000u)/32000u;
        client.hist[client.hist_pos%AUDIO_HISTORY].audio=client.audio_ms;
        client.hist[client.hist_pos%AUDIO_HISTORY].wall=now;
        ++client.hist_pos;
    }
}

void cloud_client_tick(uint32_t now) {
    client.now_ms=now;
    if(client.phase==CLOUD_WAITING_UPDATE && client.update_pending) {
        static const char update[] =
            "{\"event_id\":\"session_config\",\"type\":\"session.update\","
            "\"session\":{\"audio\":{\"input\":{\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":500}}},\"translation\":{\"language\":\"zh\"},\"output_modalities\":[\"text\"]}}";
        client.update_pending=send_json(update,(uint32_t)sizeof(update)-1u)?0u:1u;
    }
    if(client.phase!=CLOUD_STREAMING) return;
    // The PL decimates the 48 kHz codec stream to 16 kHz and buffers it; drain
    // in bursts. The bound keeps one service callback short, and 256 samples
    // (16 ms) per call is more than the ~16 that arrive per millisecond.
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>256u) count=256u;
    while(count--) {
        uint32_t raw=audio_regs32(AUDIO_FIFO_DATA);
        client.pcm[client.pcm_bytes++]=(uint8_t)(raw&0xffu);
        client.pcm[client.pcm_bytes++]=(uint8_t)((raw>>8)&0xffu);
        if(client.pcm_bytes>=PCM_CHUNK) send_pcm(now);
    }
}

// Called from cloud_live.c when a parsed event yields translation text. Record
// the delay from when the utterance's first audio was captured to the first
// Chinese text event of the response: the measured first-character delay.
void cloud_client_translation(uint32_t now) {
    (void)now;
    if(!client.utterance_started || client.utterance_reported) return;
    uint32_t base=client.utterance_wall_valid?client.utterance_wall:client.utterance_ms;
    uint32_t ms=client.now_ms-base;
    client.latency_ms=ms;
    client.utterance_reported=1u;
    audio_regs32(AUDIO_LATENCY_REG)=(1u<<16)|(ms>9999u?9999u:ms);
}

// A completed response ends the measurement window; the next uploaded packet
// starts a fresh first-character measurement.
void cloud_client_response_done(void) {
    client.utterance_started=0;
    client.utterance_reported=0;
    client.utterance_wall_valid=0;
}
