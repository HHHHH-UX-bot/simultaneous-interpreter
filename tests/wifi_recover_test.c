#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_recover.h"
static unsigned divisor,peer,head,tail,used,resets,clears,now_ms,unresponsive,bad_query,stuck_tx;
static unsigned char rx[512];static char command[80];
static void check(int value,const char *what) {if(!value) {fprintf(stderr,"FAIL %s phase=%u\n",what,wifi_recover_phase());exit(1);}}
static void enqueue(const char *p) {while(*p) rx[tail++%sizeof(rx)]=(unsigned char)*p++;}
static uint32_t read_reg(uint32_t off) {
    if(off==4) return 0x102u|(head!=tail?1u:0u)|(stuck_tx?4u:0u);
    if(off==8) return divisor;
    if(off==20) return head!=tail?0x80000000u|rx[head++%sizeof(rx)]:0;
    check(0,"unexpected register read");return 0;
}
static void write_reg(uint32_t off,uint32_t value) {
    if(off==12) {if(value&3u) head=tail=0;++clears;return;}
    if(off==8) {divisor=value;return;}
    check(off==16,"UART writes only");
    if(divisor!=peer || unresponsive) {used=0;return;}
    check(used+1<sizeof(command),"bounded AT command");command[used++]=(char)value;command[used]=0;
    if(value!='\n') return;
    if(!strcmp(command,"\r\n")) {}
    else if(!strcmp(command,"AT\r\n")) enqueue("\r\nOK\r\n");
    else if(!strcmp(command,"AT+RST\r\n")) {++resets;peer=434;enqueue("\r\nOK\r\nready\r\n");}
    else if(!strcmp(command,WIFI_STREAM_UART_COMMAND)) {enqueue("\r\nOK\r\n");peer=WIFI_STREAM_DIVISOR;}
    else if(!strcmp(command,"AT+UART_CUR?\r\n")) {
        char response[80];
        unsigned actual=80000000u/(80000000u/WIFI_STREAM_BAUD);
        snprintf(response,sizeof(response),"\r\n+UART_CUR:%u,8,1,0,0\r\nOK\r\n",bad_query?115200u:actual);enqueue(response);
    }
    else check(0,"no credentials or Flash commands during link recovery");
    used=0;
}
static int trial(unsigned initial_peer,unsigned silence,unsigned wrong_reply,unsigned busy) {
    divisor=54;peer=initial_peer;head=tail=used=resets=clears=0;now_ms=0;
    unresponsive=silence;bad_query=wrong_reply;stuck_tx=busy;
    wifi_recover_begin(read_reg,write_reg,now_ms);
    int result=0;while(!result && now_ms<10000u) result=wifi_recover_tick(++now_ms);
    return result;
}
int main(void) {
    check(trial(25,0,0,0)==1 && resets==1 && divisor==WIFI_STREAM_DIVISOR && peer==WIFI_STREAM_DIVISOR,"recover high-speed peer, reset, query actual speed");
    check(trial(54,0,0,0)==1 && resets==1 && divisor==WIFI_STREAM_DIVISOR && peer==WIFI_STREAM_DIVISOR,"migrate previous 921600 peer");
    check(trial(434,0,0,0)==1 && resets==1 && divisor==WIFI_STREAM_DIVISOR && peer==WIFI_STREAM_DIVISOR,"recover peer rebooted at 115200");
    check(trial(54,1,0,0)==-1 && now_ms<4000,"silent module fails within a bound across known rates");
    check(trial(54,0,1,0)==-1,"bare OK cannot certify wrong queried speed");
    check(trial(54,0,0,1)==-1 && now_ms<1000,"physical TX ownership times out rather than hanging");
    puts("PASS_WIFI_RECOVER fast/slow peers, volatile reset, verified baud, silent/wrong-speed/stuck-TX failures bounded");
    return 0;
}
