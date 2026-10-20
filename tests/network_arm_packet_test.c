/* ARM TLS owns authentication; the ESP owns only framed TCP transactions. */
#define NETWORK_PASSIVE_TEST_NO_MAIN
#include "network_speech_passive_test.c"

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
    printf("PASS_NETWORK_ARM_PACKETS checks=%u\n",checks);return 0;
}
