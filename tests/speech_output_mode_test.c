#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t regs[64];
static void write_reg(uint32_t offset,uint32_t value) {
    if(offset==0xa0u) ++regs[0xa4u/4u];
    else if(offset==0x9cu) {
        regs[offset/4u]=value&0xbu;
        if(value&4u) regs[0xa4u/4u]=0;
    } else regs[offset/4u]=value;
}
#define SPEECH_REG(offset) regs[(offset)/4u]
#define SPEECH_WRITE(offset,value) write_reg(offset,value)
#include "../software/network_ui/speech_playback.c"
static void check_output(void) {
#ifdef Z7_TEXT_ONLY_RESPONSE
    const unsigned expected=0;
#else
    const unsigned expected=1;
#endif
    if((regs[0x9cu/4u]&1u)!=expected) {
        fprintf(stderr,"DAC selection expected=%u actual=%u\n",expected,regs[0x9cu/4u]&1u);
        exit(1);
    }
}
int main(void) {
    regs[0x98u/4u]=0x31594c50u;regs[0x9cu/4u]=1u; /* Warm load after translated audio. */
    speech_playback_init();check_output();
    speech_playback_tick(1);check_output();
    if(!speech_playback_audio("AAABAA==",8)) return 2;
    speech_playback_done();speech_playback_tick(2);check_output();
    regs[0xa4u/4u]=0;speech_playback_tick(3);check_output();
    speech_playback_stop();check_output();speech_playback_tick(4);check_output();
    puts("PASS_SPEECH_OUTPUT_MODE warm load, idle, completion and stop");return 0;
}
