#define main legacy_suite
#include "network_runtime_test.c"
#undef main
static char tls_tx[320];
static uint32_t tls_used,tls_case,time_queries,connect_queries,close_queries,server_queries,wrong_cn;
static void tls_write(uint32_t offset,uint32_t value) {
    if(offset==8 || offset==12) return; /* UART divider and FIFO reset. */
    check(offset==16 && tls_used+1<sizeof(tls_tx),"bounded TLS command");
    tls_tx[tls_used++]=(char)value;tls_tx[tls_used]=0;
    if(value!='\n') return;
    if(!strncmp(tls_tx,"AT+CIPDOMAIN=",13)) enqueue(tls_case==1?"\r\nOK\r\n":"\r\n+CIPDOMAIN:192.0.2.10\r\nOK\r\n");
    else if(!strcmp(tls_tx,"AT+CIPSNTPTIME?\r\n")) {
        ++time_queries;
        enqueue(tls_case==2 || time_queries==1?"\r\n+CIPSNTPTIME:Thu Jan  1 00:00:00 1970\r\nOK\r\n":
            "\r\n+CIPSNTPTIME:Fri Oct 16 03:00:00 2026\r\nOK\r\n");
    } else if(!strcmp(tls_tx,"AT+CIPSSLCCONF?\r\n")) enqueue(tls_case==3?"\r\n+CIPSSLCCONF:0,0,0,0\r\nOK\r\n":"\r\n+CIPSSLCCONF:0,2,0,0\r\nOK\r\n");
    else if(!strncmp(tls_tx,"AT+CIPSTART=",12)) {
        ++connect_queries;
        if(tls_case==4) check(wrong_cn,"negative trial sends wrong CN before connect");
        enqueue(tls_case==4?"\r\nERROR\r\n":tls_case==5?"\r\nOK\r\n":tls_case==7?"\r\n0,CONNECT\r\nOK\r\n0,CLOSED\r\n":"\r\n0,CONNECT\r\nOK\r\n");
    } else if(!strcmp(tls_tx,"AT+CIPCLOSE=0\r\n")) {
        ++close_queries;enqueue(tls_case==6 || tls_case==4?"\r\nERROR\r\n":"\r\n0,CLOSED\r\nOK\r\n");
    } else if(!strncmp(tls_tx,"AT+CIPSNTPCFG=",14) || !strncmp(tls_tx,"AT+CIPSSL",9) || !strcmp(tls_tx,"AT+CIPMUX=0\r\n") || !strcmp(tls_tx,"AT+CIPRECVMODE=1\r\n")) {
        if(!strcmp(tls_tx,"AT+CIPSSLCCN=0,\"wrong.invalid\"\r\n")) wrong_cn=1;
        enqueue("\r\nOK\r\n");
    }
    else {
        if(!strncmp(tls_tx,"AT+CWJAP=",9)) enqueue("\r\nWIFI DISCONNECT\r\n");
        if(!strcmp(tls_tx,"AT+CIPSERVER=1,8266\r\n")) ++server_queries;
        for(uint32_t i=0;i<tls_used;++i) reg_write(offset,(uint8_t)tls_tx[i]);
    }
    tls_used=0;
}
static void tls_reset(uint32_t mode) {
    head=tail=command_used=raw_left=raw_used=clock_ms=join_seen=ack_count=sysstore_verified=0;echo=1;scenario=0;
    tls_used=time_queries=connect_queries=close_queries=server_queries=wrong_cn=0;tls_case=mode;
    wifi_info.phase=2;network_init(reg_read,tls_write);ui_init(&state);ui_control(&state,UI_START);
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);memcpy((void *)(network_config.credentials+33),"unit-pass",9);
    network_config.request_sequence=1;
}
static void tls_run(void) {for(uint32_t i=0;i<400 && network_info.phase!=8 && network_info.phase!=6;++i) tick();}
static void tls_close(void) {
    network_config.action=4;network_config.reserved=0;network_config.request_sequence++;
    for(uint32_t i=0;i<20 && network_info.config_ack!=network_config.request_sequence;++i) tick();
    for(uint32_t i=0;i<80 && network_info.phase!=0 && network_info.phase!=6;++i) tick();
}
#ifndef NETWORK_TLS_TEST_NO_MAIN
#ifdef Z7_STREAM_TRANSPORT
void cache_clean_range(const void *start,unsigned bytes) {(void)start;(void)bytes;}
#endif
static void tls_receive(uint8_t byte) {(void)byte;}
int main(void) {
    check(legacy_suite()==0,"legacy LAN unchanged");
    tls_reset(0);tls_run();check(network_info.phase==8 && !network_info.error,"trusted TLS opens on ARM");
    check(time_queries==2 && connect_queries==1 && !server_queries,"wait SNTP before SSL, no LAN server");
    check(!network_owns_ui() && state.session==UI_RUNNING,"TLS bootstrap does not clear or pause UI");
    for(uint32_t i=0;i<100;++i) check(!network_config.credentials[i],"Wi-Fi private input consumed");
    uint32_t initial_closes=close_queries;
    tls_close();check(network_info.phase==0 && close_queries==initial_closes+1,"close and restore MUX0");
    tls_reset(1);tls_run();check(network_info.error==44 && !connect_queries,"bare DNS OK cannot certify hostname lookup");
    tls_reset(2);tls_run();check(network_info.error==45 && !connect_queries && time_queries<20,"unsynced clock times out before TLS");
    tls_reset(3);tls_run();check(network_info.error==44 && !connect_queries,"auth0 readback cannot open SSL");
    tls_reset(4);network_config.reserved=1;tls_run();check(network_info.error==43 && connect_queries==1,"wrong identity failure exposed");
    tls_close();check(network_info.phase==0,"failed connect permits absent-socket close then MUX0");
    tls_reset(5);tls_run();check(network_info.error==44,"bare SSL OK without CONNECT rejected");
    tls_reset(6);tls_run();tls_close();check(network_info.phase==6 && network_info.error==46,"live socket close error is not released");
    tls_reset(7);tls_run();check(network_info.phase==6 && network_info.error==44,"CONNECT then CLOSED in same drain cannot certify TLS");
    tls_reset(0);tls_run();network_config.action=1;network_config.request_sequence++;tick();
    check(network_info.phase==8 && network_info.rejected==1 && !network_info.error,"new LAN request cannot take TLS ownership");
    tls_close();check(network_info.phase==0,"rejected reconfiguration preserves valid close path");
    tls_reset(0);tls_run();enqueue("\r\n0,CLOSED\r\n");tick();check(network_info.phase==6 && network_info.error==47,"peer disconnect invalidates ready TLS");
    tls_reset(0);tls_run();check(network_stream_attach(tls_receive),"cloud adopts TLS");
    enqueue("\r\n0,CLOSED\r\n");tick();check(network_info.phase==6 && network_info.error==47,"peer disconnect invalidates active cloud stream");
    tls_reset(0);clock_ms=0xfffffff0u;tls_run();check(network_info.phase==8,"SNTP timers survive wrap");
    printf("PASS_NETWORK_TLS checks=%u\n",checks);return 0;
}
#endif
