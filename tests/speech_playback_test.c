#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t regs[64],received;
static uint32_t expected_at;
static int16_t expected(uint32_t n) {return (int16_t)(n*173u+0x8001u);}
static void hw_write(uint32_t offset,uint32_t value) {
    if(offset==0xa0) {
        if(regs[0xa4/4]>=8192 || (int16_t)value!=expected(expected_at++)) {
            fprintf(stderr,"PCM order/full failure at %u\n",expected_at);exit(1);
        }
        ++regs[0xa4/4];++received;
    } else {
        regs[offset/4]=value;
        if(offset==0x9c && (value&4u)) regs[0xa4/4]=0;
    }
}
#define SPEECH_REG(offset) regs[(offset)/4]
#define SPEECH_WRITE(offset,value) hw_write(offset,value)
#include "../software/network_ui/speech_playback.c"
#include "../software/network_ui/cloud_events.h"
#include "../software/network_ui/cloud_wire.h"
unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"failure line %d\n",__LINE__);exit(1);}} while(0)
static int receive(const char *json,uint32_t n,void *unused) {
    struct cloud_event e;(void)unused;
    CHECK(cloud_event_parse(json,n,&e)==CLOUD_OK && e.kind==CLOUD_AUDIO_DELTA);
    CHECK(speech_playback_audio(e.audio,e.audio_bytes));return 0;
}
static char message[30000];
static uint32_t make_message(uint32_t start,uint32_t samples) {
    static const char base64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t n=(uint32_t)sprintf(message,"{\"type\":\"response.audio.delta\",\"delta\":\"");
    for(uint32_t i=0;i<samples*2u;i+=3) {
        uint32_t v=0;
        for(uint32_t j=0;j<3;++j) {
            uint32_t at=i+j;
            if(at<samples*2u) v|=((uint16_t)expected(start+at/2u)>>(8u*(at%2u))&255u)<<(16u-j*8u);
        }
        message[n++]=base64[v>>18];message[n++]=base64[(v>>12)&63u];
        message[n++]=i+1u<samples*2u?base64[(v>>6)&63u]:'=';
        message[n++]=i+2u<samples*2u?base64[v&63u]:'=';
    }
    message[n++]='"';message[n++]='}';return n;
}
static void send_audio(uint32_t start,uint32_t samples) {
    uint32_t n=make_message(start,samples);
    CHECK(n>4096 && n<65536);
    // Real WebSocket parser, including network-sized streaming delivery.
    cloud_wire_byte(0x81);cloud_wire_byte(126);
    cloud_wire_byte((uint8_t)(n>>8));cloud_wire_byte((uint8_t)n);
    for(uint32_t i=0;i<n;++i) cloud_wire_byte((uint8_t)message[i]);
    CHECK(!cloud_wire_status()->error);
}
static uint32_t test_now;
static void advance(uint32_t milliseconds) {
    while(milliseconds--) {
        speech_playback_tick(test_now++);
        if(regs[0x9c/4]&2u) {
            uint32_t n=regs[0xa4/4]<24u?regs[0xa4/4]:24u;
            if(n<24u && !(regs[0x9c/4]&8u)) ++regs[0xa8/4];
            regs[0xa4/4]-=n;regs[0xac/4]+=n;
        }
    }
}
int main(void) {
    struct cloud_event transcript;
    const char *preview="{\"type\":\"response.audio_transcript.text\",\"item_id\":\"t1\",\"response_id\":\"r1\",\"text\":\"Hello,\",\"stash\":\" world.\"}";
    CHECK(cloud_event_parse(preview,(uint32_t)strlen(preview),&transcript)==CLOUD_OK);
    CHECK(transcript.kind==CLOUD_TRANSLATION_DELTA && transcript.cumulative && transcript.text[6]==' ' && transcript.text[7]=='w' && transcript.text[12]=='.');
    const char *final="{\"type\":\"response.audio_transcript.done\",\"item_id\":\"t1\",\"response_id\":\"r1\",\"transcript\":\"Hello.\"}";
    CHECK(cloud_event_parse(final,(uint32_t)strlen(final),&transcript)==CLOUD_OK && transcript.kind==CLOUD_TRANSLATION && transcript.text[5]=='.');
    regs[0x98/4]=0x31594c50;speech_playback_init();cloud_wire_init(receive,NULL);
    CHECK(regs[0x9c/4]==5);
    send_audio(0,4800);CHECK(speech_diag.samples==4800);
    advance(4);CHECK(!(regs[0x9c/4]&2u));
    send_audio(4800,4800);advance(1);CHECK(!(regs[0x9c/4]&2u));
    uint32_t before=queued;
    CHECK(!speech_playback_audio("!!!!",4) && queued==before);
    CHECK(!speech_playback_audio("AA==",4) && queued==before);
    CHECK(!speech_playback_audio("AA=A",4) && queued==before);
    for(unsigned batch=2;batch<10;++batch) send_audio(batch*4800u,4800);
    advance(1);CHECK(!(regs[0x9c/4]&2u)); /* Software depth alone cannot prime DAC. */
    advance(16);CHECK(regs[0x9c/4]&2u);
    CHECK(regs[0xa4/4]>7000u); /* Physical reservoir survives a 50 ms service gap. */
    regs[0xa4/4]-=1200u;regs[0xac/4]+=1200u;test_now+=50u;
    // Twelve seconds of real-time input crosses the ten-second ring while
    // the simulated DAC consumes samples only when playback is enabled.
    for(unsigned batch=10;batch<60;++batch) {advance(200);send_audio(batch*4800u,4800);}
    speech_playback_done();
    while(queued || regs[0xa4/4]) advance(1);
    advance(1);
    CHECK(received==288000 && regs[0xac/4]==received && !(regs[0x9c/4]&2u));
    CHECK(!speech_diag.overflow_chunks && speech_diag.invalid_chunks==3);
    CHECK(!regs[0xa8/4]);
    // Reproduce the board's 320 ms PCM chunks arriving every 540 ms.
    // The old 400 ms startup stutters; the reservoir must play this phrase once.
    memset(regs,0,sizeof(regs));received=expected_at=test_now=0;
    regs[0x98/4]=0x31594c50;speech_playback_init();cloud_wire_init(receive,NULL);
    for(unsigned batch=0;batch<5;++batch) {
        send_audio(batch*7680u,7680);
        if(batch<4) advance(540);
    }
    speech_playback_done();
    while(queued || regs[0xa4/4]) advance(1);
    advance(1);
    CHECK(speech_diag.starts==1 && !regs[0xa8/4] && received==38400);
    CHECK(regs[0xac/4]==received);
    // Next voice arrives while the previous voice is still playing. It must
    // wait for its own reservoir rather than inheriting the old run flag.
    memset(regs,0,sizeof(regs));received=expected_at=test_now=0;
    regs[0x98/4]=0x31594c50;speech_playback_init();cloud_wire_init(receive,NULL);
    send_audio(0,7680);send_audio(7680,7680);speech_playback_done();advance(100);
    for(unsigned batch=0;batch<5;++batch) {
        send_audio(15360u+batch*7680u,7680);
        if(batch<4) advance(540);
        if(batch==1) CHECK(regs[0xac/4]==15360 && !(regs[0x9c/4]&2u));
    }
    speech_playback_done();
    while(queued || regs[0xa4/4]) advance(1);
    advance(1);
    CHECK(speech_diag.starts==2 && !regs[0xa8/4] && received==53760);
    // A short completed phrase starts immediately, below the reservoir target.
    uint32_t n=make_message(53760,6);CHECK(receive(message,n,NULL)==0);
    speech_playback_done();advance(1);
    CHECK((regs[0x9c/4]&2u) && received==53766);
    // User stop differs from disconnect: discard pending hardware and software
    // audio immediately, and no later tick may restart that old tail.
    send_audio(53766,7680);advance(2);CHECK(queued && regs[0xa4/4]);
    speech_playback_stop();
    CHECK(!queued && !regs[0xa4/4] && !(regs[0x9c/4]&2u));
    uint32_t played=regs[0xac/4];advance(1000);
    CHECK(regs[0xac/4]==played && !queued && !(regs[0x9c/4]&2u));
    expected_at=61446;n=make_message(61446,6);CHECK(receive(message,n,NULL)==0);
    speech_playback_done();advance(1);
    CHECK(regs[0x9c/4]&2u);
#ifdef Z7_LOW_LATENCY_PLAYBACK
    /* Fast 320 ms chunks arriving every 240 ms: start early, then preserve all
       12.8 s of signed PCM across the ring without speed-up or underruns. */
    memset(regs,0,sizeof(regs));received=expected_at=test_now=0;
    regs[0x98/4]=0x31594c50;speech_playback_init();cloud_wire_init(receive,NULL);
    send_audio(0,7680);advance(240);CHECK(!(regs[0x9c/4]&2u));
    send_audio(7680,7680);advance(240);CHECK(!(regs[0x9c/4]&2u));
    send_audio(15360,7680);advance(240);CHECK(!(regs[0x9c/4]&2u));
    send_audio(23040,7680);advance(20);CHECK(regs[0x9c/4]&2u);
    CHECK(voice_ms<800u);
    for(unsigned batch=4;batch<40;++batch) {advance(240);send_audio(batch*7680u,7680);}
    speech_playback_done();while(queued || regs[0xa4/4]) advance(1);advance(1);
    CHECK(received==307200 && regs[0xac/4]==received && speech_diag.starts==1 && !regs[0xa8/4]);
#endif
    puts("PASS_SPEECH_PLAYBACK real WS/JSON/base64, signed PCM order, >4k events, 12s ring wrap, jitter without underruns, prefill and short tails");
    return 0;
}
