#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
static char raw_command[320];
static unsigned raw_used,transparent,plus_count,single,auth,sni,cn,passthrough;
static unsigned sent,expected_sent,received,expected_received;
static unsigned raw_closes;
static unsigned close_error,uart_clears;
static unsigned stall_tx,ignore_escape;
static uint8_t uplink[2048],downlink[4096];
#ifdef Z7_TLS_TRACE
extern volatile uint32_t network_tx_queue_trace[260],network_tx_uart_trace[260];
static void check_trace(void) {
    uint32_t hash=2166136261u;
    check(network_tx_queue_trace[2]==sent && network_tx_uart_trace[2]==sent,"queue and UART trace have identical accepted length");
    for(unsigned i=0;i<sent;++i) {
        hash=(hash^uplink[i])*16777619u;
        if(!((i+1u)&255u)) {
            unsigned at=4u+2u*(i/256u);
            check(network_tx_queue_trace[at]==i/256u+1u && network_tx_uart_trace[at]==i/256u+1u,"trace records exact block position");
            check(network_tx_queue_trace[at+1u]==hash && network_tx_uart_trace[at+1u]==hash,"copied queue and MMIO UART bytes match expected ciphertext");
            hash=2166136261u;
        }
    }
    check(network_tx_uart_trace[3]==hash && network_tx_queue_trace[3]==hash,"trace retains exact incomplete block hash");
}
#endif
static uint32_t raw_read(uint32_t offset) {
    uint32_t value=reg_read(offset);
    return offset==4 && stall_tx?value&~2u:value;
}
static void raw_receive(uint8_t byte) {
    check(received<expected_received && byte==downlink[received],"raw TLS downlink byte exact");++received;
}
static void raw_write(uint32_t offset,uint32_t value) {
    if(offset!=16) {if(offset==12 && value==15)++uart_clears;tls_write(offset,value);return;}
    if(transparent) {
        if(ignore_escape && plus_count==3u) {
            const char probe[]="AT+CIPMODE=0\r\n";
            check(raw_used<sizeof(probe)-1u && value==(uint8_t)probe[raw_used++],"unconfirmed escape permits only the single mode-verification probe");
            if(value=='\n') raw_used=0;
            return;
        }
        if(sent<expected_sent) {check(value==uplink[sent],"raw TLS uplink byte exact");++sent;return;}
        check(value=='+',"only guarded escape after the queued raw upload");
        if(++plus_count==3 && !ignore_escape) {transparent=0;plus_count=0;}
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
    else if(!strcmp(raw_command,"AT+CIPCLOSE\r\n")) {
        ++close_queries;++raw_closes;
        enqueue(close_error?"\r\nERROR\r\n":"\r\nCLOSED\r\nOK\r\n");close_error=0;
    }
    else {
#ifdef Z7_ARM_TLS
        if(!strncmp(raw_command,"AT+CIPSTART=",12)) check(!strncmp(raw_command,"AT+CIPSTART=\"TCP\",",18),"ARM TLS uses TCP on the ESP");
#endif
        for(unsigned i=0;i<raw_used;++i) tls_write(offset,(uint8_t)raw_command[i]);
    }
    raw_used=0;
}
static void start_raw(unsigned mode) {
    tls_reset(mode);network_init(raw_read,raw_write);
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
#ifdef Z7_TLS_TRACE
    network_tls_trace_begin(1u);
#endif
    for(unsigned i=0;i<sizeof(uplink);++i) uplink[i]=(uint8_t)i;
    const char tokens[]="\r\nCLOSED\r\nERROR\r\n+IPD,0,90:> SEND FAIL\0";
    for(unsigned i=0;i<sizeof(downlink);++i) downlink[i]=(uint8_t)tokens[i%sizeof(tokens)];
    expected_sent=sizeof(uplink);expected_received=sizeof(downlink);
    check(network_send(uplink,sizeof(uplink)),"raw TLS accepts bounded upload");
    enqueue_bytes(downlink,sizeof(downlink));
    for(unsigned i=0;i<1000 && (sent!=expected_sent || network_stream_busy());++i) network_tick(&state,++clock_ms);
    check(sent==expected_sent && received==expected_received && !network_info.error,"full duplex bypasses all AT payload parsing");
#ifdef Z7_TLS_TRACE
    check_trace();network_tls_trace_freeze();
    sent=0;expected_sent=sizeof(uplink);
    check(network_send(uplink,sizeof(uplink)),"trace freeze does not affect transport");
    for(unsigned i=0;i<1000 && network_stream_busy();++i) network_tick(&state,++clock_ms);
    check(sent==expected_sent && network_tx_queue_trace[2]==sent && network_tx_uart_trace[2]==sent,"first-fault trace remains frozen during later UART sends");
    network_tls_trace_begin(2u);
    check(network_tx_queue_trace[1]==2u && network_tx_uart_trace[1]==2u && !network_tx_queue_trace[2] && !network_tx_uart_trace[2],"new TLS connection resets both boundary traces");
#endif
    network_stream_failed();check(network_info.phase==7 && network_info.error==48,"broken WebSocket escapes raw TLS");
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error && transparent && connect_queries==2,"close then reopen verified TLS for a fresh WebSocket");
    check(raw_closes==1,"fault recovery really closes the single TLS socket");
    network_config.action=4;network_config.request_sequence++;
    for(unsigned i=0;i<10000;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    check(network_info.phase==0 && !transparent && !network_info.error,"host can quiesce raw TLS before reprogramming");
    check(raw_closes==2,"host shutdown really closes the single TLS socket");
    unsigned before_queries=connect_queries;
    network_cloud_run(0);
    for(unsigned i=0;i<6000;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==0 && connect_queries==before_queries,"pause stays closed without retrying");
    network_cloud_run(1);
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    check(network_info.phase==8 && connect_queries==before_queries+1 && !network_info.error,"resume opens a new cloud transport from saved Wi-Fi");
    check(network_stream_attach(raw_receive),"resumed session can adopt transport");
    network_cloud_run(0);
    for(unsigned i=0;i<10000 && network_info.phase!=0;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==0 && !transparent && !network_info.error,"user pause closes actual raw socket");
    network_cloud_run(1);
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    check(network_stream_attach(raw_receive),"second resumed session adopts transport");
    before_queries=connect_queries;
    network_cloud_run(0);network_cloud_run(1);
    for(unsigned i=0;i<20000;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    check(network_info.phase==8 && connect_queries==before_queries+1 && !network_info.error,
          "quick pause/resume finishes close before exactly one reconnect");
    network_cloud_run(0);
    for(unsigned i=0;i<10000 && network_info.phase!=0;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==0 && !transparent,"leave simulator in AT mode before a new boot");
    network_cloud_run(1);
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    close_error=1;network_cloud_run(0);
    for(unsigned i=0;i<10000 && network_info.phase!=6;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==41,"rejected close is a visible failure");
    unsigned before_clears=uart_clears;
    for(unsigned i=0;i<6000;++i) network_tick(&state,++clock_ms);
    check(uart_clears==before_clears,"pause does not restart after rejected close");
    network_cloud_run(1);network_tick(&state,++clock_ms);
    check(uart_clears==before_clears+1,"resume re-establishes serial ownership after rejected close");
#ifdef Z7_ARM_TLS
    start_raw(1);check(network_info.phase==6 && network_info.error==44 && !transparent,"DNS failure never enters TCP raw mode");
#else
    start_raw(4);check(network_info.phase==6 && network_info.error==43 && !transparent,"wrong certificate name never enters raw TLS");
#endif
    start_raw(0);check(network_stream_attach(raw_receive),"timeout trial adopts raw transport");
    sent=received=0;expected_sent=sizeof(uplink);expected_received=0;
    check(network_send(uplink,sizeof(uplink)),"timeout trial queues encrypted block");
    network_tick(&state,++clock_ms);
    check(sent>0 && sent<expected_sent,"stall begins within accepted encrypted block");
    stall_tx=1;clock_ms+=10001u;network_tick(&state,clock_ms);
    check(network_info.phase==7 && network_info.error==27,"raw send timeout starts guarded close, never an in-stream AT probe");
    stall_tx=0;
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(sent==expected_sent,"timeout preserves every byte accepted by TLS before escape");
    check(network_info.phase==8 && transparent && !network_info.error,"drained raw timeout closes before a fresh connection");
    check(network_stream_attach(raw_receive),"hard-stall trial adopts raw transport");
    sent=0;expected_sent=sizeof(uplink);
    check(network_send(uplink,sizeof(uplink)),"hard-stall trial queues encrypted block");
    network_tick(&state,++clock_ms);stall_tx=1;clock_ms+=10001u;network_tick(&state,clock_ms);
    clock_ms+=4001u;network_tick(&state,clock_ms);
    check(network_info.phase==6 && network_info.error==42,"failed raw exit is explicitly unconfirmed");
    network_config.action=4;network_config.request_sequence++;
    network_tick(&state,++clock_ms);
    stall_tx=0;before_queries=connect_queries;
    for(unsigned i=0;i<20000;++i) network_tick(&state,++clock_ms);
    check(connect_queries==before_queries && network_info.phase==6,"unconfirmed raw exit cannot inject setup commands into ciphertext");
    /* Model a module power cycle, then an escape that never reaches AT mode. */
    transparent=plus_count=raw_used=sent=expected_sent=0;start_raw(0);
    check(network_stream_attach(raw_receive),"unanswered mode probe trial adopts raw transport");
    ignore_escape=1;network_stream_failed();before_queries=connect_queries;
    for(unsigned i=0;i<20000;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==42 && connect_queries==before_queries,"no setup or automatic retry before mode-verification OK");
#ifdef Z7_ARM_TLS
    puts("PASS_NETWORK_ARM_TCP no ESP SSL, exact full duplex, close recovery and host quiesce");
#else
    puts("PASS_NETWORK_TLS_RAW verified single SSL, exact full duplex, close recovery and host quiesce");
#endif
    return 0;
}
