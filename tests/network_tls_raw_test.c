#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
static char raw_command[320];
static unsigned raw_used,transparent,plus_count,single,auth,sni,cn,passthrough;
static unsigned sent,expected_sent,received,expected_received;
static unsigned raw_closes;
static uint8_t uplink[2048],downlink[4096];
static void raw_receive(uint8_t byte) {
    check(received<expected_received && byte==downlink[received],"raw TLS downlink byte exact");++received;
}
static void raw_write(uint32_t offset,uint32_t value) {
    if(offset!=16) {tls_write(offset,value);return;}
    if(transparent) {
        if(sent<expected_sent) {check(value==uplink[sent],"raw TLS uplink byte exact");++sent;return;}
        check(value=='+',"only guarded escape after the queued raw upload");
        if(++plus_count==3) {transparent=0;plus_count=0;}
        return;
    }
    check(raw_used+1<sizeof(raw_command),"bounded raw TLS setup command");
    raw_command[raw_used++]=(char)value;raw_command[raw_used]=0;
    if(value!='\n') return;
    if(!strcmp(raw_command,"AT+CIPMUX=0\r\n")) single=1;
    if(!strcmp(raw_command,"AT+CIPSSLCCONF=2,0,0\r\n")) auth=1;
    if(!strncmp(raw_command,"AT+CIPSSLCSNI=\"ws-",18)) sni=1;
    if(!strncmp(raw_command,"AT+CIPSSLCCN=\"ws-",17)) cn=1;
    if(!strcmp(raw_command,"AT+CIPSSLCCN=\"wrong.invalid\"\r\n")) wrong_cn=1;
    if(!strcmp(raw_command,"AT+CIPMODE=1\r\n")) {passthrough=1;enqueue("\r\nOK\r\n");}
    else if(!strcmp(raw_command,"AT+CIPSEND\r\n")) {
#ifdef Z7_ARM_TLS
        check(single && passthrough && !auth && !sni && !cn,"ESP TCP raw mode does not allocate a TLS session");
#else
        check(single && auth && sni && cn && passthrough,"raw TLS retains CA, SNI and CN verification");
#endif
        enqueue("\r\nOK\r\n> ");transparent=1;
    } else if(!strcmp(raw_command,"AT+CIPMODE=0\r\n")) enqueue("\r\nOK\r\n");
    else if(!strcmp(raw_command,"AT+CIPCLOSE\r\n")) {++close_queries;++raw_closes;enqueue("\r\nCLOSED\r\nOK\r\n");}
    else {
#ifdef Z7_ARM_TLS
        if(!strncmp(raw_command,"AT+CIPSTART=",12)) check(!strncmp(raw_command,"AT+CIPSTART=\"TCP\",",18),"ARM TLS uses TCP on the ESP");
#endif
        for(unsigned i=0;i<raw_used;++i) tls_write(offset,(uint8_t)raw_command[i]);
    }
    raw_used=0;
}
static void start_raw(unsigned mode) {
    tls_reset(mode);network_init(reg_read,raw_write);
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    if(mode==4) network_config.reserved=1;
    memcpy((void *)network_config.credentials,"TEST-NET",8);
    memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();
}
int main(void) {
    (void)tls_close;
    start_raw(0);check(network_info.phase==8 && transparent,"TLS becomes ready only after passthrough prompt");
    check(network_stream_attach(raw_receive),"cloud adopts raw TLS");
    for(unsigned i=0;i<sizeof(uplink);++i) uplink[i]=(uint8_t)i;
    const char tokens[]="\r\nCLOSED\r\nERROR\r\n+IPD,0,90:> SEND FAIL\0";
    for(unsigned i=0;i<sizeof(downlink);++i) downlink[i]=(uint8_t)tokens[i%sizeof(tokens)];
    expected_sent=sizeof(uplink);expected_received=sizeof(downlink);
    check(network_send(uplink,sizeof(uplink)),"raw TLS accepts bounded upload");
    enqueue_bytes(downlink,sizeof(downlink));
    for(unsigned i=0;i<1000 && (sent!=expected_sent || network_stream_busy());++i) network_tick(&state,++clock_ms);
    check(sent==expected_sent && received==expected_received && !network_info.error,"full duplex bypasses all AT payload parsing");
    network_stream_failed();check(network_info.phase==7 && network_info.error==48,"broken WebSocket escapes raw TLS");
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error && transparent && connect_queries==2,"close then reopen verified TLS for a fresh WebSocket");
    check(raw_closes==1,"fault recovery really closes the single TLS socket");
    network_config.action=4;network_config.request_sequence++;
    for(unsigned i=0;i<10000 && network_info.phase!=0;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==0 && !transparent && !network_info.error,"host can quiesce raw TLS before reprogramming");
    check(raw_closes==2,"host shutdown really closes the single TLS socket");
#ifdef Z7_ARM_TLS
    start_raw(1);check(network_info.phase==6 && network_info.error==44 && !transparent,"DNS failure never enters TCP raw mode");
#else
    start_raw(4);check(network_info.phase==6 && network_info.error==43 && !transparent,"wrong certificate name never enters raw TLS");
#endif
#ifdef Z7_ARM_TLS
    puts("PASS_NETWORK_ARM_TCP no ESP SSL, exact full duplex, close recovery and host quiesce");
#else
    puts("PASS_NETWORK_TLS_RAW verified single SSL, exact full duplex, close recovery and host quiesce");
#endif
    return 0;
}
