#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
void cache_clean_range(const void *start,unsigned bytes) {(void)start;(void)bytes;}
static unsigned binary_left,binary_sent,closed_once,probe_seen,padding_seen;
static char recovery_line[320];
static unsigned recovery_used;
static void recovery_write(uint32_t offset,uint32_t value) {
    if(offset!=16) {tls_write(offset,value);return;}
    if(binary_left) {
        --binary_left;++binary_sent;
        if(value==0) ++padding_seen;
        if(binary_sent==32 && !closed_once) {closed_once=1;enqueue("\r\n0,CLOSED\r\n");}
        return;
    }
    /* ESP command parser ignores NUL; CRLF terminates leftover bad data. */
    if(!value) {++padding_seen;return;}
    check(recovery_used+1<sizeof(recovery_line),"bounded recovery command");
    recovery_line[recovery_used++]=(char)value;recovery_line[recovery_used]=0;
    if(value!='\n') return;
    if(!strncmp(recovery_line,"AT+CIPSEND=0,",13)) {
        binary_left=(unsigned)strtoul(recovery_line+13,0,10);enqueue("\r\nOK\r\n> ");
    } else if(!strcmp(recovery_line,"AT\r\n")) {++probe_seen;enqueue("\r\nOK\r\n");}
    else if(strcmp(recovery_line,"\r\n")) {
        for(unsigned i=0;i<recovery_used;++i) tls_write(16,(uint8_t)recovery_line[i]);
    }
    recovery_used=0;
}
static void discard_audio(uint8_t byte) {(void)byte;}
int main(void) {
    tls_reset(0);tls_run();check(network_info.phase==8,"initial trusted TLS");
    tls_close();check(network_info.phase==0,"initial session released");
    /* Configure using our UART so every retry follows the actual transport. */
    network_init(reg_read,recovery_write);ui_init(&state);clock_ms=0;
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);
    memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();check(network_info.phase==8,"recovery mock trusted TLS");
    check(network_stream_attach(discard_audio),"audio adopts TLS");
    uint8_t block[926];memset(block,0x55,sizeof(block));
    check(network_send(block,sizeof(block)),"queue upload");
    for(unsigned i=0;i<10000 && network_info.phase!=6;++i) network_tick(&state,++clock_ms);
    check(closed_once && network_info.phase==6 && binary_left>0,"CLOSED leaves ESP in binary receive");
    for(unsigned i=0;i<20000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(probe_seen==1 && padding_seen>=2048,"bounded binary drain and fresh AT confirmation");
    check(!binary_left && network_info.phase==8 && !network_info.error,"reconnect completes without reboot");
    printf("PASS_NETWORK_SPEECH_RECOVERY checks=%u interrupted binary CIPSEND resynchronizes before TLS retry\n",checks);
    return 0;
}
