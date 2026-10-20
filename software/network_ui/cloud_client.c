// ARM-side realtime cloud client: microphone PCM -> Bailian livetranslate WSS
// over the ESP8266 TLS link, and Chinese subtitle events back to the UI.
//
// The TLS link itself is owned by network.c (action 3 / phase 8). This module
// assumes an established multiplexed link and speaks the WebSocket + realtime
// JSON protocol on top of it. The API key never lives in the ELF: it is held in
// the .cloud_config OCM window, written by the host before start.
#include "cloud_client.h"
#ifdef Z7_ARM_TLS
#include "arm_tls.h"
/* 云端 HTTP/WebSocket 只进入已认证的 ARM TLS，绝不能直发 TCP。 */
#define network_send arm_tls_send
#define network_stream_attach arm_tls_attach
#endif
#include "network.h"
#include "cloud_wire.h"
#include "cloud_events.h"
#include "cloud_live.h"
#include "speech_playback.h"
#ifdef Z7_OPUS
#include "opus_codec.h"
#include "../ps_bringup/cache.h"
#include "ogg_opus.h"
#endif
#include <string.h>
static struct cloud_client_state client;
#ifdef Z7_TLS_PASSTHROUGH
static uint32_t last_rx_ms,ping_sent,ping_ms;
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t keepalive_diag[5];
static void keepalive_clean(void) {cache_clean_range((const void *)keepalive_diag,sizeof(keepalive_diag));}
#endif

/* Microphone debug dump: the 16 kHz/16-bit PCM that is actually encoded and
   uploaded is mirrored into a fixed DDR ring (header at 0x01D3F000, 768 KiB at
   0x01D40000). On the ASR final the ring freezes (~24 s window around the last
   utterance) so the host can read it over JTAG and save a WAV for listening. */
#define AUDIO_DUMP_MAGIC 0x31445541u
#define AUDIO_DUMP_HEADER ((volatile uint32_t *)0x01D3F000u)
#define AUDIO_DUMP_DATA ((uint8_t *)0x01D40000u)
#define AUDIO_DUMP_CAPACITY 0x000C0000u
static void dump_pcm(const int16_t *samples,uint32_t count) {
    volatile uint32_t *header=AUDIO_DUMP_HEADER;
    if(header[0]!=AUDIO_DUMP_MAGIC) {
        header[0]=AUDIO_DUMP_MAGIC;header[1]=0;header[2]=AUDIO_DUMP_CAPACITY;header[3]=16000u;
        header[4]=header[5]=0;header[6]=0;
    }
    if(header[6]) return;
    uint32_t bytes=count*2u,position=header[1];
    uint32_t at=position%AUDIO_DUMP_CAPACITY;
    uint32_t first=AUDIO_DUMP_CAPACITY-at;if(first>bytes) first=bytes;
    memcpy(AUDIO_DUMP_DATA+at,samples,first);
    cache_clean_range(AUDIO_DUMP_DATA+at,first);
    if(bytes>first) {
        memcpy(AUDIO_DUMP_DATA,(const uint8_t *)samples+first,bytes-first);
        cache_clean_range(AUDIO_DUMP_DATA,bytes-first);
    }
    header[1]=position+bytes;
}
static uint32_t dump_deadline;
struct cloud_final_diag {
    uint32_t magic,stops,finals,last_tail_ms,max_tail_ms,unmatched_finals;
    uint32_t stream_start_ms,final_ms[8];
};
__attribute__((section(".cloud_runtime"),used)) static volatile struct cloud_final_diag final_diag;
static uint32_t final_stop_ms,final_pending_stops;
static void final_diag_clean(void) {
    cache_clean_range((const void *)&final_diag,(unsigned)sizeof(final_diag));
}
void cloud_client_speech_stopped(void) {
    /* The oldest outstanding stop is the conservative reference if the service
       delivers several stops before any final. It is not a capture-end clock. */
    if(!final_pending_stops) final_stop_ms=client.now_ms;
    ++final_pending_stops;++final_diag.stops;final_diag_clean();
}
void cloud_client_asr_final(void) {
    ++final_diag.finals;
    final_diag.final_ms[(final_diag.finals-1u)%8u]=client.now_ms;
    if(final_pending_stops) {
        uint32_t elapsed=client.now_ms-final_stop_ms;
        final_diag.last_tail_ms=elapsed;
        if(elapsed>final_diag.max_tail_ms) final_diag.max_tail_ms=elapsed;
        --final_pending_stops;
    } else ++final_diag.unmatched_finals;
    final_diag_clean();
}
void cloud_client_capture_freeze(void) {
    if(AUDIO_DUMP_HEADER[0]==AUDIO_DUMP_MAGIC && !AUDIO_DUMP_HEADER[6]) {
        AUDIO_DUMP_HEADER[5]=AUDIO_DUMP_HEADER[1];
        AUDIO_DUMP_HEADER[6]=1;dump_deadline=0;
        cache_clean_range((const void *)AUDIO_DUMP_HEADER,28);
    }
}

#define AUDIO_SAMPLE   (*(volatile uint32_t *)0x43c20080u) // [16]=toggle, [15:0]=sample
#define AUDIO_COUNT    (*(volatile uint32_t *)0x43c20084u)
#define AUDIO_FIFO_DATA     0x8cu  // 16 kHz sample, reading pops it
#define AUDIO_FIFO_COUNT    0x90u  // samples buffered by the PL
#define AUDIO_FIFO_OVERFLOWS 0x94u // dropped-at-full counter (write clears)
#define audio_regs32(off) (*(volatile uint32_t *)(0x43c20000u + (off)))

// Latency register consumed by the HDMI overlay (audio_regs offset 0x88).
#define AUDIO_LATENCY_REG 0x88u

__attribute__((section(".cloud_config"),used)) volatile struct cloud_client_config cloud_client_config;
int cloud_live_message(const char *json,uint32_t size,void *user);
static void pcm_flush(void);
#ifdef Z7_SELFTEST_AUDIO
static uint32_t selftest_next,selftest_ms;
#endif

/* Opus path: qwen3.5-livetranslate with Opus input fits the ESP TLS uplink.
   PCM path: the previous qwen3.8 45 ms chunk stream (kept as the fallback). */
#ifdef Z7_OPUS
#ifdef Z7_TRANSLATE_EN
#define CLOUD_TARGET_LANGUAGE "en"
#else
#define CLOUD_TARGET_LANGUAGE "zh"
#endif
#ifdef Z7_TRANSLATE_AUDIO
#define CLOUD_OUTPUT_MODALITIES "\"text\",\"audio\""
#define CLOUD_AUDIO_SKIP "false"
#define CLOUD_OUTPUT_VOICE "\"voice\":\"Tina\","
#define CLOUD_TURN_OPTIONS ",\"create_response\":true,\"interrupt_response\":false"
#else
#define CLOUD_OUTPUT_MODALITIES "\"text\""
#define CLOUD_AUDIO_SKIP "true"
#define CLOUD_OUTPUT_VOICE ""
#define CLOUD_TURN_OPTIONS ""
#endif
static const char session_update_json[] =
    "{\"event_id\":\"session_config\",\"type\":\"session.update\","
    "\"session\":{\"modalities\":[" CLOUD_OUTPUT_MODALITIES "]," CLOUD_OUTPUT_VOICE "\"sample_rate\":16000,"
    "\"input_audio_format\":\"opus\",\"output_audio_format\":\"pcm\","
    "\"input_audio_transcription\":{\"model\":\"qwen3-asr-flash-realtime\",\"language\":\"zh\"},"
    "\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.12,\"silence_duration_ms\":400" CLOUD_TURN_OPTIONS "},"
    "\"translation\":{\"language\":\"" CLOUD_TARGET_LANGUAGE "\",\"same_language_skip_options\":{\"skip_text\":false,\"skip_audio\":" CLOUD_AUDIO_SKIP "}}}}";
#define OPUS_FRAME_BYTES 640u
/* 128 packets = 2.56 s of headroom: a single CIPSEND was measured stalling for
   1325 ms, which is longer than the old 48-packet queue could absorb. */
#ifdef Z7_TRANSLATE_AUDIO
#define OPUS_QUEUE 512u /* Keep capturing across translated-PCM receive bursts. */
#else
#define OPUS_QUEUE 128u
#endif
#define OPUS_BATCH 10u            /* normal full-page trigger: 200 ms */
/* Recover from a long CIPSEND stall faster than real time. Twenty 60-byte CBR
   packets + Ogg/base64/JSON/WebSocket overhead fit the 2048-byte send window.
   The normal 150 ms/10-packet flush trigger is unchanged. */
#ifdef Z7_TRANSLATE_AUDIO
#define OPUS_MAX_BATCH 10u /* Keep SSL UART sends below 1 KiB while receiving PCM. */
#else
#define OPUS_MAX_BATCH 20u
#endif
/* Measured on this link: one CIPSEND costs 100-1300 ms, so a page every 60 ms
   (16 pages/s) is unpayable and the queue overflows. 150 ms is the floor that
   keeps one transaction in flight at a time; the controller may raise it. */
#define OPUS_FLUSH_MIN_MS 150u
#define OPUS_FLUSH_MAX_MS 300u    /* the old fixed 10-packet page */
#define OGG_SERIAL 0x5A374E32u
static uint8_t opus_packet[OPUS_QUEUE][128];
static uint8_t opus_packet_len[OPUS_QUEUE];
static uint32_t opus_packet_ms[OPUS_QUEUE];   /* queue time of each packet */
static uint32_t opus_head,opus_tail;
/* A page only reaches the ESP once the previous CIPSEND transaction finished,
   so a page's age is bounded by both the fill time and the link round trip.
   opus_flush_ms starts at the latency floor and only grows while a send
   completes leaving a real backlog, i.e. while the link cannot keep up. */
static uint32_t opus_flush_ms=OPUS_FLUSH_MIN_MS;
static uint32_t opus_wait_ms,opus_age_max_ms,opus_wait_max_ms;
static uint32_t opus_wait_started,opus_waiting;

#ifdef Z7_ASR_SUBTITLE
/* The dedicated ASR endpoint has a different session schema: translation and
   modalities must not be sent to it. Keep LiveTranslate available for bilingual
   work; select ASR only for its exact configured model path. */
static const char asr_model_path[]="/api-ws/v1/realtime?model=qwen3-asr-flash-realtime";
static const char asr_session_update_json[] =
    "{\"event_id\":\"session_config\",\"type\":\"session.update\","
    "\"session\":{\"sample_rate\":16000,\"input_audio_format\":\"opus\","
    "\"input_audio_transcription\":{\"language\":\"zh\"},"
    "\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.12,\"silence_duration_ms\":400}}}";
#endif

/* Upload counters the host can read without guessing. They live in cached DDR,
   so every update cleans the line: JTAG must see the same values the CPU saw,
   otherwise a dropped-frame hole is invisible. */
struct cloud_upload_diag {
    uint32_t magic,drops,pages,queue_max,fifo_max,stall_max_ms,speech_started;
};
#define CLOUD_UPLOAD_MAGIC 0x31475544u   /* 'DUG1' */
__attribute__((section(".cloud_runtime"),used)) static volatile struct cloud_upload_diag upload_diag;
static void upload_diag_clean(void) {
#ifdef Z7_MMU_CACHE
    cache_clean_range((const void *)&upload_diag,(unsigned)sizeof(upload_diag));
#endif
}
static void upload_diag_init(void) {
    upload_diag.magic=CLOUD_UPLOAD_MAGIC;
    upload_diag.drops=upload_diag.pages=upload_diag.queue_max=upload_diag.fifo_max=0;
    upload_diag.stall_max_ms=upload_diag.speech_started=0;
    upload_diag_clean();
}
#else
static const char session_update_json[] =
    "{\"event_id\":\"session_config\",\"type\":\"session.update\","
    "\"session\":{\"audio\":{\"input\":{\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.12,\"silence_duration_ms\":500}}},\"translation\":{\"language\":\"zh\"},\"output_modalities\":[\"text\"]}}";
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

static int send_session_config(void) {
#if defined(Z7_OPUS) && defined(Z7_ASR_SUBTITLE)
    uint32_t i=0;
    while(asr_model_path[i] && cloud_client_config.path[i]==asr_model_path[i]) ++i;
    if(!asr_model_path[i] && !cloud_client_config.path[i])
        return send_json(asr_session_update_json,(uint32_t)sizeof(asr_session_update_json)-1u);
#endif
    return send_json(session_update_json,(uint32_t)sizeof(session_update_json)-1u);
}

static void send_session_update(void) {
    client.phase=CLOUD_WAITING_UPDATE;
    client.session_sent=1u;
    // The previous CIPSEND may still be finishing; keep retrying until the
    // network channel accepts the frame instead of silently losing the config.
    client.update_pending=send_session_config()?0u:1u;
    client.update_ms=client.now_ms;client.update_retries=0;
}

/* session.created is the first server event; only then may the update be sent
   (sending it earlier makes the service answer with COMMON_ERROR). */
void cloud_client_session_created(void) {
    if(client.phase!=CLOUD_WAITING_SESSION) return;
    send_session_update();
}

void cloud_client_session_updated(void) {
    if(client.phase!=CLOUD_WAITING_UPDATE) return;
    client.update_retries=0;
    pcm_flush();
    client.phase=CLOUD_STREAMING;
    final_diag.stream_start_ms=client.now_ms;final_diag_clean();
    client.utterance_started=0;
    client.utterance_reported=0;
}

/* The server's audio_start_ms is not a reliable wall-clock mapping: it counts
   its own accepted audio and drops long silences, so a capture-time estimate
   drifts. T1 is therefore measured from the moment the speech-start event
   arrives (server VAD trigger) to the first recognised characters. */
volatile uint32_t diag_speech_started,diag_peak;
void cloud_client_speech_started(uint32_t audio_start_ms) {
    (void)audio_start_ms;
    ++diag_speech_started;
    ++upload_diag.speech_started;
    upload_diag_clean();
    if(AUDIO_DUMP_HEADER[0]==AUDIO_DUMP_MAGIC) {
        uint32_t total=AUDIO_DUMP_HEADER[1];
        AUDIO_DUMP_HEADER[4]=total>96000u?total-96000u:0u;   /* 3 s lead-in */
        AUDIO_DUMP_HEADER[6]=0;dump_deadline=client.now_ms+9000u;
    }
    client.utterance_started=1;
    client.utterance_reported=0;
    client.utterance_ms=client.now_ms;
}

/* Stop appending after a cloud error or close. A dead session must not leave a
   stale T1 number on the display, so the overlay goes back to dashes. */
void cloud_client_stop(void) {
    client.phase=CLOUD_IDLE;
    client.pcm_bytes=0;
    audio_regs32(AUDIO_LATENCY_REG)=0;
    speech_playback_disconnect();
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
#ifdef Z7_TLS_PASSTHROUGH
    for(uint32_t i=0;i<5u;++i) keepalive_diag[i]=0;
    keepalive_diag[0]=0x31414c4bu;last_rx_ms=ping_sent=ping_ms=0;keepalive_clean();
#endif
    memset(&client,0,sizeof(client));
    client.phase=CLOUD_IDLE;
    memset((void *)&final_diag,0,sizeof(final_diag));
    final_diag.magic=0x314e4946u;final_stop_ms=final_pending_stops=0;final_diag_clean();
    upload_diag_init();
#ifdef Z7_OPUS
    client.opus_ready=(uint32_t)opus_codec_init();
#endif
    audio_regs32(AUDIO_LATENCY_REG)=0;
    pcm_flush();
}
void cloud_client_start(uint32_t now) {
    if(client.phase!=CLOUD_IDLE) return;
#ifdef Z7_TLS_PASSTHROUGH
    last_rx_ms=now;ping_sent=0;
#endif
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
#ifdef Z7_TLS_PASSTHROUGH
    last_rx_ms=client.now_ms;keepalive_diag[3]=last_rx_ms;
#endif
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
#ifdef Z7_SELFTEST_AUDIO
    /* Reconnect starts a new real-time input stream. Never enqueue the seconds
       spent offline as one burst of historical test audio. */
    selftest_next=selftest_ms=0;
#endif
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>1024u) count=1024u;
    while(count--) (void)audio_regs32(AUDIO_FIFO_DATA);
    audio_regs32(AUDIO_FIFO_OVERFLOWS)=0;
    client.pcm_bytes=0;
#ifdef Z7_OPUS
    opus_head=opus_tail=0;
    opus_flush_ms=OPUS_FLUSH_MIN_MS;
    opus_wait_ms=opus_age_max_ms=opus_wait_max_ms=0;
    opus_wait_started=opus_waiting=0;
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

static int send_ogg_page(void) {
    static uint8_t page[2048];
    const uint8_t *packets[OPUS_MAX_BATCH];
    uint8_t lengths[OPUS_MAX_BATCH];
    uint32_t count=opus_tail-opus_head;
    if(count>OPUS_MAX_BATCH) count=OPUS_MAX_BATCH;
    for(uint32_t i=0;i<count;++i) {
        uint32_t slot=(opus_head+i)%OPUS_QUEUE;
        packets[i]=opus_packet[slot];
        lengths[i]=opus_packet_len[slot];
    }
    /* RFC 7845 section 4: Ogg Opus granules always count 48 kHz samples,
       even though this encoder consumes 16 kHz PCM (320 samples / 20 ms). */
    uint32_t granule=client.ogg_granule+960u*count;
    uint32_t length=ogg_opus_page(page,sizeof(page),0,granule,OGG_SERIAL,client.ogg_seq,
                                  packets,lengths,count);
    if(!length || !send_append(page,length)) return 0;
    opus_head+=count;
    client.ogg_granule=granule;
    ++client.ogg_seq;
    client.opus_frames+=count;client.opus_bytes+=length;
    ++upload_diag.pages;
    upload_diag_clean();
    return 1;
}
#endif

#ifdef Z7_OPUS
static void queue_opus_frame(const int16_t *samples) {
    dump_pcm(samples,320u);
    for(uint32_t i=0;i<320u;++i) {int32_t v=samples[i];if(v<0) v=-v;if((uint32_t)v>diag_peak) diag_peak=(uint32_t)v;}
    uint8_t packet[128];
    int length=opus_codec_encode(samples,packet,sizeof(packet));
    if(length<=0) return;
    if(length>(int)sizeof(opus_packet[0])) length=(int)sizeof(opus_packet[0]);
    if(opus_tail-opus_head>=OPUS_QUEUE) {
        ++opus_head;
        ++upload_diag.drops;
        upload_diag_clean();
    }
    uint32_t depth=opus_tail-opus_head+1u;
    if(depth>upload_diag.queue_max) {upload_diag.queue_max=depth;upload_diag_clean();}
    uint32_t slot=opus_tail%OPUS_QUEUE;
    for(int i=0;i<length;++i) opus_packet[slot][i]=packet[i];
    opus_packet_len[slot]=(uint8_t)length;
    opus_packet_ms[slot]=client.now_ms;
    ++opus_tail;
}
#ifdef Z7_SELFTEST_AUDIO
/* Generated by the build: Chinese TTS speech followed by 30 s of silence in a
   loop, so the whole board pipeline can be exercised without the microphone. */
extern const int16_t selftest_pcm[];
extern const uint32_t selftest_samples;
extern const uint32_t selftest_speech_ms;
#endif
#endif

void cloud_client_tick(uint32_t now) {
    client.now_ms=now;
#ifdef Z7_TLS_PASSTHROUGH
    if(client.phase!=CLOUD_IDLE) {
        if(ping_sent && (int32_t)(last_rx_ms-ping_ms)>=0) ping_sent=0;
        if(ping_sent && now-ping_ms>=5000u) {
            ++keepalive_diag[2];keepalive_clean();cloud_client_stop();network_stream_failed();return;
        }
        if(!ping_sent && now-last_rx_ms>=15000u && client.phase!=CLOUD_STREAMING) {
            ++keepalive_diag[2];keepalive_clean();cloud_client_stop();network_stream_failed();return;
        }
        if(!ping_sent && now-last_rx_ms>=15000u && client.phase==CLOUD_STREAMING) {
            uint8_t ping[14];uint32_t size=build_frame(ping,sizeof(ping),(const uint8_t *)"",0);
            ping[0]=0x89u;
            if(size && network_send(ping,size)) {
                ping_sent=1;ping_ms=now;++keepalive_diag[1];keepalive_diag[4]=now;keepalive_clean();
            }
        }
    }
#endif
    if(dump_deadline && (int32_t)(now-dump_deadline)>=0) cloud_client_capture_freeze();
    if(client.phase==CLOUD_WAITING_UPDATE) {
        if(client.update_pending) {
            client.update_pending=send_session_config()?0u:1u;
            client.update_ms=now;
        } else if((int32_t)(now-client.update_ms)>3000 && client.update_retries<5u) {
            /* A dropped CIPSEND can silently lose the session.update; resend it. */
            ++client.update_retries;
            client.update_pending=send_session_config()?0u:1u;
            client.update_ms=now;
        }
    }
    if(client.phase!=CLOUD_STREAMING) return;
    // The PL decimates the 48 kHz codec stream to 16 kHz and buffers it; drain
    // in bursts. The bound keeps one service callback short, and 256 samples
    // (16 ms) per call is more than the ~16 that arrive per millisecond.
#ifdef Z7_OPUS
#ifdef Z7_SELFTEST_AUDIO
    uint32_t discard=audio_regs32(AUDIO_FIFO_COUNT);
    if(discard>256u) discard=256u;
    while(discard--) (void)audio_regs32(AUDIO_FIFO_DATA);
    if(!selftest_next) selftest_next=now;
    while((int32_t)(now-selftest_next)>=0) {
        int16_t frame[320];
        if(selftest_ms<selftest_speech_ms) {
            uint32_t at=selftest_ms*16u;
            for(uint32_t i=0;i<320u;++i) frame[i]=(at+i<selftest_samples)?selftest_pcm[at+i]:0;
        } else {
            for(uint32_t i=0;i<320u;++i) frame[i]=0;
        }
        queue_opus_frame(frame);
        selftest_ms+=20u;
        if(selftest_ms>=selftest_speech_ms+30000u) selftest_ms=0;
        selftest_next+=20u;
    }
#else
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>upload_diag.fifo_max) {upload_diag.fifo_max=count;upload_diag_clean();}
    if(count>256u) count=256u;
    while(count--) {
        uint32_t raw=audio_regs32(AUDIO_FIFO_DATA);
        client.pcm[client.pcm_bytes++]=(uint8_t)(raw&0xffu);
        client.pcm[client.pcm_bytes++]=(uint8_t)((raw>>8)&0xffu);
        if(client.pcm_bytes>=OPUS_FRAME_BYTES) {
            int16_t samples[320];
            for(uint32_t i=0;i<320u;++i)
                samples[i]=(int16_t)((uint16_t)client.pcm[2u*i]|((uint16_t)client.pcm[2u*i+1u]<<8));
            client.pcm_bytes=0;
            queue_opus_frame(samples);
        }
    }
#endif
    if(client.ogg_header_step<2u) {
        while(client.ogg_header_step<2u && send_ogg_header()) {}
        return;
    }
    /* Flush the queue when its oldest packet reaches opus_flush_ms, or earlier
       when a full page is ready. The age bound is what keeps the upload from
       sitting on the speech onset: a fixed 10-packet page held the first
       syllable for up to 200 ms before the server could even run its VAD. */
    while(opus_head!=opus_tail) {
        uint32_t count=opus_tail-opus_head;
        uint32_t age=now-opus_packet_ms[opus_head%OPUS_QUEUE];
        if(count<OPUS_BATCH && age<opus_flush_ms) break;
        if(!send_ogg_page()) {
            /* The link still owns the previous CIPSEND; the page keeps ageing. */
            if(!opus_waiting) {opus_wait_started=now;opus_waiting=1;}
            opus_wait_ms=now-opus_wait_started;
            if(opus_wait_ms>opus_wait_max_ms) opus_wait_max_ms=opus_wait_ms;
            break;
        }
        if(opus_waiting) opus_wait_ms=now-opus_wait_started;
        if(age>opus_age_max_ms) opus_age_max_ms=age;
        if(opus_wait_ms>upload_diag.stall_max_ms) {
            upload_diag.stall_max_ms=opus_wait_ms;
            upload_diag_clean();
        }
        opus_wait_ms=0;
        opus_waiting=0;
        uint32_t left=opus_tail-opus_head;
        if(left>=6u) {
            opus_flush_ms+=20u;
            if(opus_flush_ms>OPUS_FLUSH_MAX_MS) opus_flush_ms=OPUS_FLUSH_MAX_MS;
        } else if(left<=1u && opus_flush_ms>OPUS_FLUSH_MIN_MS) {
            opus_flush_ms-=5u;
            if(opus_flush_ms<OPUS_FLUSH_MIN_MS) opus_flush_ms=OPUS_FLUSH_MIN_MS;
        }
        break;
    }
#else
    uint32_t count=audio_regs32(AUDIO_FIFO_COUNT);
    if(count>256u) count=256u;
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
    uint32_t base=client.utterance_ms;
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
}
