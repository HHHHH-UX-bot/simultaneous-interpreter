/* Exercise the real client/muxer/event/UI code with a stalled transport.
   Fixed MMIO pages are private Windows process memory, never board accesses. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define Z7_OPUS
#ifndef Z7_TRANSLATE_EN
#define Z7_ASR_SUBTITLE
#endif
#ifdef Z7_ARM_TLS
#include "../software/network_ui/network.h"
#endif
#include "../software/subtitle_ui/ui_render.h"
#include "../software/network_ui/cloud_client.c"
#include "../software/network_ui/cloud_live.c"
#ifdef Z7_ARM_TLS
#undef network_send
#undef network_stream_attach
#define test_send arm_tls_send
#define test_attach arm_tls_attach
#else
#define test_send network_send
#define test_attach network_stream_attach
#endif

volatile struct network_status network_info;
volatile struct stream_pilot_status stream_pilot;
static int blocked;
static unsigned transport_failures;
static unsigned checks;
#ifndef Z7_TRANSLATE_EN
static unsigned sent_pong;
#endif
#ifdef Z7_SELFTEST_AUDIO
const int16_t selftest_pcm[320]={0};
const uint32_t selftest_samples=320,selftest_speech_ms=20;
#endif
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"failed line %d\n",__LINE__);exit(1);}} while(0)
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
int opus_codec_init(void) {return 1;}
int opus_codec_encode(const int16_t *pcm,uint8_t *out,uint32_t n) {
    (void)pcm;if(n<60) return -1;
    memset(out,0,60);out[0]=0x48;return 60;
}
int test_attach(void (*receive)(uint8_t)) {(void)receive;return 1;}
void network_stream_failed(void) {++transport_failures;}
void network_stop(void) {}
void network_cloud_run(int run) {(void)run;}
void stream_pilot_set_receiver(void (*receive)(uint8_t)) {(void)receive;}
int test_send(const uint8_t *data,uint32_t n) {
#ifdef Z7_TRANSLATE_AUDIO
    CHECK(n<=1024); /* Speech-mode uploads stay within the small SSL window. */
#else
    CHECK(n<=2048);
#endif
    if(blocked) return 0;
#ifndef Z7_TRANSLATE_EN
    if(n && data[0]==0x8au) ++sent_pong;
#endif
    if(n && data[0]==0x81) {
        printf("FRAME ");for(uint32_t i=0;i<n;++i) printf("%02x",data[i]);puts("");
    }
    return 1;
}
static void queue(unsigned n,uint32_t now) {
    int16_t pcm[320]={0};client.now_ms=now;
    while(n--) queue_opus_frame(pcm);
}
int main(void) {
    CHECK(VirtualAlloc((void *)0x43c20000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=NULL);
    CHECK(VirtualAlloc((void *)0x01d30000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=NULL);
    AUDIO_DUMP_HEADER[0]=AUDIO_DUMP_MAGIC;AUDIO_DUMP_HEADER[6]=1;
    struct ui_state model;ui_init(&model);cloud_live_init(&model);
    const char live_path[]="/api-ws/v1/realtime?model=qwen3.5-livetranslate-flash-realtime";
#ifdef Z7_ASR_SUBTITLE
    memcpy((void *)cloud_client_config.path,asr_model_path,sizeof(asr_model_path));
#else
    memcpy((void *)cloud_client_config.path,live_path,sizeof(live_path));
#endif
    cloud_client_init();cloud_client_start(0);
    client.phase=CLOUD_WAITING_SESSION;
    blocked=1;cloud_client_session_created();CHECK(client.update_pending);
    cloud_client_tick(10);CHECK(client.update_pending);
    blocked=0;cloud_client_tick(11);CHECK(!client.update_pending);
    cloud_client_session_updated();CHECK(client.phase==CLOUD_STREAMING);
#ifdef Z7_SELFTEST_AUDIO
    cloud_client_tick(20);CHECK(opus_tail==1 && selftest_ms==20);
    cloud_client_tick(21);CHECK(opus_tail==1);
    cloud_client_stop();cloud_client_tick(99999);
    cloud_client_start(100000);client.phase=CLOUD_WAITING_UPDATE;cloud_client_session_updated();
    cloud_client_tick(100020);
    CHECK(opus_tail==1 && selftest_ms==20 && upload_diag.drops==0);
    cloud_client_tick(100039);CHECK(opus_tail==1);
    cloud_client_tick(100040);CHECK(opus_tail==2);
    puts("PASS_SELFTEST_CLOCK reconnect does not burst offline audio");return 0;
#endif
    cloud_client_tick(20);CHECK(client.ogg_header_step==2);
    queue(5,20);cloud_client_tick(180);CHECK(client.ogg_granule==4800);
    queue(10,200);cloud_client_tick(220);CHECK(client.ogg_granule==14400);
    queue(8,240);blocked=1;
    cloud_client_tick(400);cloud_client_tick(401);cloud_client_tick(900);
    CHECK(upload_diag.drops==0 && opus_tail-opus_head==8);
    blocked=0;cloud_client_tick(1000);
    CHECK(upload_diag.stall_max_ms==600 && client.ogg_granule==22080);
    queue(30,1001);opus_flush_ms=295;cloud_client_tick(1002);
    CHECK(opus_flush_ms==OPUS_FLUSH_MAX_MS);
    opus_head=opus_tail;opus_flush_ms=151;
    queue(1,1003);cloud_client_tick(1300);CHECK(opus_flush_ms==OPUS_FLUSH_MIN_MS);
    CHECK(upload_diag.drops==0);
    /* A 2 s backlog must drain on a link accepting one send every 200 ms.
       Ten packets/send can only sustain real time and never catch up. */
    queue(100,1400);
    for(uint32_t tick=1400;tick<=3000;tick+=20) {
        queue(1,tick);
#ifdef Z7_TRANSLATE_AUDIO
        blocked=(tick%100u)!=0;
#else
        blocked=(tick%200u)!=0;
#endif
        cloud_client_tick(tick);
    }
    blocked=0;
    CHECK(upload_diag.drops==0 && opus_tail-opus_head<20);

    memcpy((void *)cloud_client_config.path,live_path,sizeof(live_path));
    CHECK(send_session_config());

    cloud_live_begin(1400);
    const char *partial="{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"a1\",\"text\":\"\\u4f60\",\"stash\":\"\\u597d\"}";
    CHECK(cloud_live_message(partial,(uint32_t)strlen(partial),0)==0);
#ifdef Z7_ASR_SUBTITLE
    CHECK(model.translated[0]==0x4f60 && model.translated[1]==0x597d);
#else
    CHECK(model.show_source && model.source[0]==0x4f60 && model.source[1]==0x597d && !model.translated[0]);
#endif
    const char *final="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"a1\",\"transcript\":\"\\u4f60\\u597d\\ud83d\\ude00\"}";
    const char *stop="{\"type\":\"input_audio_buffer.speech_stopped\",\"item_id\":\"a1\"}";
    client.now_ms=1500;
    CHECK(cloud_live_message(stop,(uint32_t)strlen(stop),0)==0 && final_diag.stops==1);
    client.now_ms=1700;
    CHECK(cloud_live_message(final,(uint32_t)strlen(final),0)==0);
    CHECK(final_diag.last_tail_ms==200 && final_diag.final_ms[0]==1700);
#ifdef Z7_TRANSLATE_EN
    CHECK(model.final_count==0 && cloud.live.committed==0);
    uint32_t *waiting_pixels=malloc(UI_WIDTH*UI_HEIGHT*sizeof(*waiting_pixels));CHECK(waiting_pixels!=NULL);
    struct ui_render_stats waiting_stats;ui_render(waiting_pixels,&model,1700,0,&waiting_stats);
    CHECK(waiting_stats.caption_lines==0 && waiting_stats.source_lines>0);
    FILE *waiting_preview=fopen("tmp/asr_accuracy/zhen_waiting_preview.rgbx","wb");CHECK(waiting_preview!=NULL);
    CHECK(fwrite(waiting_pixels,sizeof(*waiting_pixels),UI_WIDTH*UI_HEIGHT,waiting_preview)==UI_WIDTH*UI_HEIGHT);
    fclose(waiting_preview);free(waiting_pixels);
    const char *events[]={
        "{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"t1\"},\"previous_item_id\":\"a1\"}",
        "{\"type\":\"response.text.text\",\"item_id\":\"t1\",\"response_id\":\"r1\",\"text\":\"Hello\"}",
        "{\"type\":\"response.text.text\",\"item_id\":\"t1\",\"response_id\":\"r1\",\"text\":\"Hello.\"}",
        "{\"type\":\"response.text.done\",\"item_id\":\"t1\",\"response_id\":\"r1\",\"text\":\"Hello.\"}",
        "{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\"}}"};
    for(unsigned i=0;i<4;++i) CHECK(cloud_live_message(events[i],(uint32_t)strlen(events[i]),0)==0);
    CHECK(model.final_count==0 && model.translated[0]=='H' && model.translated[5]=='.' && !model.translated[6]);
    CHECK(cloud_live_message(events[4],(uint32_t)strlen(events[4]),0)==0);
    CHECK(model.show_source && cloud_caption.translated[0]=='H' && cloud_caption.translated[5]=='.');
#endif
    CHECK(cloud.live.committed==1 && model.final_count==1);
    CHECK(cloud_caption.source[2]==0x1f600 && text_log.text[0][2]==0x1f600);
    CHECK(model.source[2]==' ');
    CHECK(cloud_live_message(final,(uint32_t)strlen(final),0)==0 && model.final_count==1);
#ifdef Z7_TRANSLATE_EN
    uint32_t *pixels=malloc(UI_WIDTH*UI_HEIGHT*sizeof(*pixels));CHECK(pixels!=NULL);
    struct ui_render_stats stats;ui_render(pixels,&model,2000,0,&stats);
    CHECK(stats.caption_lines>0 && stats.source_lines>0 && !stats.missing_glyphs);
    FILE *preview=fopen("tmp/asr_accuracy/zhen_preview.rgbx","wb");CHECK(preview!=NULL);
    CHECK(fwrite(pixels,sizeof(*pixels),UI_WIDTH*UI_HEIGHT,preview)==UI_WIDTH*UI_HEIGHT);
    fclose(preview);free(pixels);
    const char *next_speech="{\"type\":\"input_audio_buffer.speech_started\",\"audio_start_ms\":2000}";
    CHECK(cloud_live_message(next_speech,(uint32_t)strlen(next_speech),0)==0);
    CHECK(model.translated[0]=='H' && model.translated[5]=='.' && !model.translated[6]);
    CHECK(model.source[0]==0x4f60 && model.show_source);
    const char *next_partial="{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"a2\",\"text\":\"\\u518d\",\"stash\":\"\\u89c1\"}";
    CHECK(cloud_live_message(next_partial,(uint32_t)strlen(next_partial),0)==0);
    CHECK(model.source[0]==0x518d && model.source[1]==0x89c1);
    CHECK(model.translated[0]=='H' && model.translated[5]=='.' && !model.translated[6]);
    const char *next_final="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"a2\",\"transcript\":\"\\u518d\\u89c1\"}";
    CHECK(cloud_live_message(next_final,(uint32_t)strlen(next_final),0)==0);
    CHECK(model.translated[0]=='H' && model.translated[5]=='.' && !model.translated[6]);
    const char *next_link="{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"t2\"},\"previous_item_id\":\"a2\"}";
    CHECK(cloud_live_message(next_link,(uint32_t)strlen(next_link),0)==0);
    const char *next_english="{\"type\":\"response.text.text\",\"item_id\":\"t2\",\"response_id\":\"r2\",\"text\":\"Bye\"}";
    CHECK(cloud_live_message(next_english,(uint32_t)strlen(next_english),0)==0);
    CHECK(model.translated[0]=='B' && model.translated[1]=='y' && model.translated[2]=='e' && !model.translated[3]);
#ifdef Z7_TRANSLATE_AUDIO
    unsigned prior_count=model.final_count,prior_epoch=model.epoch;
    cloud_live_begin(5000);
    CHECK(model.source[0]==0x518d && model.source[1]==0x89c1 && model.translated[0]=='B');
    CHECK(model.epoch>prior_epoch && model.final_count==prior_count && model.session==UI_RUNNING);
    CHECK(cloud_live_message(final,(uint32_t)strlen(final),0)==0);
    for(unsigned i=0;i<5;++i) CHECK(cloud_live_message(events[i],(uint32_t)strlen(events[i]),0)==0);
    CHECK(model.final_count==prior_count+1 && cloud.live.committed==1 && model.translated[0]=='H');
#endif
#endif
    cloud_live_init(&model);CHECK(text_log.count==0);
#ifndef Z7_TRANSLATE_EN
    cloud_live_begin(7000);
    /* Recycle bounded utterance/history slots over many consecutive finals. */
    for(unsigned i=0;i<500u;++i) {
        char event[192];
        int n=snprintf(event,sizeof(event),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"long%u\",\"transcript\":\"\\u4f60\\u597d\"}",i);
        CHECK(n>0 && n<(int)sizeof(event));
        CHECK(cloud_live_message(event,(uint32_t)n,0)==0);
        CHECK(cloud.live.committed==i+1u && !cloud.live.error);
    }
    CHECK(model.translated[0]==0x4f60 && model.translated[1]==0x597d);
    /* A server Ping must be acknowledged on the authenticated channel. */
    network_info.phase=4;
    const uint8_t ping_frame[]={0x89u,1u,'x'};
    for(unsigned i=0;i<sizeof(ping_frame);++i) cloud_wire_byte(ping_frame[i]);
    cloud_live_tick(7001);CHECK(sent_pong==1);
    cloud_live_init(&model);
#endif
#ifdef Z7_TRANSLATE_EN
#ifdef Z7_TRANSLATE_AUDIO
    CHECK(strstr(session_update_json,"\"interrupt_response\":false")!=NULL);
#endif
    cloud_live_begin(6000);
    const char *configured="{\"type\":\"session.updated\",\"session\":{\"turn_detection\":{\"interrupt_response\":false}}}";
    CHECK(!cloud_live_message(configured,(uint32_t)strlen(configured),0));
    CHECK(session_diag[1]==1 && session_diag[2]==1 && session_diag[3]==0);
    /* A's translated text is ready while its audio response is still open.
       B must preview now; committing A later must not rewind B to A. */
    const char *text_before_audio[]={
        "{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"earlyA\",\"transcript\":\"A\"}",
        "{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"englishA\"},\"previous_item_id\":\"earlyA\"}",
        "{\"type\":\"response.text.done\",\"item_id\":\"englishA\",\"response_id\":\"audioA\",\"text\":\"Alpha\"}",
        "{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"earlyB\",\"text\":\"B\",\"stash\":\"\"}",
        "{\"type\":\"response.done\",\"response\":{\"id\":\"audioA\",\"status\":\"completed\"}}",
        "{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"earlyC\",\"transcript\":\"C\"}"};
    for(unsigned i=0;i<4;++i) CHECK(!cloud_live_message(text_before_audio[i],(uint32_t)strlen(text_before_audio[i]),0));
    CHECK(model.source[0]=='B' && model.translated[0]=='A' && !cloud.live.committed);
    CHECK(!cloud_live_message(text_before_audio[4],(uint32_t)strlen(text_before_audio[4]),0));
    CHECK(model.source[0]=='B' && model.translated[0]=='A' && cloud.live.committed==1);
    CHECK(!cloud_live_message(text_before_audio[5],(uint32_t)strlen(text_before_audio[5]),0));
    CHECK(model.source[0]=='B');
    cloud_live_init(&model);cloud_live_begin(6000);
    const char *empty_partial="{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"empty\",\"text\":\"\",\"stash\":\"\"}";
    CHECK(!cloud_live_message(empty_partial,(uint32_t)strlen(empty_partial),0));
    const char *empty_final="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"empty\",\"transcript\":\"\"}";
    CHECK(!cloud_live_message(empty_final,(uint32_t)strlen(empty_final),0));
    char rapid[1600];
    for(unsigned i=0;i<12;++i) {
        snprintf(rapid,sizeof(rapid),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"q%u\",\"transcript\":\"%c\"}",i,'A'+i);
        CHECK(!cloud_live_message(rapid,(uint32_t)strlen(rapid),0));
        CHECK(model.source[0]=='A'); /* Later speech cannot replace untranslated A. */
        snprintf(rapid,sizeof(rapid),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"v%u\"},\"previous_item_id\":\"q%u\"}",i,i);
        CHECK(!cloud_live_message(rapid,(uint32_t)strlen(rapid),0));
    }
    /* Complete later translations first: commits and visible pairs stay ordered. */
    for(int i=11;i>=0;--i) {
        snprintf(rapid,sizeof(rapid),"{\"type\":\"response.text.done\",\"item_id\":\"v%d\",\"response_id\":\"z%d\",\"text\":\"%c\"}",i,i,'a'+i);
        CHECK(!cloud_live_message(rapid,(uint32_t)strlen(rapid),0));
        snprintf(rapid,sizeof(rapid),"{\"type\":\"response.done\",\"response\":{\"id\":\"z%d\",\"status\":\"completed\"}}",i);
        CHECK(!cloud_live_message(rapid,(uint32_t)strlen(rapid),0));
        if(i) CHECK(!cloud.live.committed && model.source[0]=='A');
    }
    CHECK(cloud.live.committed==12 && text_log.count==12 && !session_diag[5]);
    CHECK(model.source[0]=='L' && model.translated[0]=='l');
    char long_text[702];memset(long_text,'x',700);long_text[700]='Z';long_text[701]=0;
    snprintf(rapid,sizeof(rapid),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"long\",\"transcript\":\"%s\"}",long_text);
    struct cloud_event long_event;
    CHECK(cloud_event_parse(rapid,(uint32_t)strlen(rapid),&long_event)==CLOUD_OK);
    CHECK(long_event.text[UI_MAX_TEXT-2]=='Z' && !long_event.text[UI_MAX_TEXT-1]);
    cloud_live_init(&model);
#endif
    cloud_live_begin(7000);
    const uint8_t close_frame[]={0x88,2,0x03,0xe8};
    for(unsigned i=0;i<sizeof(close_frame);++i) cloud_wire_byte(close_frame[i]);
    unsigned failures_before=transport_failures;
    cloud_live_tick(7001);
    CHECK(cloud_report.error==8 && transport_failures==failures_before+1 && client.phase==CLOUD_IDLE);
    CHECK(cloud_health[8]==8 && cloud_health[15]==1000);
    cloud_live_tick(7201);CHECK(transport_failures==failures_before+1);
    cloud_live_begin(7300);CHECK(cloud_health[8]==8 && !cloud.live.error);
#ifdef Z7_TLS_PASSTHROUGH
    client.phase=CLOUD_STREAMING;last_rx_ms=0;ping_sent=0;blocked=1;
    cloud_client_tick(16000);CHECK(!ping_sent);
    blocked=0;cloud_client_tick(16001);CHECK(ping_sent && keepalive_diag[1]==1);
    cloud_client_receive(0x8au);cloud_client_receive(0);
    cloud_client_tick(16002);CHECK(!ping_sent && client.phase==CLOUD_STREAMING);
    cloud_client_tick(31002);CHECK(ping_sent && keepalive_diag[1]==2);
    unsigned before_timeout=transport_failures;
    cloud_client_tick(36003);
    CHECK(client.phase==CLOUD_IDLE && keepalive_diag[2]==1 && transport_failures==before_timeout+1);
#endif
    /* A refused upgrade request is resent; a server silent for 15 s fails the link. */
    cloud_client_stop();blocked=1;cloud_client_start(40000);
    CHECK(client.phase==CLOUD_HANDSHAKE && client.handshake_pending);
    cloud_client_tick(40010);CHECK(client.handshake_pending);
    blocked=0;cloud_client_tick(40020);CHECK(!client.handshake_pending && client.phase==CLOUD_HANDSHAKE);
    unsigned before_handshake=transport_failures;
    cloud_client_tick(54999);CHECK(client.phase==CLOUD_HANDSHAKE && transport_failures==before_handshake);
    cloud_client_tick(55001);CHECK(client.phase==CLOUD_IDLE && transport_failures==before_handshake+1);
    printf("PASS_ASR_ACCURACY checks=%u\n",checks);
    return 0;
}
