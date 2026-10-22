#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
static uint32_t sent_bytes,received_bytes,send_left,cmd_used,receive_modes,defer_send_ok;
static char active_command[320];
static void active_receive(uint8_t b) {check(b==(uint8_t)received_bytes,"active TCP RX byte exact");++received_bytes;}
static void active_write(uint32_t offset,uint32_t value) {
    if(offset!=16) {tls_write(offset,value);return;}
    if(send_left) {
        check(value==(uint8_t)sent_bytes,"active TCP TX byte exact");++sent_bytes;
        if(!--send_left && !defer_send_ok) enqueue("\r\nSEND OK\r\n");
        return;
    }
    check(cmd_used+1<sizeof(active_command),"active command bound");
    active_command[cmd_used++]=(char)value;active_command[cmd_used]=0;
    if(value!='\n') return;
    check(!strstr(active_command,"CIPRECVDATA") && !strstr(active_command,"CIPRECVLEN"),"active TCP needs no passive read/query");
    if(!strcmp(active_command,"AT+CIPRECVMODE=0\r\n")) ++receive_modes;
    check(strcmp(active_command,"AT+CIPRECVMODE=1\r\n"),"ARM TCP explicitly uses active receive");
    if(!strncmp(active_command,"AT+CIPSEND=0,",13)) {
        send_left=(uint32_t)strtoul(active_command+13,0,10);enqueue("\r\nOK\r\n> ");
    } else for(uint32_t i=0;i<cmd_used;++i) tls_write(16,(uint8_t)active_command[i]);
    cmd_used=0;
}
int main(void) {
    tls_reset(0);tls_run();tls_close();network_init(reg_read,active_write);
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();check(network_info.phase==8 && receive_modes==1,"active TCP handshake ready");
    check(network_stream_attach(active_receive),"attach ARM TLS boundary");
    for(unsigned round=0;round<32;++round) {
        enqueue("\r\n+IPD,0,4096:");
        for(unsigned i=0;i<4096;++i) {uint8_t b=(uint8_t)(round*4096+i);enqueue_bytes(&b,1);}
        uint8_t audio[640];for(unsigned i=0;i<sizeof(audio);++i)audio[i]=(uint8_t)(round*640+i);
        check(network_send(audio,sizeof(audio)),"queue microphone while downlink pending");
        for(unsigned i=0;i<300 && (network_stream_busy() || received_bytes<(round+1u)*4096u);++i)tick();
        check(!network_stream_busy() && !network_info.error && sent_bytes==(round+1u)*640u && received_bytes==(round+1u)*4096u,"duplex block complete");
    }
    /* Hundreds of small IPD blocks can precede one SEND OK. Their blank
       separators must not accumulate in the bounded AT reply buffer. */
    defer_send_ok=1;
    uint8_t byte=(uint8_t)sent_bytes;
    check(network_send(&byte,1),"queue upload during fragmented downlink");
    for(unsigned i=0;i<20 && sent_bytes==20480u;++i)tick();
    check(sent_bytes==20481u,"upload awaits deferred SEND OK");
    for(unsigned i=0;i<600;++i) {
        enqueue("\r\n+IPD,0,");network_tick(&state,++clock_ms);
        enqueue("1:");byte=(uint8_t)received_bytes;enqueue_bytes(&byte,1);network_tick(&state,++clock_ms);
        check(!network_info.error,"fragmented IPD separators stay bounded");
    }
    enqueue("\r\nSEND OK\r\n");tick();
    check(!network_stream_busy() && received_bytes==131672u,"fragmented duplex completes without loss");
    enqueue("\r\n0,CLOSED\r\n");tick();check(network_info.phase==6,"closed active socket is visible");
    printf("PASS_ARM_ACTIVE_TCP checks=%u download=%u upload=%u no passive AT round trips\n",checks,received_bytes,sent_bytes);return 0;
}
