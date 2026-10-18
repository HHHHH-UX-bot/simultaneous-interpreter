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
#ifdef Z7_OPUS
#include "opus_codec.h"
#include "ogg_opus.h"
#endif
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

/* Opus path: qwen3.5-livetranslate with Opus input fits the ESP TLS uplink.
   PCM path: the previous qwen3.8 45 ms chunk stream (kept as the fallback). */
#ifdef Z7_OPUS
static const char session_update_json[] =
    "{\"event_id\":\"session_config\",\"type\":\"session.update\","
    "\"session\":{\"modalities\":[\"text\"],\"sample_rate\":16000,"
    "\"input_audio_format\":\"opus\",\"output_audio_format\":\"pcm\","
    "\"input_audio_transcription\":{\"model\":\"qwen3-asr-flash-realtime\",\"language\":\"zh\"},"
    "\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":500},"
    "\"translation\":{\"language\":\"zh\",\"same_language_skip_options\":{\"skip_text\":false,\"skip_audio\":true}}}}";
#define OPUS_FRAME_BYTES 640u
#define OPUS_QUEUE 24u
#define OPUS_BATCH 10u
#define OGG_SERIAL 0x5A374E32u
static uint8_t opus_packet[OPUS_QUEUE][128];
static uint8_t opus_packet_len[OPUS_QUEUE];
static uint32_t opus_head,opus_tail;
#else
static const char session_update_json[] =
    "{\"event_id\":\"session_config\",\"type\":\"session.update\","
    "\"session\":{\"audio\":{\"input\":{\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":500}}},\"translation\":{\"language\":\"zh\"},\"output_modalities\":[\"text\"]}}";
#endif

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
    client.phase=CLOUD_WAITING_UPDATE;
    client.session_sent=1u;
    // The previous CIPSEND may still be finishing; keep retrying until the
    // network channel accepts the frame instead of silently losing the config.
    client.update_pending=send_json(session_update_json,(uint32_t)sizeof(session_update_json)-1u)?0u:1u;
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
#ifdef Z7_OPUS
    client.opus_ready=(uint32_t)opus_codec_init();
#endif
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
#ifdef Z7_OPUS
    opus_head=opus_tail=0;
    client.ogg_header_step=0;
    client.ogg_seq=2;          /* OpusHead and OpusTags occupy pages 0 and 1 */
    client.ogg_granule=0;
#endif
}

/* One WebSocket append carries 45 ms (720 samples / 1440 B PCM) instead of the
   20 ms the protocol's PC reference used. At the ESP's per-CIPSEND round-trip
   the smaller packet cannot sustain 16 kHz real time; the larger one can, and
   the cloud accepts any append length. This is the PCM fallback path. */
#ifndef Z7_OPUS
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
#else
/* Opus path: encode each 20 ms frame, package OPUS_BATCH packets (200 ms) into
   one Ogg page and send that page as one input_audio_buffer.append. The
   qwen3.5 websocket endpoint expects an Ogg Opus stream, so the OpusHead and
   OpusTags pages must be sent before the first audio page. */
static int send_append(const uint8_t *bytes,uint32_t length) {
    static char json[2100];
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t used=0;
    append(json,&used,sizeof(json),"{\"event_id\":\"a");
    append_decimal(json,&used,sizeof(json),++client.event_sequence);
    append(json,&used,sizeof(json),"\",\"type\":\"input_audio_buffer.append\",\"audio\":\"");
    for(uint32_t i=0;i<length;i+=3u) {
        uint32_t left=length-i;
        uint32_t v=((uint32_t)bytes[i]<<16);
        if(left>1u) v|=(uint32_t)bytes[i+1]<<8;
        if(left>2u) v|=(uint32_t)bytes[i+2];
        if(used+4u>=sizeof(json)) return 0;
        json[used++]=table[(v>>18)&63u];json[used++]=table[(v>>12)&63u];
        json[used++]=left>1u?table[(v>>6)&63u]:'=';
        json[used++]=left>2u?table[v&63u]:'=';
    }
    append(json,&used,sizeof(json),"\"}");
    return send_json(json,used);
}

static int send_ogg_header(void) {
    static uint8_t page[128];
    uint32_t length=client.ogg_header_step==0u?ogg_opus_head(page,sizeof(page),OGG_SERIAL):
                                                ogg_opus_tags(page,sizeof(page),OGG_SERIAL);
    if(!length || !send_append(page,length)) return 0;
    ++client.ogg_header_step;
    return 1;
}

static int send_ogg_page(uint32_t now) {
    static uint8_t page[2048];
    const uint8_t *packets[OPUS_BATCH];
    uint8_t lengths[OPUS_BATCH];
    uint32_t count=opus_tail-opus_head;
    if(count>OPUS_BATCH) count=OPUS_BATCH;
    for(uint32_t i=0;i<count;++i) {
        uint32_t slot=(opus_head+i)%OPUS_QUEUE;
        packets[i]=opus_packet[slot];
        lengths[i]=opus_packet_len[slot];
    }
    uint32_t granule=client.ogg_granule+320u*count;   /* 16 kHz samples */
    uint32_t length=ogg_opus_page(page,sizeof(page),0,granule,OGG_SERIAL,client.ogg_seq,
                                  packets,lengths,count);
    if(!length || !send_append(page,length)) return 0;
    opus_head+=count;
    client.ogg_granule=granule;
    ++client.ogg_seq;
    client.audio_ms+=20u*count;
    client.hist[client.hist_pos%AUDIO_HISTORY].audio=client.audio_ms;
    client.hist[client.hist_pos%AUDIO_HISTORY].wall=now;
    ++client.hist_pos;
    client.opus_frames+=count;client.opus_bytes+=length;
    return 1;
}
#endif

void cloud_client_tick(uint32_t now) {
    client.now_ms=now;
    if(client.phase==CLOUD_WAITING_UPDATE && client.update_pending) {
        client.update_pending=send_json(session_update_json,(uint32_t)sizeof(session_update_json)-1u)?0u:1u;
    }
    if(client.phase!=CLOUD_STREAMING) return;
    // The PL decimates the 48 kHz codec stream to 16 kHz and buffers it; drain
    // in bursts. The bound keeps one service callback short, and 256 samples
    // (16 ms) per call is more than the ~16 that arrive per millisecond.
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>256u) count=256u;
#ifdef Z7_OPUS
    while(count--) {
        uint32_t raw=audio_regs32(AUDIO_FIFO_DATA);
        client.pcm[client.pcm_bytes++]=(uint8_t)(raw&0xffu);
        client.pcm[client.pcm_bytes++]=(uint8_t)((raw>>8)&0xffu);
        if(client.pcm_bytes>=OPUS_FRAME_BYTES) {
            int16_t samples[320];
            for(uint32_t i=0;i<320u;++i)
                samples[i]=(int16_t)((uint16_t)client.pcm[2u*i]|((uint16_t)client.pcm[2u*i+1u]<<8));
            uint8_t packet[128];
            int length=opus_codec_encode(samples,packet,sizeof(packet));
            client.pcm_bytes=0;
            if(length>0) {
                if(length>(int)sizeof(opus_packet[0])) length=(int)sizeof(opus_packet[0]);
                if(opus_tail-opus_head>=OPUS_QUEUE) ++opus_head;
                uint32_t slot=opus_tail%OPUS_QUEUE;
                for(int i=0;i<length;++i) opus_packet[slot][i]=packet[i];
                opus_packet_len[slot]=(uint8_t)length;
                ++opus_tail;
            }
        }
    }
    if(client.ogg_header_step<2u) {
        while(client.ogg_header_step<2u && send_ogg_header()) {}
        return;
    }
    while(opus_tail-opus_head>=OPUS_BATCH) { if(!send_ogg_page(now)) break; }
#else
    while(count--) {
        uint32_t raw=audio_regs32(AUDIO_FIFO_DATA);
        client.pcm[client.pcm_bytes++]=(uint8_t)(raw&0xffu);
        client.pcm[client.pcm_bytes++]=(uint8_t)((raw>>8)&0xffu);
        if(client.pcm_bytes>=PCM_CHUNK) send_pcm(now);
    }
#endif
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
