#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t regs[64],received;
static int16_t output[128];
static void write_hw(unsigned offset,unsigned value) {
    if(offset==0xa0) {if(received<128) output[received]=(int16_t)value;++received;++regs[0xa4/4];}
    else {regs[offset/4]=value;if(offset==0x9c && (value&4)) regs[0xa4/4]=0;}
}
#define SPEECH_REG(offset) regs[(offset)/4]
#define SPEECH_WRITE(offset,value) write_hw(offset,value)
#include "../software/network_ui/speech_playback.c"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"tts controls failure line %d\n",__LINE__);exit(1);}}while(0)
static void reset(void) {memset(regs,0,sizeof(regs));received=0;regs[0x98/4]=0x31594c50;speech_playback_init();speech_playback_settings(1,100,100);}
static void four(void) {CHECK(speech_playback_audio("/38AgBAn8Ng=",12));}
int main(void) {
    reset();speech_playback_settings(1,70,100);four();speech_playback_done();speech_playback_tick(100);
    CHECK(received==4 && output[0]==22936 && output[1]==-22937 && output[2]==7000 && output[3]==-7000);
    struct speech_status s;speech_playback_status(&s);CHECK(s.state==3 && s.latency_valid && s.latency_ms==100);
    reset();speech_playback_settings(1,0,100);four();speech_playback_done();speech_playback_tick(1);
    CHECK(received==4 && !output[0] && !output[1] && !output[2] && !output[3]);
    reset();speech_playback_settings(1,100,80);four();speech_playback_done();speech_playback_tick(1);
    CHECK(received==5 && speech_diag.to_hardware==4 && output[0]==32767 && output[1]==32767);
    reset();speech_playback_settings(1,100,120);for(unsigned i=0;i<3;++i) four();speech_playback_done();speech_playback_tick(1);
    CHECK(received==10 && speech_diag.to_hardware==12 && !queued);
    reset();four();speech_playback_done();speech_playback_tick(1);CHECK(regs[0xa4/4]==4);
    speech_playback_settings(0,70,100);CHECK(!queued && !regs[0xa4/4]);four();speech_playback_tick(2);
    CHECK(!queued && received==4);speech_playback_status(&s);CHECK(s.state==0);
    speech_playback_settings(1,100,100);four();speech_playback_done();speech_playback_tick(3);CHECK(received==8);
    puts("PASS_TTS_CONTROLS signed digital volume, mute, 0.8/1.0/1.2 speed, speech-off tail discard and T2 timing");return 0;
}
