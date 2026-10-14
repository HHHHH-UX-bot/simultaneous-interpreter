#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_at_probe.h"
static unsigned char fifo[4096];
static uint32_t head,tail,sent,received,overflows,framing,mode,echo,blocked,clock_ms;
static char command[64];static uint32_t command_size,checks;
static void check(int v,const char *m) {++checks;if(!v) {fprintf(stderr,"FAIL %s\n",m);exit(1);}}
static void enqueue(const char *s) {while(*s) {fifo[tail++%sizeof(fifo)]=(unsigned char)*s++;++received;}}
static uint32_t read_reg(uint32_t o) {
    switch(o) {
    case 0:return mode==1?0:0x57494631u;
    case 4:return (head!=tail?1u:0u)|(blocked?0u:2u);
    case 8:return mode==2?100u:434u;
    case 20:if(head==tail) return 0;return 0x80000000u|fifo[head++%sizeof(fifo)];
    case 32:return overflows;case 36:return framing;case 40:return sent;case 44:return received;
    default:fprintf(stderr,"Unexpected register read %u\n",o);exit(1);
    }
}
static void write_reg(uint32_t o,uint32_t v) {
    if(o==12) {check(v==15,"atomic FIFO/stat reset");head=tail=sent=received=0;return;}
    check(o==16,"only TX_DATA writes");check(command_size+1<sizeof(command),"bounded outbound command");
    command[command_size++]=(char)v;command[command_size]=0;++sent;
    if(v!='\n') return;
    if(mode==3) {command_size=0;return;}
    if(echo) enqueue(command);
    if(mode==4) enqueue("\r\nERROR\r\n");
    else if(strcmp(command,"ATE0\r\n")==0) {if(mode!=5) echo=0;enqueue("\r\nOK\r\n");}
    else if(strcmp(command,"AT+GMR\r\n")==0) {
        if(mode!=6) enqueue("\r\nAT version:2.2.1.0\r\nSDK version:3.4\r\n");
        enqueue("\r\nOK\r\n");
    } else enqueue("\r\nOK\r\n");
    command_size=0;
}
static void reset(uint32_t scenario) {
    head=tail=sent=received=overflows=framing=command_size=clock_ms=blocked=0;mode=scenario;echo=1;
    wifi_probe_init(read_reg,write_reg,clock_ms);
}
static void run(uint32_t count) {for(uint32_t i=0;i<count;++i) {clock_ms+=700;wifi_probe_tick(clock_ms);}}
int main(void) {
    reset(0);run(10);check(wifi_info.phase==2 && wifi_info.error==0,"PS-owned complete probe");
    check(wifi_info.at_ok && wifi_info.echo_off && wifi_info.firmware_ok && wifi_info.firmware_hash,"AT/echo/firmware separate evidence");
    check((wifi_info.sequence&1u)==0,"coherent report");
    reset(1);check(wifi_info.error==1 && !sent,"wrong identity no TX");
    reset(2);check(wifi_info.error==2 && !sent,"wrong divider no TX");
    reset(3);run(8);check(wifi_info.error==8,"bounded absent module timeout");
    reset(4);run(5);check(wifi_info.error==5,"explicit AT error");
    reset(5);run(9);check(wifi_info.error==6 && !wifi_info.echo_off,"ATE0 OK cannot prove echo disabled");
    reset(6);run(12);check(wifi_info.error==7 && !wifi_info.firmware_ok,"GMR OK without version rejected");
    reset(0);overflows=1;run(1);check(wifi_info.error==3,"overflow blocks probe");
    reset(0);framing=1;run(1);check(wifi_info.error==3,"framing error blocks probe");
    reset(0);blocked=1;run(8);check(wifi_info.error==8 && !sent,"blocked TX has deadline");
    reset(0);clock_ms=0xfffffff0u;wifi_probe_init(read_reg,write_reg,clock_ms);run(10);
    check(wifi_info.phase==2,"uint32 timer wrap");
    reset(3);wifi_probe_tick(100);for(uint32_t i=0;i<520;++i) enqueue("x");run(1);check(wifi_info.error==4,"oversize response rejects");
    reset(3);enqueue("\r\nOK\r\n");wifi_probe_tick(100);
    check(!wifi_info.at_ok && !wifi_info.echo_off,"pre-send OK never certifies AT");
    reset(3);wifi_probe_tick(100);enqueue("\r\nOK\r\n");wifi_probe_tick(3700);
    check(wifi_info.error==8 && !wifi_info.at_ok,"late OK cannot bypass deadline");
    reset(5);wifi_probe_tick(100);wifi_probe_tick(200);
    check(wifi_info.step==2,"verification transaction selected");
    enqueue("\r\nOK\r\n");wifi_probe_tick(300);
    check(wifi_info.error==6 && !wifi_info.echo_off && echo,"pre-send OK cannot forge echo-off proof");
    printf("PASS_PS_WIFI_PROBE_HOST checks=%u\n",checks);return 0;
}
