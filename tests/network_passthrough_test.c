#define main legacy_suite
#include "network_runtime_test.c"
#undef main
static uint32_t wire_raw,plus_count,raw_count,last_payload,escape_ms,phy_busy,tx_space,forced_error,test_mode,server_closed;
static uint8_t raw_bytes[4096];
static char at_line[320];static uint32_t at_used;
static uint32_t raw_read(uint32_t offset) {
    if(offset==4) return (head!=tail?1u:0u)|(tx_space?2u:0u)|(phy_busy?4u:0u);
    if(offset==32) return forced_error;
    return reg_read(offset);
}
static void raw_write(uint32_t offset,uint32_t value) {
    /* network_init/retry_setup reset the UART: clear (12) and 115200 (8 = 434). */
    if(offset==12 || (offset==8 && value==434u)) return;
    check(offset==16,"raw writes TX only");
    if(wire_raw) {
        if(network_info.phase==7 && value=='+') {
            check(clock_ms-last_payload>=21,"escape has preceding guard");
            if(++plus_count==3) {wire_raw=0;escape_ms=clock_ms;}
        } else {check(raw_count<sizeof(raw_bytes),"bounded fixture");raw_bytes[raw_count++]=(uint8_t)value;last_payload=clock_ms;}
        return;
    }
    at_line[at_used++]=(char)value;
    if(value!='\n') return;
    at_line[at_used]=0;
    if(!strcmp(at_line,"ATE0\r\n")) enqueue("\r\nOK\r\n");
    else if(!strcmp(at_line,"AT+GMR\r\n")) enqueue("\r\nAT version:2.2.2\r\nSDK version:3.4\r\n\r\nOK\r\n");
    else if(!strcmp(at_line,"AT+CIPSERVER=0,1\r\n")) {server_closed=1;enqueue("\r\nOK\r\n");}
    else if(!strcmp(at_line,"AT+CIPMUX=0\r\n")) {check(server_closed,"all old server clients closed before single mode");enqueue("\r\nOK\r\n");}
    else if(!strcmp(at_line,"AT+CIFSR\r\n")) enqueue("\r\n+CIFSR:STAIP,\"192.0.2.42\"\r\n\r\nOK\r\n");
    else if(!strcmp(at_line,"AT+CIPSTART=\"TCP\",\"192.0.2.1\",8267\r\n")) enqueue("\r\nCONNECT\r\n\r\nOK\r\n");
    else if(!strcmp(at_line,"AT+CIPSEND\r\n")) {enqueue("\r\nOK\r\n");if(test_mode!=1) {enqueue("> ");wire_raw=1;}}
    else if(!strcmp(at_line,"AT+CIPMODE=0\r\n")) {
        check(plus_count==3 && clock_ms-escape_ms>=1000,"three pluses without CRLF, one second before AT");
        enqueue(test_mode==2?"\r\nERROR\r\n":"\r\nOK\r\n");
    } else if(!strcmp(at_line,"AT+CIPCLOSE\r\n")) {
        if(test_mode!=3) enqueue("\r\nCLOSED\r\n\r\nOK\r\n");
    } else if(!strcmp(at_line,"AT+SYSSTORE=0\r\n") || !strcmp(at_line,"AT+CWMODE=1\r\n") ||
              !strcmp(at_line,"AT+CIPMODE=1\r\n") || !strncmp(at_line,"AT+CWJAP=",9)) enqueue("\r\nOK\r\n");
    else {fprintf(stderr,"unexpected test command: %s",at_line);exit(1);}
    at_used=0;
}
static void raw_tick(uint32_t ms) {clock_ms+=ms;network_tick(&state,clock_ms);}
static void start_raw(uint32_t mode) {
    head=tail=clock_ms=at_used=wire_raw=plus_count=raw_count=last_payload=escape_ms=phy_busy=forced_error=server_closed=0;
    tx_space=1;test_mode=mode;scenario=0;wifi_info.phase=2;
    network_init(raw_read,raw_write);ui_init(&state);
    network_config.action=1;network_config.request_sequence=1;network_config.ssid_bytes=3;network_config.password_bytes=8;
    memcpy((void *)network_config.credentials,"LAN",3);memcpy((void *)(network_config.credentials+33),"testpass",8);
    network_config.reserved=0xc0000201;network_config.port=8267;
    for(uint32_t i=0;i<50 && network_info.phase!=4 && network_info.phase!=6;++i) raw_tick(100);
}
static void finish_raw(void) {for(uint32_t i=0;i<80 && network_info.phase==7;++i) raw_tick(100);}
int main(void) {
    check(legacy_suite()==0,"default LAN regression");
    uint8_t data[1536];for(uint32_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i*13u);
    start_raw(0);check(network_info.phase==4 && !network_info.error,"single TCP prompt enters raw mode");
    enqueue("\r\nERROR\r\n+IPD,0,10:OK > FAIL");raw_tick(1);
    check(!network_info.error && !network_info.accepted,"raw RX tokens never enter AT or LAN parser");
    tx_space=0;check(network_send(data,sizeof(data)),"raw owned block queued");raw_tick(1);
    check(raw_count==0 && !network_send(data,1),"full FIFO blocks output and requeue");
    tx_space=1;phy_busy=1;raw_tick(1);check(raw_count==sizeof(data) && !memcmp(raw_bytes,data,sizeof(data)),"raw bytes exact without CIPSEND header");
    check(!network_send(data,1),"last stop bit retains ownership");phy_busy=0;raw_tick(1);
    check(network_send(data,1),"physical completion releases buffer without SEND OK");network_stop();finish_raw();
    check(network_info.phase==0 && !network_info.error && raw_count==sizeof(data)+1,"stop drains final block then confirms normal mode and close");
    start_raw(1);raw_tick(4500);check(network_info.phase==6 && network_info.error==27,"OK without prompt times out");
    start_raw(0);forced_error=1;raw_tick(1);finish_raw();check(network_info.phase==6 && network_info.error==32,"UART fault cleaned up and retained");
    start_raw(2);network_stop();finish_raw();check(network_info.phase==6 && network_info.error==41,"mode exit error is explicit failure");
    start_raw(3);network_stop();finish_raw();check(network_info.phase==6 && network_info.error==27,"close timeout terminates cleanup");
    printf("PASS_NETWORK_PASSTHROUGH checks=%u\n",checks);return 0;
}
