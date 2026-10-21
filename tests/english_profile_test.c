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
#ifdef Z7_TEXT_ONLY_RESPONSE
#define TRANSLATION_PARTIAL "response.text.text"
#define TRANSLATION_FINAL "response.text.done"
#define TRANSLATION_FIELD "text"
#else
#define TRANSLATION_PARTIAL "response.audio_transcript.text"
#define TRANSLATION_FINAL "response.audio_transcript.done"
#define TRANSLATION_FIELD "transcript"
#endif
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);exit(1);}}while(0)
void cache_clean_range(const void *p,unsigned n){(void)p;(void)n;}
int opus_codec_init(void){return 1;}
int opus_codec_encode(const int16_t *p,uint8_t *out,uint32_t n){(void)p;if(n<60)return -1;memset(out,0,60);out[0]=0x48;return 60;}
void network_stop(void){}
void network_cloud_run(int run){(void)run;}
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
    network_info.phase=4;
    cloud_live_connection(cloud_client_ready());CHECK(model.stage==UI_CONNECTING);
    blocked=1;cloud_client_session_created();CHECK(client.update_pending);
    blocked=0;cloud_client_tick(10);CHECK(!client.update_pending && session[0]);
    printf("SESSION %s\n",session);
    cloud_live_connection(cloud_client_ready());CHECK(model.stage==UI_CONNECTING);
    cloud_client_session_updated();CHECK(client.phase==CLOUD_STREAMING);
    cloud_live_connection(cloud_client_ready());CHECK(model.stage==UI_LISTENING);
    uint32_t connection_seq=model.change_sequence;
    cloud_live_connection(cloud_client_ready());CHECK(model.change_sequence==connection_seq);
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
    cloud_live_connection(cloud_client_ready());CHECK(model.stage==UI_LISTENING);
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
        snprintf(json,sizeof(json),"{\"type\":\"" TRANSLATION_PARTIAL "\",\"item_id\":\"t%u\",\"response_id\":\"r%u\",\"text\":\"\\u65e9\\u4e0a\",\"stash\":\"\\u597d\\u3002\"}",i,i);event(json);
        CHECK(model.translated[0]==0x65e9 && model.translated[2]==0x597d);
        snprintf(json,sizeof(json),"{\"type\":\"" TRANSLATION_FINAL "\",\"item_id\":\"t%u\",\"response_id\":\"r%u\",\"" TRANSLATION_FIELD "\":\"\\u65e9\\u4e0a\\u597d\\u3002\"}",i,i);event(json);
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
    /* Real common Han outside the demo subset must remain readable at every
       selectable size, rather than becoming the same two replacement boxes. */
    struct ui_state font_model=model;
    uint32_t *font_image=malloc(UI_WIDTH*UI_HEIGHT*sizeof(*font_image));CHECK(font_image!=0);
    unsigned previous_height=0;
    for(unsigned size=0;size<3u;++size) {
        font_model.font_index=size;
        memset(font_model.translated,0,sizeof(font_model.translated));
        font_model.translated[0]=0x5496; /* 咖 */
        font_model.translated[1]=0x5561; /* 啡 */
        ui_render(font_image,&font_model,3000,0,&stats);
        CHECK(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
        unsigned top=346,bottom=0,soft=0;
        for(unsigned y=148;y<346;++y)for(unsigned x=44;x<700;++x) {
            unsigned color=font_image[y*UI_WIDTH+x];
            if(color!=0xffffffu) {if(y<top)top=y;if(y>bottom)bottom=y;if(color!=0x172a38u)++soft;}
        }
        CHECK(bottom>top && bottom-top>previous_height && soft>20u);previous_height=bottom-top;
        font_model.translated[0]=font_model.translated[1]=0x25a1;
        ui_render(pixels,&font_model,3000,0,&stats);
        unsigned different=0;
        for(unsigned y=148;y<346;++y)for(unsigned x=44;x<700;++x)
            different+=font_image[y*UI_WIDTH+x]!=pixels[y*UI_WIDTH+x];
        CHECK(different>100u);
    }
    free(font_image);ui_render(pixels,&model,3000,0,&stats);
    CHECK(ui_latency_pack(62,1,850,1)==0x8352803eu);
    CHECK(ui_latency_pack(10000,1,65535,1)==0xe70fe70fu);
    CHECK(ui_latency_pack(62,0,850,0)==0);
    uint32_t *base=malloc(UI_WIDTH*UI_HEIGHT*sizeof(*base));CHECK(base!=0);
    memcpy(base,pixels,UI_WIDTH*UI_HEIGHT*sizeof(*base));
    ui_render_latency(pixels,ui_latency_pack(62,1,850,1),&stats);
    unsigned changed=0,soft_ink=0;
    for(unsigned y=0;y<UI_HEIGHT;++y)for(unsigned x=0;x<UI_WIDTH;++x){
        unsigned at=y*UI_WIDTH+x;
        if(base[at]==pixels[at])continue;
        CHECK(y>=8u && y<56u && ((x>=492u && x<596u)||(x>=604u && x<708u)));
        ++changed;
        if(pixels[at]!=0xd7e1e5u && pixels[at]!=0xf1f6f6u && pixels[at]!=0x006c68u && pixels[at]!=0x526575u)++soft_ink;
    }
    CHECK(changed>7000u && soft_ink>100u && !stats.missing_glyphs && !stats.clipped_glyph_pixels);
    for(unsigned i=0;i<2u;++i) {
        memcpy(base,pixels,UI_WIDTH*UI_HEIGHT*sizeof(*base));
        ui_render_latency(base,i?ui_latency_pack(10000,1,65535,1):0,&stats);
        CHECK(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
    }
    free(base);
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
    uint32_t retained=model.translated[0],finals=model.final_count;
    CHECK(ui_control(&model,UI_FONT_PLUS));
    uint32_t font_choice=model.font_index,source_choice=model.show_source;
    CHECK(ui_control(&model,UI_PAUSE));cloud_live_control(UI_PAUSE);
    uint32_t frames=client.opus_frames;
    *(volatile uint32_t *)0x43c20090u=320;
    cloud_client_tick(4000);cloud_client_tick(4001);
    CHECK(!cloud_live_capture_enabled() && client.phase==CLOUD_IDLE && client.opus_frames==frames);
    connection_seq=model.change_sequence;cloud_live_connection(1);
    CHECK(model.session==UI_PAUSED && model.change_sequence==connection_seq);
    event("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"late\",\"transcript\":\"old session\"}");
    CHECK(model.translated[0]==retained && model.final_count==finals);
    CHECK(ui_control(&model,UI_RESUME));cloud_live_control(UI_RESUME);cloud_live_begin(4100);
    cloud_live_connection(cloud_client_ready());CHECK(model.stage==UI_CONNECTING);
    CHECK(cloud_live_capture_enabled() && cloud.live.epoch==model.epoch);
    CHECK(model.font_index==font_choice && model.show_source==source_choice && model.final_count==finals);
    network_info.phase=6;cloud_live_connection(0);
    CHECK(model.stage==UI_OFFLINE && model.session==UI_PAUSED && model.translated[0]==retained);
    connection_seq=model.change_sequence;cloud_live_connection(0);
    CHECK(model.change_sequence==connection_seq);
    network_info.phase=1;cloud_live_connection(0);CHECK(model.stage==UI_CONNECTING);
    CHECK(model.translated[0]==retained && model.font_index==font_choice && model.final_count==finals);
    CHECK(ui_control(&model,UI_CLEAR));cloud_live_control(UI_CLEAR);
    CHECK(!cloud_live_capture_enabled() && model.session==UI_IDLE && !model.final_count && !model.translated[0]);
    CHECK(ui_control(&model,UI_START));cloud_live_control(UI_START);cloud_live_begin(4200);
    CHECK(cloud_live_capture_enabled() && cloud.live.epoch==model.epoch && !model.final_count);
#ifdef Z7_BILINGUAL
    /* Runtime direction changes use real control/session/event paths. */
    for(uint32_t turn=0;turn<4u;++turn) {
        enum ui_language next=model.language==UI_EN_ZH?UI_ZH_EN:UI_EN_ZH;
        enum ui_command select=next==UI_ZH_EN?UI_SELECT_ZH_EN:UI_SELECT_EN_ZH;
        uint32_t epoch=model.epoch;
        CHECK(ui_control(&model,UI_LANGUAGE_MENU) && model.language_menu);
        CHECK(ui_control(&model,select));cloud_live_control(select);
        CHECK(model.language==next && !model.language_menu && model.epoch==epoch+1u);
        CHECK(model.session==UI_RUNNING && model.stage==UI_CONNECTING && client.phase==CLOUD_IDLE);
        CHECK(!model.final_count && !model.translated[0] && model.font_index==font_choice);
        event("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"late_direction\",\"transcript\":\"old direction\"}");
        CHECK(!model.translated[0] && !model.final_count);
        cloud_live_begin(4300u+turn*100u);
        client.phase=CLOUD_WAITING_SESSION;cloud_client_session_created();
        const char *input=next==UI_EN_ZH?"\"language\":\"en\"":"\"language\":\"zh\"";
        const char *target=next==UI_EN_ZH?"\"translation\":{\"language\":\"zh\"":"\"translation\":{\"language\":\"en\"";
        CHECK(strstr(session,input) && strstr(session,target));
        printf("LANGUAGE_SESSION %s\n",session);
        cloud_client_session_updated();CHECK(client.phase==CLOUD_STREAMING);
        event("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"fresh_direction\",\"transcript\":\"hello\"}");
        event("{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"fresh_translation\"},\"previous_item_id\":\"fresh_direction\"}");
        event("{\"type\":\"" TRANSLATION_FINAL "\",\"item_id\":\"fresh_translation\",\"response_id\":\"fresh_response\",\"" TRANSLATION_FIELD "\":\"translated\"}");
        event("{\"type\":\"response.done\",\"response\":{\"id\":\"fresh_response\",\"status\":\"completed\"}}");
        CHECK(model.final_count==1u && model.translated[0]=='t');
    }
    CHECK(ui_control(&model,UI_PAUSE));cloud_live_control(UI_PAUSE);
    CHECK(ui_control(&model,UI_SELECT_ZH_EN));cloud_live_control(UI_SELECT_ZH_EN);
    CHECK(model.session==UI_PAUSED && !cloud_live_capture_enabled() && client.phase==CLOUD_IDLE);
    CHECK(ui_control(&model,UI_CLEAR) && model.language==UI_ZH_EN);
    CHECK(ui_control(&model,UI_START));cloud_live_control(UI_START);cloud_live_begin(4800);
    CHECK(model.language==UI_ZH_EN && cloud_live_capture_enabled());
#endif
    *(volatile uint32_t *)0x43c20090u=0;
    client.now_ms=5000;cloud_client_speech_started(0);
    CHECK(*(volatile uint32_t *)0x43c20088u==0);
    client.now_ms=5234;cloud_client_translation(0);
    CHECK(*(volatile uint32_t *)0x43c20088u==0x100eau);
    client.now_ms=5300;cloud_client_speech_started(0);
    CHECK(*(volatile uint32_t *)0x43c20088u==0);
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
