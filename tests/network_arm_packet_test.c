/* ARM TLS owns authentication; the ESP owns only framed TCP transactions. */
#define NETWORK_PASSIVE_TEST_NO_MAIN
#include "network_speech_passive_test.c"
#ifdef Z7_PASSIVE_RECV_PROBE
extern volatile uint32_t network_receive_probe_diag[16];
#endif

static void start_packet(void) {
    tls_reset(0);network_init(reg_read,passive_write);ui_init(&state);clock_ms=0;
    passive_used=pending_bytes=delivered=read_queries=uploads=upload_left=passive_selected=0;
    reject_first_read=reject_upload=0;
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);
    memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();
    check(network_info.phase==8 && passive_selected,"packet TCP ready with passive receive");
    check(!time_queries && !server_queries && connect_queries==1,"ARM TCP skips ESP TLS and SNTP");
    check(network_stream_attach(speech_receive),"ARM adopts framed TCP");
}
int main(void) {
    (void)tls_close;
    start_packet();
    pending_bytes=6100;enqueue("\r\n+IPD,0,6100\r\n");
    uint8_t block[1024];memset(block,0x55,sizeof(block));unsigned accepted=0;
    for(unsigned i=0;i<5000 && (delivered!=6100 || uploads!=4 || network_stream_busy());++i) {
        network_tick(&state,++clock_ms);
        if(accepted<4 && network_send(block,sizeof(block))) ++accepted;
    }
    check(delivered==6100 && uploads==4 && !network_info.error,"full-duplex packet payloads remain byte exact");
#ifdef Z7_PASSIVE_RECV_PROBE
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    unsigned queries_before=length_queries;
    pending_bytes=13; /* ESP has bytes, but no +IPD notification reaches ARM. */
    network_tick(&state,clock_ms+=59000u);
    check(length_queries==queries_before && delivered==6100,"probe does not poll during normal fixture silence");
    network_tick(&state,clock_ms+=1000u);
    for(unsigned i=0;i<200;++i) network_tick(&state,++clock_ms);
    check(network_receive_probe_diag[1]==1 && network_receive_probe_diag[12]==13 &&
          network_receive_probe_diag[14]==1 && !network_receive_probe_diag[4] &&
          !network_receive_probe_diag[7] && !network_receive_probe_diag[8],
          "one queued idle query records previously unannounced ESP bytes");
    check(delivered==6113 && !pending_bytes && !network_info.error,"existing passive scheduler drains diagnosed bytes exactly");
    queries_before=length_queries;
    network_tick(&state,clock_ms+=120000u);
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(length_queries==queries_before,"diagnostic is one shot, not a retry loop");
#endif
    check(network_send(block,sizeof(block)),"pause with an accepted block");
    for(unsigned i=0;i<1000 && network_info.phase!=0;++i) {
        network_cloud_run(0);network_tick(&state,++clock_ms);
    }
    check(uploads==5 && network_info.phase==0 && !network_info.error,"pause drains accepted block then closes TCP");
    unsigned before=connect_queries;
    for(unsigned i=0;i<6000;++i) {network_cloud_run(0);network_tick(&state,++clock_ms);}
    check(connect_queries==before,"paused TCP stays closed");
    network_cloud_run(1);tls_run();
    check(network_info.phase==8 && connect_queries==before+1,"resume opens a new TCP connection");
    check(network_stream_attach(speech_receive),"adopt resumed TCP");
    check(network_send(block,sizeof(block)),"quick pause with an accepted block");
    network_cloud_run(0);network_tick(&state,++clock_ms);
    check(network_stream_busy(),"pause waits for accepted bytes");
    before=connect_queries;unsigned closes=close_queries;
    for(unsigned i=0;i<2000 && connect_queries==before;++i) {
        network_cloud_run(1);network_tick(&state,++clock_ms);
    }
    tls_run();
    check(uploads==6 && close_queries>=closes+2 && connect_queries==before+1 && network_info.phase==8,
          "quick resume finishes old block and close before opening new TCP");
#ifdef Z7_PASSIVE_RECV_PROBE
    start_packet();pending_bytes=13;
    network_tick(&state,clock_ms+=60001u);
    enqueue("\r\n+IPD,0,13\r\n");
    for(unsigned i=0;i<200;++i) network_tick(&state,++clock_ms);
    check(network_receive_probe_diag[12]==13 && network_receive_probe_diag[15]==1,
          "notification arriving during query stays visible in diagnostic result");
    start_packet();reject_length_query=1;
    network_tick(&state,clock_ms+=60001u);
    for(unsigned i=0;i<100 && !network_info.error;++i) network_tick(&state,++clock_ms);
    check(network_info.error==43 && network_receive_probe_diag[14]==2,"failed diagnostic remains a failed observation");
    reject_length_query=0;
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_stream_attach(speech_receive),"adopt TCP after diagnostic failure");
    pending_bytes=13;enqueue("\r\n+IPD,0,13\r\n");
    for(unsigned i=0;i<200;++i) network_tick(&state,++clock_ms);
    check(delivered==13 && network_receive_probe_diag[14]==2 && !network_receive_probe_diag[12],
          "new connection cannot supply the old diagnostic result");
#endif
    printf("PASS_NETWORK_ARM_PACKETS checks=%u\n",checks);return 0;
}
