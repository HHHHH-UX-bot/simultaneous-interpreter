/* Replay public chunk timestamps/lengths; no recorded audio or transcript. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t regs[64];
static void write_reg(uint32_t offset,uint32_t value) {
    if(offset==0xa0u) {if(regs[0xa4/4]>=8192u) abort();++regs[0xa4/4];}
    else {
        regs[offset/4u]=value;
        if(offset==0x9cu && (value&4u)) regs[0xa4/4]=regs[0xa8/4]=regs[0xac/4]=0;
    }
}
#define SPEECH_REG(offset) regs[(offset)/4u]
#define SPEECH_WRITE(offset,value) write_reg(offset,value)
#include "../software/network_ui/speech_playback.c"
static uint32_t clock_ms,total;
static void advance(uint32_t until) {
    while(clock_ms<until) {
        speech_playback_tick(clock_ms++);
        if(regs[0x9c/4]&2u) {
            uint32_t n=regs[0xa4/4]<24u?regs[0xa4/4]:24u;
            if(n<24u && !(regs[0x9c/4]&8u)) ++regs[0xa8/4];
            regs[0xa4/4]-=n;regs[0xac/4]+=n;
        }
    }
}
int main(void) {
    uint32_t ms,kind,samples;static char zeros[65536];
    memset(zeros,'A',sizeof(zeros));regs[0x98/4]=0x31594c50u;speech_playback_init();
    while(scanf("%u %u %u",&ms,&kind,&samples)==3) {
        advance(ms);
        if(kind==1u) {
            uint32_t n=(samples*2u+2u)/3u*4u,padding=n/4u*3u-samples*2u;
            if(n>sizeof(zeros)) return 2;
            if(padding) memset(zeros+n-padding,'=',padding);
            if(!speech_playback_audio(zeros,n)) return 3;
            if(padding) memset(zeros+n-padding,'A',padding);
            total+=samples;
        } else if(kind==2u) speech_playback_done();
    }
    advance(clock_ms+30000u);
    int pass=regs[0xac/4]==total && !regs[0xa8/4] && !speech_diag.overflow_chunks;
    printf("{\"passed\":%s,\"samples\":%u,\"played\":%u,\"underruns\":%u,\"starts\":%u,\"timeline\":[",pass?"true":"false",total,regs[0xac/4],regs[0xa8/4],speech_diag.starts);
    for(uint32_t i=0;i<speech_timeline[0];++i) printf("%s[%u,%u,%u,%u]",i?",":"",speech_timeline[1+i*4],speech_timeline[2+i*4],speech_timeline[3+i*4],speech_timeline[4+i*4]);
    puts("]}");return pass?0:1;
}
