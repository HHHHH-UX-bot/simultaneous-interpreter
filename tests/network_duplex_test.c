#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
volatile struct wifi_status wifi_info;
static uint8_t rx[65536],tx[4096];
static unsigned head,tail,received,remaining,tx_used,commands,command_used;
static unsigned injected,received_during_send;
static unsigned phy_busy;
static char command[64];
static const char payload_control[]="\r\nSEND OK\r\n> ERROR\r\n";
static void enqueue(const char *s) {while(*s) rx[tail++%sizeof(rx)]=(uint8_t)*s++;}
static void consume(uint8_t value) {
    uint8_t expected=received<sizeof(payload_control)-1u?(uint8_t)payload_control[received]:0x55;
    if(value!=expected) {puts("bad payload");exit(1);}++received;
    if(remaining && injected) ++received_during_send;
}
#ifdef Z7_TRANSLATE_AUDIO
void cache_clean_range(const void *start,unsigned bytes) {(void)start;(void)bytes;}
#endif
static uint32_t read_reg(uint32_t offset) {
    if(offset==4) return 2u|(head!=tail?1u:0u)|(phy_busy?4u:0u);
    if(offset==20) return head!=tail?0x80000000u|rx[head++%sizeof(rx)]:0;
    return 0;
}
static void write_reg(uint32_t offset,uint32_t value) {
    if(offset!=16) return;
    if(remaining) {
        if(!injected) {
            injected=1;enqueue("\r\n+IPD,0,8192:");
            for(unsigned i=0;i<8192;++i) rx[tail++%sizeof(rx)]=0x55;
        }
        tx[tx_used++]=(uint8_t)value;
        if(!--remaining) {phy_busy=1;enqueue("\r\nSEND OK\r\n");}
        return;
    }
    command[command_used++]=(char)value;
    if(value=='\n') {
        command[command_used]=0;
        if(strncmp(command,"AT+CIPSEND=0,",13)) {puts("bad command");exit(1);}
        remaining=(unsigned)strtoul(command+13,NULL,10);
        ++commands;command_used=0;enqueue("\r\nOK\r\n> ");
    }
}
int main(void) {
    struct ui_state ui;ui_init(&ui);wifi_info.phase=2;
    network_init(read_reg,write_reg);
    if(!network_stream_attach(consume)) return 1;
    enqueue("\r\n+IPD,0,32768:");
    enqueue(payload_control);
    for(unsigned i=sizeof(payload_control)-1u;i<32768;++i) rx[tail++%sizeof(rx)]=0x55;
    uint8_t audio[640];for(unsigned i=0;i<sizeof(audio);++i) audio[i]=(uint8_t)i;
    if(!network_send(audio,sizeof(audio))) return 2;
    network_tick(&ui,1);
    if(commands!=1 || received<2000 || head==tail) {puts("CIPSEND starved by continuous IPD");return 3;}
    for(unsigned i=2;i<200;++i) network_tick(&ui,i);
    // The UART may finish while one RX drain is already in progress. Preserve
    // SEND OK seen in TX phase, but don't release before the final stop bit.
    if(!network_stream_busy()) {puts("physical TX ownership released early");return 6;}
    phy_busy=0;network_tick(&ui,200);
    if(network_stream_busy() || received!=40960 || tx_used!=sizeof(audio) || memcmp(audio,tx,sizeof(audio))) return 4;
#ifdef Z7_TRANSLATE_AUDIO
    if(!received_during_send) {puts("downlink blocked during paced payload TX");return 5;}
#endif
    puts("PASS_NETWORK_DUPLEX microphone CIPSEND progresses with continuous IPD; all incoming and outgoing bytes exact");
    return 0;
}
