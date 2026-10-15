#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
#include "stream_pilot.h"
volatile struct wifi_status wifi_info;
static struct ui_state ui;
static uint8_t fifo[8192],file[STREAM_PILOT_CAPACITY],received[STREAM_PILOT_CAPACITY+16],sent[4096];
static uint32_t head,tail,remaining,used,sends,now,hold_ack,checks;
static char command[64];static uint32_t command_bytes;
static uint8_t consumed[32];static uint32_t consume_count;
static void consume_byte(uint8_t byte) {if(consume_count<sizeof(consumed)) consumed[consume_count++]=byte;}
static void check(int value,const char *name) {++checks;if(!value) {fprintf(stderr,"FAIL %s phase=%u error=%u\n",name,stream_pilot.phase,stream_pilot.error);exit(1);}}
static void enqueue(const void *bytes,uint32_t count) {
    const uint8_t *p=bytes;for(uint32_t i=0;i<count;++i) fifo[tail++%sizeof(fifo)]=p[i];
}
static void text(const char *s) {enqueue(s,(uint32_t)strlen(s));}
static uint32_t read_reg(uint32_t offset) {
    if(offset==4) return 2u|(head!=tail?1u:0u);
    if(offset==20) return head==tail?0u:0x80000000u|fifo[head++%sizeof(fifo)];
    if(offset==32 || offset==36) return 0;
    check(0,"unexpected register");return 0;
}
static void write_reg(uint32_t offset,uint32_t byte) {
    check(offset==16,"only TX writes");
    if(remaining) {sent[used++]=(uint8_t)byte;if(!--remaining && !hold_ack) text("\r\nSEND OK\r\n");return;}
    command[command_bytes++]=(char)byte;
    if(byte=='\n') {
        command[command_bytes]=0;check(!strncmp(command,"AT+CIPSEND=0,",13),"normal CIPSEND only");
        remaining=(uint32_t)strtoul(command+13,0,10);command_bytes=0;++sends;text("\r\nOK\r\n> ");
    }
}
static void put(uint32_t at,uint32_t v) {for(uint32_t i=0;i<4;++i) file[at+i]=(uint8_t)(v>>(i*8));}
static void init(void) {
    head=tail=remaining=used=sends=now=hold_ack=command_bytes=0;
    memset(file,0,sizeof(file));memset(received,0xcc,sizeof(received));
    ui_init(&ui);wifi_info.phase=2;network_init(read_reg,write_reg);stream_pilot_init(file,received);
    put(0,0x31465453u);put(4,2);put(8,36);put(12,0);put(16,4);memcpy(file+20,"ONE!",4);
    put(24,80);put(28,4);memcpy(file+32,"TWO!",4);
}
static void tick(void) {network_tick(&ui,++now);stream_pilot_tick(now);}
static void run(uint32_t count) {while(count--) tick();}
int main(void) {
    init();stream_pilot.request=1;run(60);
    check(sends==1 && used==4 && !memcmp(sent,"ONE!",4),"second frame waits for audio clock");
    run(40);check(stream_pilot.phase==2 && stream_pilot.frames==2 && used==8 && !memcmp(sent,"ONE!TWO!",8),"timed file sent exactly");
    check(stream_pilot.sent_ms>=80 && stream_pilot.max_lag_ms<=1,"bounded local pacing");
    stream_pilot.request=2;tick();check(stream_pilot.phase==3 && network_info.phase==0,"owner released after completion");
    stream_pilot.request=3;tick();check(stream_pilot.phase==0 && stream_pilot.ack==3 && stream_pilot.received==0,"released pilot resets without reloading the UI");
    stream_pilot.request=1;tick();check(stream_pilot.phase==1 && stream_pilot.ack==1,"next job attaches after explicit reset");
    init();hold_ack=1;stream_pilot.request=1;run(10);
    const char payload[]="\r\nSEND OK\r\n> ERROR\r\n";char prefix[40];
    snprintf(prefix,sizeof(prefix),"\r\n+IPD,0,%u:",(unsigned)(sizeof(payload)-1));text(prefix);enqueue(payload,7);tick();
    check(network_stream_busy(),"payload SEND OK does not certify serial completion");
    stream_pilot.request=2;tick();check(stream_pilot.phase==1,"cannot release pending serial send");
    enqueue(payload+7,sizeof(payload)-1-7);text("\r\nSEND OK\r\n");run(2);
    check(stream_pilot.phase==3 && stream_pilot.received==sizeof(payload)-1 && !memcmp(received,payload,sizeof(payload)-1),"fragmented binary IPD preserved before release");
    init();put(8,35);stream_pilot.request=1;tick();
    check(stream_pilot.phase==6 && stream_pilot.error==1 && !sends,"truncated file rejected before UART ownership");
    init();hold_ack=1;stream_pilot.request=1;run(4010);
    check(stream_pilot.phase==6 && network_info.error==27,"missing SEND OK times out without retry");
    init();stream_pilot.request=1;run(100);
    uint8_t block[1024];memset(block,0x5a,sizeof(block));
    for(uint32_t i=0;i<STREAM_PILOT_CAPACITY/sizeof(block)+1;++i) {text("\r\n+IPD,0,1024:");enqueue(block,sizeof(block));tick();}
    check(stream_pilot.phase==6 && stream_pilot.error==3 && stream_pilot.received==STREAM_PILOT_CAPACITY,"bounded incoming buffer reports overflow");
    for(uint32_t i=STREAM_PILOT_CAPACITY;i<sizeof(received);++i) check(received[i]==0xcc,"RX canary intact");
    init();consume_count=0;stream_pilot_set_receiver(consume_byte);stream_pilot.request=1;run(100);
    text("\r\n+IPD,0,5:hello");run(210);
    check(consume_count==5 && !memcmp(consumed,"hello",5) && stream_pilot.received==5,"consumer receives exact TCP bytes and accounting");
    check(received[0]==0xcc && received[4]==0xcc,"consumer bypasses whole-session DDR capture");
    init();stream_pilot.request=1;run(100);text("\r\n+IPD,0,5:again");run(210);
    check(stream_pilot.received==5 && !memcmp(received,"again",5) && consume_count==5,"init restores legacy capture without stale callback");
    printf("PASS_NETWORK_STREAM checks=%u\n",checks);return 0;
}
