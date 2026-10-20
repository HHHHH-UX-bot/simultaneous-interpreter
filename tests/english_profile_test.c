/* Real client/event/UI code, mocked TLS boundary, private Windows MMIO pages. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define Z7_OPUS
#include "../software/network_ui/network.h"
#include "../software/subtitle_ui/ui_render.h"
#include "../software/network_ui/cloud_client.c"
#include "../software/network_ui/cloud_live.c"
#undef network_send
#undef network_stream_attach
volatile struct network_status network_info;
volatile struct stream_pilot_status stream_pilot;
static unsigned checks,failures,pings,pongs,blocked;
static char session[2048];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);exit(1);}}while(0)
void cache_clean_range(const void *p,unsigned n){(void)p;(void)n;}
int opus_codec_init(void){return 1;}
int opus_codec_encode(const int16_t *p,uint8_t *out,uint32_t n){(void)p;if(n<60)return -1;memset(out,0,60);out[0]=0x48;return 60;}
void network_stop(void){}
void network_stream_failed(void){++failures;}
void stream_pilot_set_receiver(void (*p)(uint8_t)){(void)p;}
int arm_tls_attach(void (*p)(uint8_t)){(void)p;return 1;}
int arm_tls_send(const uint8_t *p,uint32_t n){
    CHECK(n<=2048);if(blocked)return 0;
    if(n && p[0]==0x89u)++pings;
    if(n && p[0]==0x8au)++pongs;
    if(n && p[0]==0x81u){
        uint32_t bytes=p[1]&127u,at=2;if(bytes==126u){bytes=((uint32_t)p[2]<<8)|p[3];at=4;}
        CHECK(bytes<sizeof(session) && at+4u+bytes==n);
        char json[2048];for(uint32_t i=0;i<bytes;++i)json[i]=(char)(p[at+4u+i]^p[at+(i&3u)]);json[bytes]=0;
        if(strstr(json,"\"session.update\""))memcpy(session,json,bytes+1u);
    }
    return 1;
}
static void event(const char *s){CHECK(!cloud_live_message(s,(uint32_t)strlen(s),0));}
static void queue(unsigned n,uint32_t now){int16_t pcm[320]={0};client.now_ms=now;while(n--)queue_opus_frame(pcm);}
int main(void){
    CHECK(VirtualAlloc((void *)0x43c20000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=0);
    CHECK(VirtualAlloc((void *)0x01d30000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=0);
    AUDIO_DUMP_HEADER[0]=AUDIO_DUMP_MAGIC;AUDIO_DUMP_HEADER[6]=1;
    struct ui_state model;ui_init(&model);cloud_live_init(&model);cloud_client_init();speech_playback_init();
#ifdef Z7_ASR_SUBTITLE
    memcpy((void *)cloud_client_config.path,asr_model_path,sizeof(asr_model_path));
#else
    const char path[]="/api-ws/v1/realtime?model=qwen3.5-livetranslate-flash-realtime";
    memcpy((void *)cloud_client_config.path,path,sizeof(path));
#endif
    cloud_client_start(0);client.phase=CLOUD_WAITING_SESSION;
    blocked=1;cloud_client_session_created();CHECK(client.update_pending);
    blocked=0;cloud_client_tick(10);CHECK(!client.update_pending && session[0]);
    printf("SESSION %s\n",session);cloud_client_session_updated();CHECK(client.phase==CLOUD_STREAMING);
    cloud_client_tick(20);CHECK(client.ogg_header_step==2);
    queue(100,1400);
    for(uint32_t now=1400;now<=3000;now+=20){queue(1,now);
#ifdef Z7_TRANSLATE_AUDIO
        blocked=(now%100u)!=0;
#else
        blocked=(now%200u)!=0;
#endif
        cloud_client_tick(now);
    }
    CHECK(!upload_diag.drops && opus_tail-opus_head<20);blocked=0;
    cloud_live_begin(2000);
    event("{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"a0\",\"text\":\"Good \",\"stash\":\"morning.\"}");
#ifdef Z7_ASR_SUBTITLE
    CHECK(model.translated[0]=='G' && !model.show_source);
#else
    CHECK(model.source[0]=='G' && model.show_source && !model.translated[0]);
#endif
    for(unsigned i=0;i<500u;++i){
        char json[384];snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"a%u\",\"transcript\":\"Good morning.\"}",i);event(json);
#ifndef Z7_ASR_SUBTITLE
        CHECK(cloud.live.committed==i);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"t%u\"},\"previous_item_id\":\"a%u\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.text\",\"item_id\":\"t%u\",\"response_id\":\"r%u\",\"text\":\"\\u65e9\\u4e0a\",\"stash\":\"\\u597d\\u3002\"}",i,i);event(json);
        CHECK(model.translated[0]==0x65e9 && model.translated[2]==0x597d);
        snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.done\",\"item_id\":\"t%u\",\"response_id\":\"r%u\",\"transcript\":\"\\u65e9\\u4e0a\\u597d\\u3002\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.done\",\"response\":{\"id\":\"r%u\",\"status\":\"completed\"}}",i);event(json);
#endif
        CHECK(cloud.live.committed==i+1u && model.final_count==i+1u && !cloud.live.error);
        CHECK(cloud_caption.source[0]=='G');
    }
#ifdef Z7_ASR_SUBTITLE
    CHECK(model.translated[0]=='G' && cloud.live.translations==0);
    event("{\"type\":\"response.text.text\",\"item_id\":\"wrong\",\"text\":\"rewritten\"}");
    CHECK(model.translated[0]=='G' && model.final_count==500);
#else
    CHECK(model.translated[0]==0x65e9 && cloud.live.translations==500);
#endif
    uint32_t *pixels=malloc(UI_WIDTH*UI_HEIGHT*sizeof(*pixels));CHECK(pixels!=0);
    struct ui_render_stats stats;ui_render(pixels,&model,3000,0,&stats);
    CHECK(stats.caption_lines && !stats.missing_glyphs);
#ifdef Z7_BILINGUAL
    CHECK(stats.source_lines);
#else
    CHECK(!stats.source_lines);
#endif
    const char *image=getenv("PROFILE_PREVIEW");CHECK(image!=0);FILE *f=fopen(image,"wb");CHECK(f!=0);
    CHECK(fwrite(pixels,sizeof(*pixels),UI_WIDTH*UI_HEIGHT,f)==UI_WIDTH*UI_HEIGHT);fclose(f);free(pixels);
#ifdef Z7_RECOGNIZE_EN
    /* Long English utterances must still produce bounded final captions. */
    for(unsigned extra=0;extra<3u;++extra){
        unsigned length=UI_MAX_TEXT-1u+(extra==2u?80u:extra);
        char text[UI_MAX_TEXT+80],json[1024];
        for(unsigned i=0;i<length;++i) text[i]=(char)('a'+i%26u);
        text[length-1u]='Z';text[length]=0;
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"long%u\",\"text\":\"%s\",\"stash\":\"!\"}",extra,text);
        event(json);CHECK(model.translated[UI_MAX_TEXT-2u]=='!');
        uint32_t before=cloud.live.committed;
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"long%u\",\"transcript\":\"%s\"}",extra,text);
        event(json);
        CHECK(cloud.live.committed==before+1u && model.final_count==before+1u);
        CHECK(model.translated[0]==(uint32_t)text[length-(UI_MAX_TEXT-1u)]);
        CHECK(model.translated[UI_MAX_TEXT-2u]=='Z' && !model.translated[UI_MAX_TEXT-1u]);
    }
#endif
    network_info.phase=4;const uint8_t ping[]={0x89u,1u,'x'};
    for(unsigned i=0;i<sizeof(ping);++i) {cloud_wire_byte(ping[i]);}
    cloud_live_tick(3001);CHECK(pongs==1);
    client.phase=CLOUD_STREAMING;last_rx_ms=0;ping_sent=0;blocked=1;
    cloud_client_tick(16000);CHECK(!ping_sent);
    blocked=0;cloud_client_tick(16001);CHECK(ping_sent && pings==1);
    cloud_client_receive(0x8au);cloud_client_receive(0);cloud_client_tick(16002);CHECK(!ping_sent);
    cloud_client_tick(31002);CHECK(ping_sent);cloud_client_tick(36003);CHECK(client.phase==CLOUD_IDLE && failures==1);
    printf("PASS_ENGLISH_PROFILE checks=%u finals=500\n",checks);return 0;
}
