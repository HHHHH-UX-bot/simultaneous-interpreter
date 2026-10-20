#define NETWORK_TLS_TEST_NO_MAIN
#include "network_tls_test.c"
void cache_clean_range(const void *start,unsigned bytes) {(void)start;(void)bytes;}
static unsigned pending_bytes,delivered,read_queries,uploads,upload_left,passive_selected;
static unsigned reject_first_read=1,empty_error,length_queries;
static unsigned reject_length_query;
static unsigned reject_upload;
static unsigned fair_stress,fair_uploads,first_fair_upload;
static unsigned short_reads;
static unsigned split_notice_on_send;
static char passive_command[320];
static unsigned passive_used;
static const char controls[]="\r\n0,CLOSED\r\nSEND OK\r\nERROR >\0";
static uint8_t sample(unsigned i) {return i<sizeof(controls)?(uint8_t)controls[i]:(uint8_t)i;}
static void speech_receive(uint8_t byte) {check(byte==sample(delivered),"passive binary byte exact");++delivered;}
static void passive_write(uint32_t offset,uint32_t value) {
    if(offset!=16) {tls_write(offset,value);return;}
    if(upload_left) {
        check(value==0x55,"outgoing audio exact");
        if(!--upload_left) {
            ++uploads;enqueue(reject_upload==1u?"\r\nSEND FAIL\r\n":"\r\nSEND OK\r\n");
            if(split_notice_on_send) {split_notice_on_send=0;enqueue("+IPD,0,");}
        }
        return;
    }
    check(value!=0,"read/query failure must not inject binary padding into AT command mode");
    check(passive_used+1<sizeof(passive_command),"bounded passive command");
    passive_command[passive_used++]=(char)value;passive_command[passive_used]=0;
    if(value!='\n') return;
    if(!strcmp(passive_command,"AT+CIPRECVMODE=1\r\n")) {passive_selected=1;enqueue("\r\nOK\r\n");}
    else if(!strcmp(passive_command,"AT+CIPRECVLEN?\r\n")) {
        char sizes[80];++length_queries;
        if(reject_length_query) {enqueue("\r\nERROR\r\n");passive_used=0;return;}
        snprintf(sizes,sizeof(sizes),"\r\n+CIPRECVLEN:%u,0,0,0,0\r\nOK\r\n",pending_bytes);enqueue(sizes);
    }
    else if(!strcmp(passive_command,"AT+CIPRECVDATA=0,2048\r\n")) {
        unsigned n=pending_bytes<2048?pending_bytes:2048;char prefix[64];++read_queries;
        if(short_reads && n>137u) n=137u;
        if(reject_first_read || (!n && empty_error)) {
            reject_first_read=0;enqueue("\r\nERROR\r\n");passive_used=0;return;
        }
        snprintf(prefix,sizeof(prefix),"\r\n+CIPRECVDATA:%u,",n);enqueue(prefix);
        for(unsigned i=0;i<n;++i) {uint8_t byte=sample(delivered+i);enqueue_bytes(&byte,1);}
        pending_bytes-=n;enqueue("\r\nOK\r\n");
    } else if(!strncmp(passive_command,"AT+CIPSEND=0,",13)) {
        if(fair_stress && pending_bytes>4096u) {
            if(!fair_uploads) first_fair_upload=clock_ms;
            ++fair_uploads;
        }
        if(reject_upload==2u) enqueue("\r\nERROR\r\n");
        else {upload_left=(unsigned)strtoul(passive_command+13,0,10);enqueue("\r\nOK\r\n> ");}
    } else for(unsigned i=0;i<passive_used;++i) tls_write(16,(uint8_t)passive_command[i]);
    passive_used=0;
}
#ifndef NETWORK_PASSIVE_TEST_NO_MAIN
int main(void) {
    tls_reset(0);tls_run();tls_close();check(network_info.phase==0,"old session closed");
    network_init(reg_read,passive_write);ui_init(&state);clock_ms=0;
    network_config.action=3;network_config.ssid_bytes=8;network_config.password_bytes=9;
    memcpy((void *)network_config.credentials,"TEST-NET",8);
    memcpy((void *)(network_config.credentials+33),"unit-pass",9);network_config.request_sequence=1;
    tls_run();check(network_info.phase==8 && passive_selected,"TLS uses passive receive");
    check(network_stream_attach(speech_receive),"attach speech");
    pending_bytes=16384u+23u;enqueue("\r\n+IPD,0,16407\r\n");
    uint8_t audio[640];memset(audio,0x55,sizeof(audio));unsigned accepted=0;
    for(unsigned i=0;i<5000 && (delivered!=16407u || uploads!=8 || network_stream_busy());++i) {
        network_tick(&state,++clock_ms);
        if(accepted<8 && !network_stream_busy() && network_send(audio,sizeof(audio))) ++accepted;
    }
    check(delivered==16407u && uploads==8 && !network_info.error,"binary download and microphone both finish");
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(read_queries>=10 && length_queries>=1 && !network_stream_busy(),"transient read rejection retains live SSL and retries buffered data");
    unsigned previous_queries=read_queries;
    unsigned previous_lengths=length_queries;
    enqueue("\r\n+IPD,0,8\r\n"); /* SSL notification can exceed plaintext bytes. */
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(read_queries==previous_queries+1 && !network_stream_busy() && !network_info.error,"zero-byte SSL read completes");
    empty_error=1;enqueue("\r\n+IPD,0,8\r\n");
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(read_queries==previous_queries+2 && length_queries==previous_lengths+1 && !network_stream_busy() && !network_info.error,"empty ERROR does not kill translation");
    check(network_send(audio,sizeof(audio)),"upload still accepted after empty ERROR");
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(uploads==9 && !network_info.error,"microphone still progresses after empty ERROR");
    /* SSL may return a short plaintext block while more buffered data remains.
       One notification must drain all of it; no second IPD will rescue us. */
    short_reads=1;empty_error=0;pending_bytes=6100;
    enqueue("\r\n+IPD,0,6100\r\n");
    for(unsigned i=0;i<10000 && (delivered!=22507u || network_stream_busy());++i) network_tick(&state,++clock_ms);
    check(delivered==22507u && !pending_bytes,"short SSL read must not strand remaining translation bytes");
    short_reads=0;
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    /* Keep the receive backlog high for longer than an upload interval.
       A stream of short SSL reads must still yield microphone opportunities. */
    fair_stress=short_reads=1;pending_bytes=30000;
    enqueue("\r\n+IPD,0,30000\r\n");
    unsigned stress_start=clock_ms,previous_uploads=uploads;
    for(unsigned i=0;i<10000 && (pending_bytes || network_stream_busy());++i) {
        network_tick(&state,++clock_ms);
        if(pending_bytes && !network_stream_busy()) (void)network_send(audio,sizeof(audio));
    }
#ifdef Z7_SSL_DRAIN_FIRST
    (void)stress_start;
    check(!fair_uploads && uploads==previous_uploads,"receive-first mode completes buffered SSL data before upload");
#else
    check(fair_uploads>0 && uploads>previous_uploads,"sustained downlink cannot starve microphone");
    check(first_fair_upload-stress_start>=150u && first_fair_upload-stress_start<220u,"bounded receive priority at idle AT boundary");
#endif
    check(delivered==52507u && !pending_bytes && !network_info.error,"fair uploads preserve byte-exact downlink");
    fair_stress=short_reads=0;
    /* Complete the availability query scheduled by the last short SSL read. */
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    for(unsigned failure=1;failure<=2;++failure) {
        reject_upload=failure;
        check(network_send(audio,sizeof(audio)),"queue failed cloud append");
        for(unsigned i=0;i<500 && network_info.phase!=6;++i) network_tick(&state,++clock_ms);
        check(network_info.phase==6 && network_info.error==28,"failed upload cannot leave false online stream");
        reject_upload=0;
        for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
        check(network_info.phase==8 && !network_info.error,"failed append reopens TLS without binary padding");
        check(network_stream_attach(speech_receive),"cloud can start fresh WebSocket/Ogg stream");
    }
    /* The UART drain can finish after SEND OK but inside the following IPD
       header. Do not start another command until that line is complete. */
    split_notice_on_send=1;
    unsigned old_uploads=uploads;
    check(network_send(audio,sizeof(audio)),"queue upload before fragmented notification");
    for(unsigned i=0;i<200 && (uploads==old_uploads || network_stream_busy());++i) network_tick(&state,++clock_ms);
    check(uploads==old_uploads+1 && !network_stream_busy(),"SEND OK completes before IPD header tail arrives");
    check(!network_send(audio,sizeof(audio)),"partial AT notification retains receive ownership");
    enqueue("8\r\n");
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(!network_info.error && !network_stream_busy(),"fragmented notification completes without growing reply into cleared bytes");
    check(network_send(audio,sizeof(audio)),"next upload accepted after complete notification");
    for(unsigned i=0;i<100;++i) network_tick(&state,++clock_ms);
    check(uploads==old_uploads+2 && !network_stream_busy() && !network_info.error,"next CIPSEND prompt remains visible after fragmented IPD");
    reject_length_query=empty_error=1;enqueue("\r\n+IPD,0,8\r\n");
    for(unsigned i=0;i<100 && network_info.phase!=6;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==6 && network_info.error==43,"failed availability query invalidates stream");
    for(unsigned i=0;i<10000 && network_info.phase!=8;++i) network_tick(&state,++clock_ms);
    check(network_info.phase==8 && !network_info.error,"query fault reopens TLS without binary padding");
    printf("PASS_NETWORK_SPEECH_PASSIVE checks=%u 52507 binary bytes, bounded receive priority, short SSL reads fully drain, rejected uploads and empty reads recover\n",checks);
    return 0;
}
#endif
