#define NETWORK_PASSIVE_TEST_NO_MAIN
#include "network_speech_passive_test.c"
int main(void) {
    tls_reset(0);tls_run();tls_close();
    network_init(reg_read,passive_write);ui_init(&state);clock_ms=0;
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);
    memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();check(network_info.phase==8 && passive_selected,"passive TLS setup");
    check(network_stream_attach(speech_receive),"attach speech");
    reject_length_query=1;short_reads=1;pending_bytes=50000;
    enqueue("\r\n+IPD,0,50000\r\n");
    uint8_t audio[640];memset(audio,0x55,sizeof(audio));unsigned accepted=0;
    for(unsigned i=0;i<15000 && (delivered!=50000u || uploads!=20u || network_stream_busy());++i) {
        network_tick(&state,++clock_ms);
        if(accepted<20u && network_send(audio,sizeof(audio))) ++accepted;
    }
    check(delivered==50000u && uploads==20u && !network_info.error,"all short SSL plaintext and microphone uploads finish");
    check(!length_queries,"never ask for encrypted socket length");
    /* Empty ERRORs are bounded and cannot strand data that arrives without a
       second IPD notification. The 100 ms read poll must recover it. */
    empty_error=1;
    for(unsigned i=0;i<400;++i) network_tick(&state,++clock_ms);
    check(!network_info.error,"empty SSL socket retains live session");
    pending_bytes=2507u;
    for(unsigned i=0;i<2000 && (delivered!=52507u || network_stream_busy());++i) network_tick(&state,++clock_ms);
    check(delivered==52507u && !pending_bytes && !network_info.error && !length_queries,"poll recovers new plaintext without a notification");
    enqueue("\r\n0,CLOSED\r\n");
    for(unsigned i=0;i<100 && network_info.phase!=6;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==47,"closed socket still faults promptly");
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error,"closed stream opens fresh TLS");
    printf("PASS_NETWORK_SPEECH_NO_LENGTH checks=%u 52507 exact bytes, short reads, transient/empty errors, lost notifications and close recovery; zero encrypted-length queries\n",checks);
    return 0;
}
