#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "network.h"
volatile struct wifi_status wifi_info;
static struct ui_state state;
static unsigned char fifo[65536],last_ack[32];
static uint32_t head,tail,command_used,raw_left,raw_used,echo,scenario,clock_ms,checks,join_seen,ack_count,request,sysstore_verified;
static char command[320];
static void check(int yes,const char *s) {++checks;if(!yes) {fprintf(stderr,"FAIL %s phase=%u err=%u\n",s,network_info.phase,network_info.error);exit(1);}}
static void enqueue_bytes(const unsigned char *p,uint32_t n) {for(uint32_t i=0;i<n;++i) fifo[tail++%sizeof(fifo)]=p[i];}
static void enqueue(const char *s) {enqueue_bytes((const unsigned char *)s,(uint32_t)strlen(s));}
static uint32_t reg_read(uint32_t offset) {
    if(offset==4) return (head!=tail?1u:0u)|2u;
    if(offset==20) return head==tail?0:(0x80000000u|fifo[head++%sizeof(fifo)]);
    if(offset==32) return scenario==5?1:0;
    if(offset==36) return scenario==6?1:0;
    fprintf(stderr,"Unexpected offset %u\n",offset);exit(1);
}
static void reg_write(uint32_t offset,uint32_t value) {
    check(offset==16,"PS writes only TX register");
    if(raw_left) {
        last_ack[raw_used++]=(unsigned char)value;
        if(--raw_left==0) {++ack_count;if(scenario!=7) enqueue("\r\nSEND OK\r\n");}
        return;
    }
    check(command_used+1<sizeof(command),"bounded AT command");
    command[command_used++]=(char)value;command[command_used]=0;
    if(value!='\n') return;
    if(echo) {for(uint32_t i=0;i<command_used;++i) {if(command[i]=='\n') enqueue("\r");enqueue_bytes((unsigned char *)command+i,1);}}
    if(strcmp(command,"ATE0\r\n")==0) {if(scenario!=1) echo=0;enqueue("\r\nOK\r\n");}
    else if(strcmp(command,"AT+GMR\r\n")==0) enqueue("\r\nAT version:2.2.1.0\r\nSDK version:3.4\r\n\r\nOK\r\n");
    else if(strcmp(command,"AT+SYSSTORE=0\r\n")==0) {
        if(scenario==8) enqueue("\r\nERROR\r\n");
        else {sysstore_verified=1;enqueue("\r\nOK\r\n");}
    }
    else if(strncmp(command,"AT+CWJAP=",9)==0) {
        check(network_info.echo_verified==1,"strict echo proof precedes private join");++join_seen;
        check(sysstore_verified==1,"volatile storage acknowledgement precedes private join");
        if(scenario==2) enqueue("\r\nFAIL\r\n");else enqueue("WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
    } else if(strcmp(command,"AT+CIFSR\r\n")==0) enqueue("\r\n+CIFSR:STAIP,\"192.0.2.42\"\r\n+CIFSR:STAMAC,\"00:11:22:33:44:55\"\r\n\r\nOK\r\n");
    else if(strncmp(command,"AT+CIPSEND=",11)==0) {
        enqueue("\r\nOK\r\n");if(scenario!=3) {enqueue("> ");raw_left=32;raw_used=0;}
    } else if(strcmp(command,"AT+CIPSERVER=0\r\n")==0 || strcmp(command,"AT+CWMODE=1\r\n")==0 ||
              strcmp(command,"AT+CIPMUX=1\r\n")==0 || strcmp(command,"AT+CIPDINFO=0\r\n")==0 ||
              strcmp(command,"AT+CIPRECVMODE=0\r\n")==0 || strcmp(command,"AT+CIPSERVER=1,8266\r\n")==0)
        enqueue("\r\nOK\r\n");
    else enqueue("\r\nERROR\r\n"); // Explicit supported command set; obsolete _CUR commands fail.
    command_used=0;
}
static void tick(void) {clock_ms+=100;network_tick(&state,clock_ms);}
static void reset(uint32_t mode) {
    head=tail=command_used=raw_left=raw_used=clock_ms=join_seen=ack_count=request=sysstore_verified=0;echo=1;scenario=mode;
    wifi_info.phase=2;network_init(reg_read,reg_write);ui_init(&state);check(ui_control(&state,UI_START),"init model");
    network_config.action=1;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);memcpy((void *)(network_config.credentials+33),"unit-pass",9);
    network_config.request_sequence=1;tick();
    for(uint32_t i=0;i<100;++i) check(network_config.credentials[i]==0,"private mailbox promptly wiped");
}
static void ready(void) {for(uint32_t i=0;i<80 && network_info.phase!=3 && network_info.phase!=6;++i) tick();}
static uint32_t make(unsigned char *f,uint32_t type,uint32_t seg,uint32_t rev,uint32_t src,const char *text) {
    uint32_t n=text?(uint32_t)strlen(text):0;
    uint32_t w[]={0x31425553,1,++request,type,state.epoch,seg,rev,src,n};
    for(uint32_t i=0;i<9;++i) net_put(f+4*i,w[i]);
    if(n) memcpy(f+36,text,n);
    return 36+n;
}
static void ipd(const unsigned char *p,uint32_t n) {
    char prefix[40];snprintf(prefix,sizeof(prefix),"\r\n+IPD,0,%u:",n);enqueue(prefix);enqueue_bytes(p,n);
}
static void send_frame(uint32_t type,uint32_t seg,uint32_t rev,uint32_t src,const char *text,int split) {
    unsigned char f[NET_FRAME_BYTES];uint32_t n=make(f,type,seg,rev,src,text),old=ack_count;
    if(split) {ipd(f,7);tick();ipd(f+7,29);tick();if(n>36) ipd(f+36,n-36);}
    else ipd(f,n);
    for(uint32_t i=0;i<70 && ack_count==old && network_info.phase!=6;++i) tick();
    check(ack_count==old+1,"actual ACK transmitted");
    check(net_word(last_ack)==0x314b4341 && net_word(last_ack+8)==request,"ACK belongs to request");
    for(uint32_t i=0;i<3;++i) tick();
}
int main(void) {
    reset(0);ready();check(network_info.phase==3 && join_seen==1 && network_info.sta_ip==0xc000022a,"volatile join/IP/server sequence");
    check(network_info.echo_verified && network_owns_ui() && state.final_count==0,"network mode explicit");
    enqueue("\r\n0,CONNECT\r\n");tick();check(network_info.phase==4,"connection ownership");
    send_frame(0,0,0,0,0,1);check(net_word(last_ack+16)==NET_OK,"fragmented header hello");
    send_frame(3,1,1,0,"Before we begin, please check the interface.",1);check(net_word(last_ack+16)==NET_OK,"fragmented ASR accepted");
    send_frame(4,1,1,1,0,0);check(net_word(last_ack+16)==NET_OK,"translating accepted");
    send_frame(5,1,1,1,"开始之前，请确认每个人都能清楚地听到发言。",1);
    check(net_word(last_ack+16)==NET_OK && state.final_count==1 && state.history_count==1,"Chinese network final");
    send_frame(3,2,1,0,"\xf0\x9f\x98\x80",0);check(net_word(last_ack+16)==NET_GLYPH && state.final_count==1,"unknown glyph safely rejected");
    unsigned char early[NET_FRAME_BYTES];uint32_t count_before=ack_count;
    uint32_t early_size=make(early,0,0,0,0,0);ipd(early,early_size);
    for(uint32_t i=0;i<40 && ack_count==count_before;++i) tick();
    check(ack_count==count_before+1,"first ACK available to peer before serial SEND OK consumed");
    early_size=make(early,3,2,1,0,"OK > ERROR");ipd(early,early_size);
    for(uint32_t i=0;i<40 && ack_count<count_before+2 && network_info.phase!=6;++i) tick();
    check(ack_count==count_before+2 && net_word(last_ack+16)==NET_OK && network_info.phase==4,"next request buffered while prior serial completion pending; payload tokens stay data");
    for(uint32_t i=0;i<3;++i) tick();
    enqueue("\r\n0,CLOSED\r\n");tick();check(state.session==UI_PAUSED && state.final_count==1 && network_info.phase==3,"disconnect retains final");
    reset(1);ready();check(network_info.phase==6 && network_info.error==30 && !join_seen,"CRCR echo never certifies echo-off or releases secret");
    reset(2);ready();check(network_info.phase==6 && network_info.error==29,"join error fails independently");
    reset(8);ready();check(network_info.phase==6 && network_info.error==29 && network_info.last_code==3 && !join_seen,
          "unsupported volatile storage blocks private join");
    reset(3);ready();enqueue("\r\n0,CONNECT\r\n");tick();unsigned char f[36];(void)make(f,0,0,0,0,0);ipd(f,36);
    for(uint32_t i=0;i<60 && network_info.phase!=6;++i) tick();
    check(network_info.phase==6 && network_info.error==27 && ack_count==0,"OK cannot replace CIPSEND prompt");
    reset(0);ready();enqueue("\r\nWIFI DISCONNECT\r\n");tick();check(network_info.phase==6 && network_info.error==23,"Wi-Fi loss blocks network");
    reset(5);check(network_info.phase==6 && network_info.error==32,"UART overflow blocks network");
    reset(6);check(network_info.phase==6 && network_info.error==32,"UART framing blocks network");
    reset(0);ready();enqueue("\r\n0,CONNECT\r\n");tick();
    unsigned char oversized[36]={0};net_put(oversized+32,769);
    enqueue("\r\n+IPD,0,3000:");enqueue_bytes(oversized,36);tick();
    check(network_info.phase==6 && network_info.error==21,"oversized frame leaves stream faulted");
    uint32_t consumed=network_info.received_bytes;
    network_config.action=1;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);memcpy((void *)(network_config.credentials+33),"unit-pass",9);
    network_config.request_sequence=2;enqueue("ERROR > OK");tick();
    check(network_info.phase==6 && network_info.error==35 && network_info.config_ack==2 && network_info.received_bytes==consumed,
          "configuration retry preserves unfinished payload framing until restart");
    for(uint32_t i=0;i<100;++i) check(network_config.credentials[i]==0,"blocked recovery wipes credentials");
    reset(7);ready();enqueue("\r\n0,CONNECT\r\n");tick();
    (void)make(f,0,0,0,0,0);ipd(f,36);
    for(uint32_t i=0;i<60 && !ack_count && network_info.phase!=6;++i) tick();
    check(ack_count==1 && network_info.phase==4,"hold serial completion after prompt padding");
    enqueue("+IPD,1,9:\r\nERROR\r\n");tick();
    check(network_info.phase==4 && network_info.error==0,"bare IPD after prompt space keeps payload outside AT parser");
    enqueue("\r\nSEND OK\r\n");tick();
    printf("PASS_NETWORK_RUNTIME checks=%u\n",checks);return 0;
}
