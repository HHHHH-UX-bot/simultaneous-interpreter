#define main profile_main
#include "english_profile_test.c"
#undef main
int main(void) {
    CHECK(VirtualAlloc((void *)0x43c20000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=0);
    CHECK(VirtualAlloc((void *)0x01d30000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=0);
    struct ui_state model;ui_init(&model);cloud_live_init(&model);cloud_client_init();speech_playback_init();network_info.phase=4;cloud_live_begin(0);
    /* A terminal empty transcript must not reserve a permanent queue head.
       Exercise both a translation already received and a later linked one. */
    for(unsigned i=0;i<32;++i) {
        char json[512];
        if(i%4u) {snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"E%u\",\"text\":\"Speculative only.\"}",i);event(json);}
        if(i&1u) {
            snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"X%u\"},\"previous_item_id\":\"E%u\"}",i,i);event(json);
            snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.done\",\"item_id\":\"X%u\",\"response_id\":\"Q%u\",\"transcript\":\"Unpaired target.\"}",i,i);event(json);
        }
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"E%u\",\"transcript\":\"\"}",i);event(json);
        CHECK(!cloud_health[4] && !cloud.live.committed && !model.history_count);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"X%u\"},\"previous_item_id\":\"E%u\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.done\",\"item_id\":\"X%u\",\"response_id\":\"Q%u\",\"transcript\":\"Late unpaired target.\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.done\",\"response\":{\"id\":\"Q%u\",\"status\":\"completed\"}}",i);event(json);
        for(unsigned j=0;j<CLOUD_PAIRS;++j) CHECK(!cloud.pairs[j].order);
    }
    /* Empty is valid only as an explicit ASR string, never null/missing. */
    struct cloud_event parsed;
    const char *bad="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"bad\",\"transcript\":null}";
    CHECK(cloud_event_parse(bad,(uint32_t)strlen(bad),&parsed)==CLOUD_SCHEMA);
    bad="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"bad\"}";
    CHECK(cloud_event_parse(bad,(uint32_t)strlen(bad),&parsed)==CLOUD_SCHEMA);
    bad="{\"type\":\"error\",\"error\":{\"code\":\"UNEXPECTED_ASR_ERROR\",\"message\":\"\\u8bc6\\u522b\\u5931\\u8d25\"}}";
    CHECK(cloud_event_parse(bad,(uint32_t)strlen(bad),&parsed)==CLOUD_OK && parsed.kind==CLOUD_ERROR && !strcmp(parsed.error_code,"UNEXPECTED_ASR_ERROR"));
    for(unsigned i=1;i<=64;++i) {
        char json[512];
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"T%u\"},\"previous_item_id\":\"S%u\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"S%u\",\"transcript\":\"Final source.\"}",i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"S%u\",\"transcript\":\"\"}",i);event(json);
        /* A source already finalized must not be overwritten by late previews. */
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"S%u\",\"text\":\"Late source.\"}",i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.done\",\"item_id\":\"T%u\",\"response_id\":\"R%u\",\"transcript\":\"Final target.\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.text\",\"item_id\":\"T%u\",\"response_id\":\"R%u\",\"text\":\"Late target.\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.done\",\"response\":{\"id\":\"R%u\",\"status\":\"completed\"}}",i);event(json);
        CHECK(cloud.live.committed==i && model.history[model.history_count-1u].source[0]=='F' && model.history[model.history_count-1u].translated[0]=='F');
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.text\",\"item_id\":\"S%u\",\"text\":\"Late committed source.\"}",i);event(json);
        for(unsigned j=0;j<CLOUD_PAIRS;++j) CHECK(!cloud.pairs[j].order);
    }
    CHECK(session_diag[6]==32);
    for(unsigned i=1;i<=32;++i) {
        char json[1024];
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"FS%u\",\"transcript\":\"Final source.\"}",i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"FT%u\"},\"previous_item_id\":\"FS%u\"}",i,i);event(json);
        if(i&1u) {snprintf(json,sizeof(json),"{\"type\":\"response.audio_transcript.text\",\"item_id\":\"FT%u\",\"response_id\":\"FR%u\",\"text\":\"Speculative preview.\"}",i,i);event(json);}
        snprintf(json,sizeof(json),"{\"type\":\"response.done\",\"response\":{\"id\":\"FR%u\",\"status\":\"completed\",\"output\":[{\"id\":\"FT%u\",\"status\":\"completed\",\"role\":\"assistant\",\"content\":[{\"type\":\"%s\",\"%s\":\"Final target.\"}]}]}}",i,i,i&1u?"audio":"text",i&1u?"transcript":"text");event(json);
        CHECK(cloud.live.committed==64u+i && model.history[model.history_count-1u].translated[0]=='F');
        for(unsigned j=0;j<CLOUD_PAIRS;++j) CHECK(!cloud.pairs[j].order);
    }
    CHECK(session_diag[8]==32);
    for(unsigned i=0;i<8;++i) {
        char json[1024];
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"ZS%u\",\"transcript\":\"Real source without translated output.\"}",i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"ZT%u\"},\"previous_item_id\":\"ZS%u\"}",i,i);event(json);
        snprintf(json,sizeof(json),"{\"type\":\"response.done\",\"response\":{\"id\":\"ZR%u\",\"status\":\"completed\",\"output\":[{\"id\":\"ZT%u\",\"status\":\"completed\",\"role\":\"assistant\",\"content\":[{\"type\":\"audio\",\"transcript\":\"\"}]}]}}",i,i);event(json);
        CHECK(cloud.live.committed==96);
        for(unsigned j=0;j<CLOUD_PAIRS;++j) CHECK(!cloud.pairs[j].order);
    }
    CHECK(session_diag[7]==8);
    printf("PASS_LATE_PREVIEW checks=%u finals=%u empty_asr=%u empty_targets=%u response_done_finals=%u no stale slots, fabricated history or final text overwrite\n",checks,cloud.live.committed,session_diag[6],session_diag[7],session_diag[8]);return 0;
}
