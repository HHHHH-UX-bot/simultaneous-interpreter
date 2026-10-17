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
#include <string.h>

#define AUDIO_SAMPLE   (*(volatile uint32_t *)0x43c20080u) // [16]=toggle, [15:0]=sample
#define AUDIO_COUNT    (*(volatile uint32_t *)0x43c20084u)
#define audio_regs32(off) (*(volatile uint32_t *)(0x43c20000u + (off)))

// Latency register consumed by the HDMI overlay (audio_regs offset 0x88).
#define AUDIO_LATENCY_REG 0x88u

__attribute__((section(".cloud_config"),used)) volatile struct cloud_client_config cloud_client_config;
int cloud_live_message(const char *json,uint32_t size,void *user);
static struct cloud_client_state client;

static void append(char *out,uint32_t *used,uint32_t limit,const char *text) {
    while(*text && *used<limit) out[(*used)++]=*text++;
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

static void send_json(const char *json,uint32_t size) {
    static uint8_t frame[2048];
    uint32_t used=build_frame(frame,sizeof(frame),(const uint8_t *)json,size);
    if(used) network_send(frame,used);
}

static void send_session_update(void) {
    static const char update[] =
        "{\"event_id\":\"session_config\",\"type\":\"session.update\","
        "\"session\":{\"translation\":{\"language\":\"zh\"},\"output_modalities\":[\"text\"]}}";
    send_json(update,(uint32_t)sizeof(update)-1u);
    client.phase=CLOUD_STREAMING;
    client.session_sent=1u;
}

static void send_handshake(void) {
    static char request[768];uint32_t used=0;
    append(request,&used,sizeof(request),"GET /api-ws/v1/realtime?model=");
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
}

void cloud_client_start(void) {
    if(client.phase!=CLOUD_IDLE) return;
    cloud_wire_init(cloud_live_message,0);   // server frames -> cloud_live mapping
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
            send_session_update();
        }
        return;
    }
    if(client.phase==CLOUD_STREAMING || client.phase==CLOUD_WAITING_SESSION) {
        cloud_wire_byte(byte);
    }
}

void cloud_client_tick(uint32_t now) {
    if(client.phase!=CLOUD_STREAMING) return;
    // 48 kHz -> 16 kHz: keep every third captured frame.
    uint32_t raw=AUDIO_SAMPLE;
    uint32_t toggle=(raw>>16)&1u;
    if(toggle==client.sample_toggle) return;
    client.sample_toggle=toggle;
    if(++client.decimate<3u) return;
    client.decimate=0;
    client.pcm[client.pcm_bytes++]=(uint8_t)(raw&0xffu);
    client.pcm[client.pcm_bytes++]=(uint8_t)((raw>>8)&0xffu);
    if(client.pcm_bytes<640u) return;
    // 20 ms (320 samples) per append; base64 into the JSON body.
    static char json[1200];uint32_t used=0;
    client.pcm_bytes=0;
    append(json,&used,sizeof(json),"{\"event_id\":\"audio\",\"type\":\"input_audio_buffer.append\",\"audio\":\"");
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(uint32_t i=0;i<640u;i+=3u) {
        uint32_t v=((uint32_t)client.pcm[i]<<16)|((uint32_t)client.pcm[i+1]<<8)|client.pcm[i+2];
        if(used+4u>=sizeof(json)) break;
        json[used++]=table[(v>>18)&63u];json[used++]=table[(v>>12)&63u];
        json[used++]=table[(v>>6)&63u];json[used++]=table[v&63u];
    }
    append(json,&used,sizeof(json),"\"]}");
    send_json(json,used);
    if(!client.utterance_started) {client.utterance_started=1u;client.utterance_ms=now;}
    client.last_tick=now;
}

// Called from cloud_live.c when a parsed event yields translation text.
void cloud_client_translation(uint32_t now) {
    (void)now;
    if(!client.utterance_started) return;
    uint32_t ms=client.last_tick-client.utterance_ms;
    client.latency_ms=ms;
    audio_regs32(AUDIO_LATENCY_REG)=ms>9999u?9999u:ms;
}
