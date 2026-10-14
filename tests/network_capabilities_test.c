#define main legacy_suite
#include "network_runtime_test.c"
#undef main
static char query[64];
static uint32_t query_used,query_count,query_mode,tx_busy;
static uint32_t cap_read(uint32_t offset) {
    if(offset==4) return reg_read(offset)|(tx_busy?4u:0u);
    return reg_read(offset);
}
static void cap_write(uint32_t offset,uint32_t value) {
    check(offset==16,"capability task only writes UART TX");
    check(query_used+1<sizeof(query),"query bounded");query[query_used++]=(char)value;query[query_used]=0;
    if(value!='\n') return;
    ++query_count;
    if(query_mode==4) {query_used=0;return;}
    if(query_mode==3) enqueue("\r\nOK\r\n");
    else if(!strcmp(query,"AT+CIPSSLSIZE?\r\n"))
        enqueue(query_mode==2?"\r\n+CIPSSLSIZE:0\r\nOK\r\n":"\r\n+CIPSSLSIZE:4096\r\nOK\r\n");
    else if(!strcmp(query,"AT+CIPSSLCCONF?\r\n"))
        enqueue(query_mode==1?"\r\n+CIPSSLCCONF:0,2,0,0\r\nOK\r\n":"\r\n+CIPSSLCCONF:0\r\nOK\r\n");
    else if(!strcmp(query,"AT+CIPSSLCSNI?\r\n"))
        enqueue(query_mode==5?"\r\n+CIPSSLCSNI:\r\nOK\r\n":query_mode==1?"\r\n+CIPSSLCSNI:0,\"\"\r\nOK\r\n":"\r\nERROR\r\n");
    else if(!strcmp(query,"AT+CIPSSLCCN?\r\n"))
        enqueue(query_mode==5?"\r\n+CIPSSLCCN:\r\nOK\r\n":query_mode==1?"\r\n+CIPSSLCCN:0,\"\"\r\nOK\r\n":"\r\nERROR\r\n");
    else if(!strcmp(query,"AT+CIPSNTPCFG?\r\n")) enqueue(query_mode==5?"\r\n+CIPSNTPCFG:\r\nOK\r\n":"\r\n+CIPSNTPCFG:0,8,\"example.test\"\r\nOK\r\n");
    else {fprintf(stderr,"Non-query command issued by capability task\n");exit(1);}
    query_used=0;
}
static void cap_reset(uint32_t mode) {
    head=tail=command_used=raw_left=raw_used=clock_ms=join_seen=ack_count=0;scenario=0;echo=0;
    query_used=query_count=tx_busy=0;query_mode=mode;
    wifi_info.phase=2;wifi_info.firmware_hash=1234;network_init(cap_read,cap_write);
    ui_init(&state);check(ui_control(&state,UI_START),"capability model init");
    network_config.action=2;network_config.request_sequence=1;
    memset((void *)network_config.credentials,'x',100);
}
static void cap_run(void) {
    for(uint32_t i=0;i<80 && network_capabilities.phase!=2 && network_info.phase!=6;++i) tick();
}
static void completed(void) {
    check(network_capabilities.phase==2 && !network_capabilities.error,"all query responses completed");
    check(network_capabilities.queried_mask==31 && query_count==5,"only five fixed read queries");
    check(network_info.phase==0 && !network_owns_ui() && state.session==UI_RUNNING,"no Wi-Fi/UI ownership change");
    check(network_capabilities.firmware_hash==1234,"queries bound to accepted firmware proof");
    for(uint32_t i=0;i<100;++i) check(!network_config.credentials[i],"query action clears unused private bytes");
}
int main(void) {
    check(legacy_suite()==0,"existing network suite with capability profile");
    cap_reset(0);cap_run();completed();
    check(network_capabilities.ok_mask==19 && network_capabilities.error_mask==12 && !network_capabilities.invalid_mask,"unsupported queries recorded independently");
    check(network_capabilities.ssl_buffer==4096 && network_capabilities.auth_count==1 && !network_capabilities.auth_args[0],"numeric fields only");
    cap_reset(1);cap_run();completed();
    check(network_capabilities.ok_mask==31 && network_capabilities.auth_count==4 && network_capabilities.auth_args[1]==2,"four observed auth arguments");
    cap_reset(2);cap_run();completed();check(network_capabilities.invalid_mask==1 && !network_capabilities.ssl_buffer,"bad SSL size cannot certify buffer");
    cap_reset(3);cap_run();completed();check(network_capabilities.invalid_mask==31 && !network_capabilities.ok_mask,"bare OK cannot certify capability data");
    cap_reset(5);cap_run();completed();check(network_capabilities.invalid_mask==28 && network_capabilities.ok_mask==3,"empty tagged replies are not returned data");
    cap_reset(4);cap_run();check(network_capabilities.phase==6 && network_capabilities.error==27,"missing reply is timeout, not unsupported");
    cap_reset(0);scenario=6;cap_run();check(network_capabilities.phase==6 && network_capabilities.error==32,"serial fault fails query task");
    cap_reset(0);wifi_info.phase=1;tick();check(!query_count && !network_info.config_ack,"AT acceptance precedes query action");
    wifi_info.phase=2;cap_run();completed();
    cap_reset(0);enqueue("\r\n+CIPSSLSIZE:8192\r\nOK\r\n");cap_run();completed();
    check(network_capabilities.ssl_buffer==4096,"pre-send reply cannot forge query result");
    cap_reset(0);for(unsigned i=0;i<2048;++i) enqueue("\n");enqueue("\r\n+CIPSSLSIZE:8192\r\nOK\r\n");
    tick();check(!query_count && !network_capabilities.queried_mask,"RX backlog delays first query");
    cap_run();completed();check(network_capabilities.ssl_buffer==4096,"post-budget stale reply cannot replace fresh result");
    cap_reset(0);tx_busy=1;for(unsigned i=0;i<4;++i) tick();
    check(!network_capabilities.queried_mask,"reply waits until final TX stop bit");tx_busy=0;cap_run();completed();
    cap_reset(0);tick();enqueue("\r\n0,CONNECT\r\nWIFI DISCONNECT\r\n");cap_run();completed();
    check(network_info.client==0xffffffffu,"query task does not adopt unrelated socket notifications");
    cap_reset(0);clock_ms=0xfffffff0u;cap_run();completed();
    reset(0);ready();network_config.action=2;network_config.ssid_bytes=network_config.password_bytes=0;
    network_config.request_sequence=2;tick();check(network_info.error==11 && !network_capabilities.queried_mask,"active LAN cannot be repurposed for queries");
    printf("PASS_Z7_NETWORK_CAPABILITIES checks=%u\n",checks);return 0;
}
