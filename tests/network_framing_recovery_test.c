#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
#include "wifi_recover.h"
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
static unsigned divisor,peer=54,framing,live_recovery,used,reset_count;
static char modem_command[80];
static uint32_t modem_read(uint32_t off) {
    if(off==8) return divisor;
    if(off==36) return framing;
    if(off==4) return reg_read(off)|0x100u;
    return reg_read(off);
}
static void modem_write(uint32_t off,uint32_t value) {
    if(off==8) {divisor=value;return;}
    if(off==12) {if(live_recovery) {framing=0;head=tail=0;}return;}
    if(!live_recovery) {tls_write(off,value);return;}
    if(divisor!=peer) {used=0;return;}
    check(used+1<sizeof(modem_command),"bounded modem command");
    modem_command[used++]=(char)value;modem_command[used]=0;
    if(value!='\n') return;
    if(!strcmp(modem_command,"\r\n")) {}
    else if(!strcmp(modem_command,"AT\r\n")) enqueue("\r\nOK\r\n");
    else if(!strcmp(modem_command,"AT+RST\r\n")) {++reset_count;peer=434;enqueue("\r\nOK\r\n");}
    else if(!strcmp(modem_command,WIFI_STREAM_UART_COMMAND)) {peer=WIFI_STREAM_DIVISOR;enqueue("\r\nOK\r\n");}
    else if(!strcmp(modem_command,"AT+UART_CUR?\r\n")) {
        char response[80];snprintf(response,sizeof(response),"\r\n+UART_CUR:%u,8,1,0,0\r\nOK\r\n",WIFI_STREAM_BAUD);enqueue(response);
    }
    else for(unsigned i=0;i<used;++i) tls_write(16,(uint8_t)modem_command[i]);
    used=0;
}
static void discard(uint8_t b) {(void)b;}
int main(void) {
    tls_reset(0);tls_run();tls_close();
    network_init(modem_read,modem_write);ui_init(&state);clock_ms=0;
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);memcpy((void *)(network_config.credentials+33),"unit-pass",9);
    network_config.request_sequence=1;tls_run();check(network_info.phase==8,"initial TLS ready");
    divisor=54;check(network_stream_attach(discard),"stream adopts TLS");
    framing=1946;live_recovery=1;peer=434;network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==32,"framing invalidates failed session");
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error && !framing,"sticky framing no longer prevents reconnection");
    check(reset_count==1 && divisor==WIFI_STREAM_DIVISOR && peer==WIFI_STREAM_DIVISOR,"one volatile modem reset and verified high speed");
    check(network_stream_attach(discard),"cloud can resume after full TLS recovery");
    char corrupt_line[200];memset(corrupt_line,'x',sizeof(corrupt_line));
    enqueue_bytes((const uint8_t *)corrupt_line,sizeof(corrupt_line));
    network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==26,"corrupt AT line invalidates session");
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error && reset_count==2,"AT line corruption can recover instead of permanently freezing");
    network_info.phase=6;network_info.error=41;network_cloud_run(0);
    for(unsigned i=0;i<6000;++i) network_tick(&state,++clock_ms);
    check(reset_count==2,"rejected close remains stopped while user is paused");
    network_cloud_run(1);
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    check(network_info.phase==8 && !network_info.error && reset_count==3,
          "resume after rejected close verifies AT/baud before a new TLS session");
    puts("PASS_NETWORK_FRAMING_RECOVERY latched error 1946, rebooted 115200 peer and corrupt AT line recover through verified serial/TLS reset");
    return 0;
}
