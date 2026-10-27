#include "network.h"
#include "wifi_identity.h"
static struct wifi_identity station;
uint32_t network_wifi_name(uint8_t out[33]) {
 for(uint32_t i=0;i<33u;++i)out[i]=station.name[i];
 return station.connected;
}
#ifdef Z7_STREAM_TRANSPORT
#include "../ps_bringup/cache.h"
#include "wifi_recover.h"
#endif
_Static_assert(sizeof(struct network_status)==64,"Network public ABI");
_Static_assert(sizeof(struct network_configuration)==128,"Network private configuration ABI");
volatile struct network_status network_info;
__attribute__((section(".network_mailbox"),used)) volatile struct network_status network_report;
__attribute__((section(".network_config"),used)) volatile struct network_configuration network_config;
#ifdef Z7_NETWORK_CAPABILITIES
_Static_assert(sizeof(struct network_capability_status)==64,"Capability public ABI");
volatile struct network_capability_status network_capabilities;
__attribute__((section(".network_capabilities"),used)) volatile struct network_capability_status network_capability_report;
static uint32_t capability_query;
static const char *const capability_commands[]={"AT+CIPSSLSIZE?\r\n","AT+CIPSSLCCONF?\r\n",
    "AT+CIPSSLCSNI?\r\n","AT+CIPSSLCCN?\r\n","AT+CIPSNTPCFG?\r\n"};
static const char *const capability_tags[]={"+CIPSSLSIZE:","+CIPSSLCCONF:",
    "+CIPSSLCSNI:","+CIPSSLCCN:","+CIPSNTPCFG:"};
#endif
static wifi_read_fn read_reg;static wifi_write_fn write_reg;
static struct ui_state *model;
static uint32_t current_time,report_ms,setup_step,enabled,ipd_left,ipd_client,line_used,frame_used,frame_expected,frame_ready;
static uint32_t command_phase,command_size,command_pos,command_deadline,reply_size,reply_line_start,ack_pending,last_request,prompt_padding,resync,send_failures;
static char command[320],reply[512],line[128],join_command[240];
enum setup_state { SETUP_ECHO,SETUP_VERSION,SETUP_STOP_SERVER,SETUP_VOLATILE,
    SETUP_STATION,SETUP_JOIN,SETUP_SIGNAL,SETUP_IP,
#ifdef Z7_STREAM_TRANSPORT
    SETUP_CLOSE_SOCKET,
#endif
    SETUP_MULTIPLEX,SETUP_IPD_SIMPLE,
#ifdef Z7_STREAM_TRANSPORT
    SETUP_AWAKE,
#endif
    SETUP_ACTIVE_RECEIVE,SETUP_SERVER,SETUP_COUNT };
static uint8_t frame[NET_FRAME_BYTES],ack[2048];
static uint32_t send_size;
static uint32_t peer,peer_port,raw_state,raw_deadline;
#define RAW_UNCONFIRMED 8u
/* A transient AT/SNTP/TLS failure must not require a re-flash: the last SSID and
   password are kept so the whole setup can be re-armed automatically. */
static uint8_t saved_ssid[32],saved_pass[64];
static uint32_t saved_ssid_bytes,saved_pass_bytes;
#ifdef Z7_NETWORK_CAPABILITIES
static uint32_t saved_retry_count,saved_retry_at;
static int cloud_suspended,cloud_resume_pending;
#endif
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
static uint32_t uart_sync_needed,uart_recovery,uart_recovery_pos,uart_recovery_deadline;
static uint32_t passive_enabled,passive_pending,passive_read,passive_header;
static uint32_t passive_retry_at;
static uint32_t passive_unread,upload_yield_started,upload_yielding;
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t receive_priority_diag[5];
static uint32_t link_recovering,link_recovery_started;
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t link_recovery_diag[7];
#define PASSIVE_READ_BYTES 2048u
#define PASSIVE_READ_COMMAND "AT+CIPRECVDATA=0,2048\r\n"
#ifdef Z7_LATENCY_PROBE
/* RX count/bytes/total/max milliseconds/peak pending; TX count/bytes/total/max.
   Public transport metadata only, not AT replies or credential storage. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t transport_perf[16];
static uint32_t perf_rx_ms,perf_rx_bytes,perf_tx_ms,perf_report_ms;
#endif
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t duplex_fault_state[25];
/* Capture sticky hardware evidence before recovery clears the UART counters. */
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t first_uart_fault[8];
#endif
static void (*stream_receive)(uint8_t);
__attribute__((section(".cloud_runtime"),used)) static volatile uint32_t link_quality[5];
#ifdef Z7_STREAM_TRANSPORT
/* Metadata only: collect locally and read once after playback, never log
   command/reply bytes or credentials. */
struct duplex_snapshot {uint32_t ms,phase,pos,command_bytes,send_bytes,reply_bytes,ipd_bytes,line_bytes,ack,resync,rx_count,tx_count,uart_status,sent_bytes,received_bytes,reply_flags;};
struct duplex_diagnostics {uint32_t magic,timeouts;struct duplex_snapshot current,first_timeout;uint32_t faults,last_error,first_error,first_error_ms;};
__attribute__((section(".cloud_runtime"),used)) static volatile struct duplex_diagnostics duplex_diag;
__attribute__((section(".cloud_runtime"),used)) static volatile char duplex_reply_shape[257];
static int contains(const char *s,const char *key);
static void duplex_snapshot(volatile struct duplex_snapshot *out) {
    out->ms=current_time;out->phase=command_phase;out->pos=command_pos;
    out->command_bytes=command_size;out->send_bytes=send_size;out->reply_bytes=reply_size;
    out->ipd_bytes=ipd_left;out->line_bytes=line_used;out->ack=ack_pending;out->resync=resync;
    out->rx_count=read_reg(24);out->tx_count=read_reg(28);
    out->uart_status=read_reg(4);out->sent_bytes=read_reg(40);out->received_bytes=read_reg(44);
    out->reply_flags=(contains(reply,">")?1u:0u)|(contains(reply,"SEND OK")?2u:0u)|
                     (contains(reply,"ERROR")?4u:0u)|(contains(reply,"FAIL")?8u:0u)|
                     (contains(reply,"Recv")?16u:0u)|(contains(reply,"busy")?32u:0u)|
                     ((reply_size && !reply[0])?64u:0u);
}
#endif
#ifdef Z7_NETWORK_CAPABILITIES
/* action 3: trusted cloud TLS, action 4: close and return UART ownership.
   reserved=1 is the explicit wrong-CN diagnostic; no API key is involved. */
#define CLOUD_HOST "ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com"
static uint32_t tls_mode,tls_step,tls_wait_until,tls_time_deadline,tls_connected,tls_closing;
#ifdef Z7_ARM_TLS
static uint32_t packet_close_pending;
#endif
static const char *const tls_commands[]={
#ifdef Z7_ARM_TLS
#define ARM_TCP_ID "0,"
    "AT+CIPDOMAIN=\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSTART=" ARM_TCP_ID "\"TCP\",\"" CLOUD_HOST "\",443\r\n",
#else
    "AT+CIPDOMAIN=\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSNTPCFG=1,0,\"ntp.aliyun.com\",\"ntp1.aliyun.com\"\r\n",
    "AT+CIPSNTPTIME?\r\n",
    "AT+CIPSSLCCONF=0,2,0,0\r\n","AT+CIPSSLCCONF?\r\n",
    "AT+CIPSSLCSNI=0,\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSSLCCN=0,\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSTART=0,\"SSL\",\"" CLOUD_HOST "\",443\r\n"
#endif
};
#endif
static void fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static void publish_report(uint32_t now) {
    if(now-report_ms<200u) return;
    report_ms=now;++network_report.sequence;fence();
#ifdef Z7_STREAM_TRANSPORT
    duplex_snapshot(&duplex_diag.current);
    cache_clean_range((const void *)&duplex_diag,sizeof(duplex_diag));
#ifdef Z7_NETWORK_CAPABILITIES
    cache_clean_range((const void *)receive_priority_diag,sizeof(receive_priority_diag));
#endif
#endif
    volatile uint32_t *out=(volatile uint32_t *)&network_report;
    const volatile uint32_t *in=(const volatile uint32_t *)&network_info;
    for(uint32_t i=0;i<16;++i) if(i!=1) out[i]=in[i];
#ifdef Z7_NETWORK_CAPABILITIES
    out=(volatile uint32_t *)&network_capability_report;in=(const volatile uint32_t *)&network_capabilities;
    for(uint32_t i=0;i<16;++i) out[i]=in[i];
#endif
    fence();++network_report.sequence;fence();
}
static uint32_t length(const char *s) {uint32_t n=0;while(s[n]) ++n;return n;}
static int same(const char *a,const char *b) {while(*a && *a==*b) {++a;++b;}return *a==*b;}
static int contains(const char *s,const char *key) {
    for(uint32_t i=0;s[i];++i) {uint32_t j=0;while(key[j] && s[i+j] && key[j]==s[i+j]) ++j;if(!key[j]) return 1;}return 0;
}
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
static int starts(const char *s,const char *prefix) {
    while(*prefix) if(*s++!=*prefix++) return 0;
    return 1;
}
#endif
static void wipe(char *p,uint32_t n) {volatile char *v=p;while(n--) *v++=0;fence();}
static void clear_reply(void) {
    reply_size=reply_line_start=0;wipe(reply,sizeof(reply));
}
static void remove_notification_reply(void) {
    if(reply_line_start<reply_size) reply_size=reply_line_start;
    /* Active IPD blocks each carry CRLF framing. Keep one delimiter so
       complete AT replies remain recognizable, without growing per packet. */
    while(reply_size>=4u && reply[reply_size-4u]=='\r' && reply[reply_size-3u]=='\n' &&
          reply[reply_size-2u]=='\r' && reply[reply_size-1u]=='\n') reply_size-=2u;
    reply[reply_size]=0;
}
static void fault(uint32_t error) {
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(error==32u && !first_uart_fault[0]) {
        first_uart_fault[0]=0x31554657u;first_uart_fault[1]=error;
        first_uart_fault[2]=current_time;first_uart_fault[3]=read_reg(32);
        first_uart_fault[4]=read_reg(36);first_uart_fault[5]=read_reg(24);
        first_uart_fault[6]=read_reg(28);first_uart_fault[7]=read_reg(8);
        cache_clean_range((const void *)first_uart_fault,sizeof(first_uart_fault));
    }
    uint32_t words[]={error,current_time,network_info.phase,setup_step,command_phase,command_pos,
        command_size,reply_size,passive_read,passive_header,ipd_left,resync};
    for(uint32_t i=0;i<12u;++i) {
        if(!duplex_fault_state[0]) duplex_fault_state[1u+i]=words[i];
        duplex_fault_state[13u+i]=words[i];
    }
    duplex_fault_state[0]=0x31544644u;
    cache_clean_range((const void *)duplex_fault_state,sizeof(duplex_fault_state));
    if(error==32u || error==34u) uart_sync_needed=0;
    /* CLOSED can arrive while ESP is still consuming a CIPSEND block. Starting
       ATE0 immediately makes that command part of the unfinished binary data. */
    if(!raw_state && error!=32u && error!=34u && tls_mode && network_info.phase==4 && ack_pending && command_phase>=2u && !passive_read) uart_sync_needed=1;
#endif
#ifdef Z7_STREAM_TRANSPORT
    ++duplex_diag.faults;duplex_diag.last_error=error;
    uint32_t first_fault=!duplex_diag.first_error;
    if(first_fault) {duplex_diag.first_error=error;duplex_diag.first_error_ms=current_time;}
    if(error==27u) {
        if(!duplex_diag.timeouts) {
            duplex_snapshot(&duplex_diag.first_timeout);
        }
        ++duplex_diag.timeouts;
    }
    if(first_fault) {
            uint32_t n=reply_size<256u?reply_size:256u;
            for(uint32_t i=0;i<n;++i) {
                unsigned char c=(unsigned char)reply[i];
                duplex_reply_shape[i]=(c>='A' && c<='Z')?'A':
                    (c>='a' && c<='z')?'a':(c>='0' && c<='9')?'0':
                    (c=='\r' || c=='\n' || (c>=32 && c<127))?(char)c:'.';
            }
            duplex_reply_shape[n]=0;
            cache_clean_range((const void *)duplex_reply_shape,sizeof(duplex_reply_shape));
    }
#endif
    if(raw_state==6 || raw_state==RAW_UNCONFIRMED) {
        raw_state=RAW_UNCONFIRMED;command_phase=ack_pending=0;
        network_info.phase=6;network_info.error=42;
        wipe(command,sizeof(command));clear_reply();return;
    }
    if(raw_state==7) {
        raw_state=command_phase=ack_pending=0;network_info.phase=6;
        if(!network_info.error) network_info.error=error;
        wipe(command,sizeof(command));clear_reply();return;
    }
    if(raw_state) {
        if(!network_info.error) network_info.error=error;
        network_stop();return;
    }
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {network_capabilities.phase=6;network_capabilities.error=error;}
#endif
    network_info.phase=6;network_info.error=error;command_phase=0;ack_pending=0;resync=0;
    wipe(command,sizeof(command));clear_reply();wipe(join_command,sizeof(join_command));
    if(enabled) (void)ui_control(model,UI_PAUSE);
}
static void begin_command(const char *text,uint32_t timeout) {
    command_size=length(text);
    if(command_size>=sizeof(command)) {fault(10);return;}
    for(uint32_t i=0;i<=command_size;++i) command[i]=text[i];
    command_pos=reply_size=reply_line_start=0;reply[0]=0;command_deadline=current_time+timeout;command_phase=1;
}
static uint32_t decimal(char *p,uint32_t v) {
    char reversed[10];uint32_t n=0;
    do {reversed[n++]=(char)('0'+v%10);v/=10;} while(v);
    for(uint32_t i=0;i<n;++i) p[i]=reversed[n-i-1];
    return n;
}
static void send_ack_command(void) {
    char text[48];uint32_t n=0;const char *prefix="AT+CIPSEND=";
    while(*prefix) text[n++]=*prefix++;
    n+=decimal(text+n,network_info.client);text[n++]=',';n+=decimal(text+n,send_size);
    text[n++]='\r';text[n++]='\n';text[n]=0;begin_command(text,30000);
}
int network_send(const uint8_t *data,uint32_t size) {
    if(network_info.phase!=4 || command_phase || ack_pending || frame_ready || line_used || !size || size>sizeof(ack)) return 0;
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    /* Prefer draining large audio bursts, but never starve microphone uploads
       for an entire spoken response. Recheck at the next idle AT boundary. */
    if(passive_enabled && passive_unread>4096u) {
        if(!upload_yielding) {upload_yielding=1;upload_yield_started=current_time;}
        uint32_t waited=current_time-upload_yield_started;
        if(waited>receive_priority_diag[4]) receive_priority_diag[4]=waited;
        if(waited<150u) {++receive_priority_diag[1];return 0;}
        ++receive_priority_diag[3];
    }
    upload_yielding=0;
#endif
    for(uint32_t i=0;i<size;++i) ack[i]=data[i];
    send_size=size;ack_pending=1;
#ifdef Z7_LATENCY_PROBE
    perf_tx_ms=current_time;
#endif
    if(raw_state==1) {command_phase=3;command_pos=0;command_deadline=current_time+10000;}
    else send_ack_command();
    return 1;
}
int network_stream_attach(void (*receive)(uint8_t)) {
    /* phase 0: pilot attaches before the TCP client connects; phase 8: the
       trusted cloud TLS link is already up and the cloud client adopts it. */
    if(!receive || (network_info.phase!=0 && network_info.phase!=8) ||
       network_config.request_sequence!=network_info.config_ack ||
       command_phase || ack_pending || (raw_state && !(raw_state==1 && network_info.phase==8)) || stream_receive) return 0;
    stream_receive=receive;network_info.phase=4;network_info.client=0;resync=0;send_failures=0;
    return 1;
}
int network_stream_busy(void) {return command_phase || ack_pending;}
void network_stream_failed(void) {
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode && (network_info.phase==4 || network_info.phase==8)) fault(48);
#endif
}
int network_stream_detach(void) {
    if(!stream_receive || network_stream_busy() || network_info.phase!=4 || ipd_left || line_used || (read_reg(4)&1u)) return 0;
    stream_receive=0;network_info.phase=0;network_info.client=0xffffffffu;
    ipd_left=line_used=prompt_padding=0;return 1;
}
void network_stop(void) {
    if(raw_state==1) {
        raw_state=2;raw_deadline=current_time+4000;network_info.phase=7;
        (void)ui_control(model,UI_PAUSE);
    }
#ifdef Z7_ARM_TLS
    /* Latch close even if the user resumes before the accepted block finishes. */
    else if(!raw_state && tls_mode && (network_info.phase==4 || network_info.phase==8)) packet_close_pending=1;
#endif
}
static void issue_setup(void) {
    static const char *const commands[]={"ATE0\r\n","AT+GMR\r\n","AT+CIPSERVER=0\r\n","AT+SYSSTORE=0\r\n",
        "AT+CWMODE=1\r\n",0,"AT+CWJAP?\r\n","AT+CIFSR\r\n",
#ifdef Z7_STREAM_TRANSPORT
        "AT+CIPCLOSE=0\r\n",
#endif
        "AT+CIPMUX=1\r\n","AT+CIPDINFO=0\r\n",
#ifdef Z7_STREAM_TRANSPORT
        "AT+SLEEP=0\r\n",
#endif
        "AT+CIPRECVMODE=0\r\n","AT+CIPSERVER=1,8266\r\n"};
    _Static_assert(sizeof(commands)/sizeof(commands[0])==SETUP_COUNT,"Setup command count");
    network_info.last_code=setup_step;
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(passive_enabled && setup_step==SETUP_ACTIVE_RECEIVE) {begin_command("AT+CIPRECVMODE=1\r\n",4000);return;}
#endif
        if(peer) {
        if(setup_step==SETUP_STOP_SERVER) {begin_command("AT+CIPSERVER=0,1\r\n",4000);return;}
        if(setup_step==SETUP_MULTIPLEX) {begin_command("AT+CIPMUX=0\r\n",4000);return;}
        if(setup_step==SETUP_IPD_SIMPLE) {
            char text[80];uint32_t n=0;const char *prefix="AT+CIPSTART=\"TCP\",\"";
            while(*prefix) text[n++]=*prefix++;
            for(uint32_t i=0;i<4;++i) {n+=decimal(text+n,(peer>>(24-8*i))&255u);if(i!=3) text[n++]='.';}
            text[n++]='"';text[n++]=',';n+=decimal(text+n,peer_port);
            text[n++]='\r';text[n++]='\n';text[n]=0;begin_command(text,15000);return;
        }
        if(setup_step==SETUP_ACTIVE_RECEIVE) {begin_command("AT+CIPMODE=1\r\n",4000);return;}
        if(setup_step==SETUP_SERVER) {begin_command("AT+CIPSEND\r\n",4000);return;}
    }
    begin_command(setup_step==SETUP_JOIN?join_command:commands[setup_step],setup_step==SETUP_JOIN?25000u:4000u);
}
#ifdef Z7_NETWORK_CAPABILITIES
static void tls_issue(void) {
    network_info.last_code=100u+tls_step;
    const char *text=tls_commands[tls_step];
    if(tls_step==6 && tls_mode==2) {
        text="AT+CIPSSLCCN=0,\"wrong.invalid\"\r\n";
    }
    begin_command(text,tls_step==7?25000u:4000u);
}
#ifndef Z7_ARM_TLS
static int tls_time_valid(void) {
    /* SNTP's initial 1970 clock cannot validate certificate dates. */
    const char *tag="+CIPSNTPTIME:";
    for(uint32_t i=0;reply[i];++i) {
        uint32_t j=0;while(tag[j] && reply[i+j]==tag[j]) ++j;
        if(tag[j]) continue;
        const char *p=reply+i+j;uint32_t n=0;while(p[n] && p[n]!='\r') ++n;
        if(n<5) return 0;
        p+=n-4;
        return p[-1]==' ' && p[0]=='2' && p[1]=='0' &&
            p[2]>='2' && p[2]<='9' && p[3]>='0' && p[3]<='9';
    }
    return 0;
}
#endif
static void tls_result(int failure) {
    if(tls_closing) {
        if(failure && (tls_closing!=1 || tls_connected)) {fault(46);return;}
        command_phase=0;clear_reply();line_used=0;
        if(tls_closing==1) {
            tls_connected=0;tls_closing=2;begin_command("AT+CIPMUX=0\r\n",4000);
#ifdef Z7_STREAM_TRANSPORT
        } else if(tls_closing==2 && passive_enabled) {
            tls_closing=3;begin_command("AT+CIPRECVMODE=0\r\n",4000);
#endif
        } else {
#ifdef Z7_STREAM_TRANSPORT
            passive_enabled=passive_pending=passive_read=passive_header=0;
#endif
            tls_closing=tls_mode=0;network_info.phase=0;network_info.error=0;
            network_info.client=0xffffffffu;
        }
        return;
    }
    if(failure) {fault(43);return;}
    if(tls_step==0 && !contains(reply,"+CIPDOMAIN:")) {fault(44);return;}
#ifndef Z7_ARM_TLS
    if(tls_step==4 && !contains(reply,"+CIPSSLCCONF:0,2,0,0\r\n")) {fault(44);return;}
    if(tls_step==2 && !tls_time_valid()) {
        if((int32_t)(current_time-tls_time_deadline)>=0) {fault(45);return;}
        command_phase=0;clear_reply();tls_wait_until=current_time+1000u;return;
    }
    if(tls_step==7 && !tls_connected) {fault(44);return;}
#else
    if(tls_step==1 && !tls_connected) {fault(44);return;}
#endif
    command_phase=0;clear_reply();
    if(++tls_step<sizeof(tls_commands)/sizeof(tls_commands[0])) {
        if(tls_step==2) tls_time_deadline=current_time+15000u;
        tls_issue();
    } else {
        if(!tls_connected) {fault(44);return;}
        network_info.phase=8;network_info.client=0;saved_retry_count=saved_retry_at=0;
    }
}
#endif
static uint32_t parse_ip(const char *s) {
    const char *tag="+CIFSR:STAIP,\"";uint32_t at=0,found=0;
    for(uint32_t i=0;s[i];++i) {
        uint32_t j=0;while(tag[j] && s[i+j] && tag[j]==s[i+j]) ++j;
        if(!tag[j]) {at=i+j;found=1;break;}
    }
    if(!found) return 0;
    uint32_t ip=0;
    for(uint32_t octet=0;octet<4;++octet) {
        uint32_t v=0,n=0;
        while(s[at]>='0' && s[at]<='9') {v=v*10+(uint32_t)(s[at++]-'0');if(++n>3 || v>255) return 0;}
        if(!n || s[at++]!=(octet==3?'"':'.')) return 0;
        ip=(ip<<8)|v;
    }return ip;
}
#ifdef Z7_NETWORK_CAPABILITIES
static const char *capability_field(const char *tag) {
    for(uint32_t i=0;reply[i];++i) if(i==0 || reply[i-1]=='\n') {
        uint32_t j=0;while(tag[j] && reply[i+j] && tag[j]==reply[i+j]) ++j;
        if(!tag[j]) return reply+i+j;
    }
    return 0;
}
static int capability_number(const char **text,uint32_t *value) {
    uint32_t n=0,v=0;const char *p=*text;
    while(*p>='0' && *p<='9') {
        if(++n>5) return 0;
        v=v*10+(uint32_t)(*p++-'0');
    }
    if(!n) return 0;
    *text=p;*value=v;return 1;
}
static void capability_result(int failure) {
    uint32_t bit=1u<<capability_query;
    network_capabilities.queried_mask|=bit;network_capabilities.response_bytes+=reply_size;
    if(failure) network_capabilities.error_mask|=bit;
    else {
        const char *p=capability_field(capability_tags[capability_query]);
        int valid=p && p[0] && p[0]!='\r' && p[0]!='\n';
        if(valid && capability_query==0) {
            uint32_t value;
            valid=capability_number(&p,&value) && value>=1024 && value<=16384 && p[0]=='\r' && p[1]=='\n';
            if(valid) network_capabilities.ssl_buffer=value;
        } else if(valid && capability_query==1) {
            uint32_t values[4],n=0;
            do {
                if(n==4 || !capability_number(&p,values+n) || values[n]>255) {valid=0;break;}
                ++n;if(*p!=',') break;++p;
            } while(1);
            valid=valid && p[0]=='\r' && p[1]=='\n';
            if(valid) {network_capabilities.auth_count=n;for(uint32_t i=0;i<n;++i) network_capabilities.auth_args[i]=values[i];}
        }
        if(valid) network_capabilities.ok_mask|=bit;else network_capabilities.invalid_mask|=bit;
    }
    command_phase=0;clear_reply();
    if(++capability_query<5) {
        network_capabilities.query_index=capability_query;begin_command(capability_commands[capability_query],4000);
    } else {network_capabilities.phase=2;network_info.phase=0;}
}
#endif
static void configuration(void) {
    uint32_t request=network_config.request_sequence;
    if(request==network_info.config_ack || wifi_info.phase!=2) return;
    fence();
    // Faults with unfinished framing require an application restart. A new
    // configuration must not reinterpret unread payload as command replies.
    if(command_phase || ipd_left || frame_used || frame_ready || line_used || ack_pending) {
        for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
        network_config.ssid_bytes=network_config.password_bytes=0;fence();
        network_info.config_ack=request;network_info.error=35;return;
    }
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode && network_config.action!=4) {
        for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
        network_config.ssid_bytes=network_config.password_bytes=0;fence();network_info.config_ack=request;
        ++network_info.rejected;network_info.last_request=request;network_info.last_code=11;return;
    }
    if(network_config.action==4) {
        int valid=network_config.magic==0x3146434eu && tls_mode &&
            (network_info.phase==8 || (network_info.phase==6 &&
             (network_info.error==43 || network_info.error==44 || network_info.error==45 || network_info.error==47))) && !stream_receive &&
            !network_config.ssid_bytes && !network_config.password_bytes && !network_config.reserved && !network_config.port;
        for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
        network_config.ssid_bytes=network_config.password_bytes=0;fence();network_info.config_ack=request;
        if(!valid || request!=network_config.request_sequence) {fault(11);return;}
        tls_closing=1;tls_wait_until=0;network_info.phase=7;
        begin_command("AT+CIPCLOSE=0\r\n",4000);return;
    }
    if(network_config.action==2) {
        int valid=network_config.magic==0x3146434eu && network_config.ssid_bytes==0 &&
            network_config.password_bytes==0 && network_config.port==0 && network_config.reserved==0 &&
            network_info.phase==0 && !enabled;
        for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
        network_config.ssid_bytes=network_config.password_bytes=0;fence();network_info.config_ack=request;
        if(!valid || request!=network_config.request_sequence) {fault(11);return;}
        volatile uint32_t *w=(volatile uint32_t *)&network_capabilities;for(uint32_t i=0;i<16;++i) w[i]=0;
        network_capabilities.magic=0x43415031u;network_capabilities.phase=1;
        network_capabilities.firmware_hash=wifi_info.firmware_hash;
        capability_query=0;network_info.phase=5;network_info.error=0;
        begin_command(capability_commands[0],4000);return;
    }
#endif
    uint32_t sn=network_config.ssid_bytes,pn=network_config.password_bytes;
    uint32_t cloud_tls=0;
#ifdef Z7_NETWORK_CAPABILITIES
    cloud_tls=network_config.action==3;
#endif
    /* Diagnostic build: publish every term of `valid` so a rejected
       configuration can be attributed without guessing. struct network_status
       has no spare word and publish_report copies exactly 16, so `framing`
       (index 13) carries the probe instead of the UART framing counter.
         bit0 magic   bit1 action   bit2 ssid len   bit3 password len
         bit4 phase==0   bit5 !enabled   bit6 reserved<=1   bit7 !port
         bit8 port rule   bit9 cloud_tls
         bits16..23 ssid_bytes   bits24..31 password_bytes */
    uint32_t t_magic=network_config.magic==0x3146434eu;
    uint32_t t_action=(network_config.action==1 || cloud_tls);
    uint32_t t_sn=(sn>=1 && sn<=32);
    uint32_t t_pn=(pn>=8 && pn<=64);
    uint32_t t_phase=(network_info.phase==0);
    uint32_t t_notenabled=!enabled;
    uint32_t t_reserved=(network_config.reserved<=1);
    uint32_t t_port0=!network_config.port;
    uint32_t t_portrule=(network_config.reserved
                         ? (network_config.port>=1 && network_config.port<=65535)
                         : (network_config.port==0 || network_config.port==8266));
    network_info.framing=t_magic|(t_action<<1)|(t_sn<<2)|(t_pn<<3)|(t_phase<<4)
                        |(t_notenabled<<5)|(t_reserved<<6)|(t_port0<<7)|(t_portrule<<8)
                        |(cloud_tls<<9)|((sn&0xffu)<<16)|((pn&0xffu)<<24);
    int valid=t_magic && t_action && t_sn && t_pn &&
        (!cloud_tls || (t_phase && t_notenabled && t_reserved && t_port0)) &&
        (cloud_tls || t_portrule);
    uint32_t n=0;const char *prefix="AT+CWJAP=\"";while(*prefix) join_command[n++]=*prefix++;
    if(valid) for(uint32_t part=0;part<2;++part) {
        uint32_t count=part?pn:sn;
        for(uint32_t i=0;i<count;++i) {
            uint8_t c=network_config.credentials[(part?33:0)+i];
            if(c<0x20 || c>0x7e) {valid=0;break;}
            if(c=='"' || c=='\\') join_command[n++]='\\';
            join_command[n++]=(char)c;
        }
        join_command[n++]='"';if(part==0) {join_command[n++]=',';join_command[n++]='"';}
    }
    join_command[n++]='\r';join_command[n++]='\n';join_command[n]=0;
    if(valid) {
        saved_ssid_bytes=sn;saved_pass_bytes=pn;
        for(uint32_t i=0;i<sn;++i) saved_ssid[i]=network_config.credentials[i];
        for(uint32_t i=0;i<pn;++i) saved_pass[i]=network_config.credentials[33+i];
    }
    for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
    network_config.ssid_bytes=network_config.password_bytes=0;fence();network_info.config_ack=request;
    if(!valid || request!=network_config.request_sequence) {fault(11);return;}
    wifi_identity_clear(&station);
    peer=network_config.reserved;peer_port=network_config.port;
#ifdef Z7_NETWORK_CAPABILITIES
    if(cloud_tls) {tls_mode=1u+network_config.reserved;tls_step=tls_wait_until=tls_connected=tls_closing=0;peer=0;}
#endif
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
#ifdef Z7_ARM_TCP_ACTIVE_RECV
    passive_enabled=0;
#else
    passive_enabled=cloud_tls;
#endif
    passive_pending=passive_read=passive_header=passive_retry_at=passive_unread=0;
#endif
    if(enabled) (void)ui_control(model,UI_PAUSE);
    setup_step=0;frame_used=frame_ready=ipd_left=line_used=last_request=ack_pending=0;
    network_info.phase=1;network_info.error=network_info.sta_ip=network_info.echo_verified=0;
    network_info.client=0xffffffffu;issue_setup();
}
static void frame_byte(uint8_t byte) {
    if(frame_ready || frame_used>=sizeof(frame)) {fault(20);return;}
    frame[frame_used++]=byte;
    if(frame_used==NET_HEADER_BYTES) {
        uint32_t n=net_word(frame+32);if(n>NET_TEXT_BYTES) {fault(21);return;}
        frame_expected=NET_HEADER_BYTES+n;
    }
    if(frame_used>=NET_HEADER_BYTES && frame_used==frame_expected) {
        frame_ready=1;
    }
}
static void commit_frame(void) {
    if(!frame_ready || ack_pending) return;
    uint32_t code=net_apply(model,frame,frame_used,&last_request);
    network_info.last_request=net_word(frame+8);network_info.last_code=code;
    if(code) ++network_info.rejected;else ++network_info.accepted;
    net_ack(ack,frame,code,model);send_size=32;ack_pending=1;frame_used=frame_ready=0;
}
static void line_event(void) {
    if(same(line,"WIFI DISCONNECT") || same(line,"ready"))wifi_identity_clear(&station);
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(passive_enabled && starts(line,"+IPD,0,")) {
        uint32_t n=7,count=0;
        while(line[n]>='0' && line[n]<='9') {count=count*10u+(uint32_t)(line[n++]-'0');if(count>65535u) {fault(24);return;}}
        if(n==7 || line[n]) {fault(24);return;}
        if(count) passive_pending=1;
        if(count>passive_unread) passive_unread=count;
        if(passive_unread>receive_priority_diag[2]) receive_priority_diag[2]=passive_unread;
        /* A prior SEND OK may have cleared reply while this notification was
           only partly received. Trimming must never grow the cleared buffer
           and expose NUL holes ahead of the next CIPSEND prompt. */
        remove_notification_reply();
        return;
    }
#endif
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode) {
        if(same(line,"0,CONNECT") || same(line,"CONNECT")) tls_connected=1;
        if(same(line,"0,CLOSED") || same(line,"CLOSED")) {
            tls_connected=0;
            if(!tls_closing && (network_info.phase==8 || network_info.phase==4)) fault(47);
        }
        if(same(line,"ready") || (same(line,"WIFI DISCONNECT") && setup_step>SETUP_JOIN)) fault(34);
        return;
    }
    if(network_capabilities.phase==1) {
        if(same(line,"ready")) fault(34);
        return; // Queries do not own a Wi-Fi link or TCP client.
    }
#endif
    if(contains(line,"WIFI DISCONNECT") && network_info.phase>=3) {fault(23);return;}
    if(same(line,"ready") && (network_info.echo_verified || stream_receive)) {fault(34);return;}
    if(line[0]>='0' && line[0]<='4' && line[1]==',') {
        uint32_t client=(uint32_t)(line[0]-'0');
        if(same(line+2,"CONNECT") && network_info.phase>=3 && network_info.phase<6 && network_info.client==0xffffffffu) {
            network_info.client=client;network_info.phase=4;frame_used=frame_ready=last_request=0;
        } else if(same(line+2,"CLOSED") && network_info.client==client) {
            network_info.client=0xffffffffu;network_info.phase=3;frame_used=frame_ready=last_request=0;
            ack_pending=0;command_phase=0;(void)ui_control(model,UI_PAUSE);
        }
    }
}
static int ipd_header(void) {
    if(line_used<5 || line[0]!='+' || line[1]!='I' || line[2]!='P' || line[3]!='D' || line[4]!=',') return 0;
    if(line_used<9) {fault(24);return 1;}
    uint32_t i=5,id=0,count=0;
    if(line[i]<'0' || line[i]>'4' || line[i+1]!=',') {fault(24);return 1;}
    id=(uint32_t)(line[i]-'0');i+=2;
    while(i<line_used && line[i]>='0' && line[i]<='9') {count=count*10+(uint32_t)(line[i++]-'0');if(count>65535) {fault(24);return 1;}}
    if(i!=line_used-1 || line[i]!=':' || !count) {fault(24);return 1;}
    ipd_left=count;ipd_client=id;line_used=0;return 1;
}
static void receive(uint8_t byte) {
    ++network_info.received_bytes;
    if(ipd_left) {
        --ipd_left;
        if(network_info.phase==4 && ipd_client==network_info.client) {
            if(stream_receive) stream_receive(byte);else frame_byte(byte);
        }
        return;
    }
    // ESP's CIPSEND prompt may include one trailing space. It is prompt
    // padding, not the first character of an immediately following IPD line.
    if(prompt_padding) {prompt_padding=0;if(byte==' ') return;}
    if(!line_used && byte!='\r' && byte!='\n') reply_line_start=reply_size;
    // TCP payload bytes (including OK, ERROR and '>') never enter AT parsing.
    if(command_phase==2 || command_phase==3 || command_phase==4) {
        if(reply_size+1>=sizeof(reply)) {fault(25);return;}
        reply[reply_size++]=(char)byte;reply[reply_size]=0;
    }
    if(byte=='\r') return;
    if(byte=='\n') {line[line_used]=0;line_event();line_used=0;return;}
    if(line_used+1>=sizeof(line)) {fault(26);return;}
    line[line_used++]=(char)byte;line[line_used]=0;
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(passive_read && byte==',' && line_used>=15u && starts(line,"+CIPRECVDATA:")) {
        uint32_t i=13,count=0;
        while(i<line_used && line[i]>='0' && line[i]<='9') {count=count*10u+(uint32_t)(line[i++]-'0');if(count>PASSIVE_READ_BYTES) {fault(24);return;}}
        if(i==13 || i!=line_used-1u) {fault(24);return;}
        ipd_left=count;ipd_client=0;passive_header=1;line_used=0;
#ifdef Z7_LATENCY_PROBE
        perf_rx_bytes=count;
#endif
        /* SSL's IPD count includes encrypted overhead. A successful empty
           plaintext read must not leave that overhead as a permanent backlog. */
        passive_unread=count && passive_unread>count?passive_unread-count:0;
        /* A short SSL plaintext block can still leave buffered data. Query
           availability before another read instead of waiting for a new IPD. */
        if(count==PASSIVE_READ_BYTES) passive_pending=1;
        else if(count && !passive_pending) passive_pending=2;
        remove_notification_reply();
        return;
    }
#endif
    if(byte==':' && ipd_header()) {
        // Remove the IPD prefix already collected in the response buffer.
        // It is notification framing, not an AT command reply.
        remove_notification_reply();
    }
    if(byte=='>' && command_phase==2 && ack_pending && network_info.phase==4) {line_used=0;prompt_padding=1;}
}
static void command_tick(void) {
    if(!command_phase) return;
    if((int32_t)(current_time-command_deadline)>=0) {
#ifdef Z7_STREAM_TRANSPORT
        if(!duplex_diag.timeouts) duplex_snapshot(&duplex_diag.first_timeout);
        ++duplex_diag.timeouts;
#endif
        /* A stalled streaming send is a transient Wi-Fi/TLS hiccup: drop the
           frame and probe the AT command mode instead of killing the whole
           session. Setup/TLS commands still fault on timeout. */
        if(raw_state) {
            /* Keep the accepted ciphertext intact while guarded close drains
               it; an AT probe is not data in a transparent TCP stream. */
            command_deadline=current_time+4000u;fault(27);return;
        }
        if(network_info.phase==4 && stream_receive) {
            ++send_failures;network_info.last_code=800u+(send_failures>99u?99u:send_failures);
            command_phase=ack_pending=0;command_size=command_pos=0;
            clear_reply();
            if(!resync) {resync=1;begin_command("AT\r\n",2000);return;}
        }
        fault(27);return;
    }
    if(command_phase==1) {
        // The bounded drain may leave old RX bytes queued. Send only after
        // that backlog is empty, so it cannot certify a new transaction.
        /* An active IPD payload is framed separately from AT replies. Waiting
           for its RX FIFO to become empty starves microphone uploads during
           continuous translated-audio downlink. The previous send has already
           completed before a new command can be queued. */
        if(!command_pos && (read_reg(4)&1u) && !(stream_receive && ipd_left)) return;
        for(uint32_t budget=0;budget<64 && command_pos<command_size && (read_reg(4)&2u);++budget)
            write_reg(16,(uint8_t)command[command_pos++]);
        if(command_pos<command_size || (read_reg(4)&4u)) return;
        command_phase=2;wipe(command,sizeof(command));reply_size=reply_line_start=0;reply[0]=0;
    } else if(command_phase==3) {
        /* With no RTS/CTS, flooding the ESP RX UART while it decrypts and
           streams PCM can lose input bytes. Pace audio-mode writes at up to
           8 bytes/ms while continuing to drain the independent receive stream. */
#ifdef Z7_STREAM_TRANSPORT
#ifdef Z7_NETWORK_CAPABILITIES
#ifdef Z7_ARM_TLS
        /* ESP carries TCP only: match the already tested passive TCP budget.
           The SSL-only eight-byte throttle otherwise backs up active uploads. */
        const uint32_t tx_budget=64u;
#else
        const uint32_t tx_budget=passive_enabled?64u:8u;
#endif
#else
        const uint32_t tx_budget=8u;
#endif
#else
        const uint32_t tx_budget=2048u;
#endif
        for(uint32_t budget=0;budget<tx_budget && command_pos<send_size && (read_reg(4)&2u);++budget) {
            uint8_t byte=ack[command_pos++];write_reg(16,byte);
        }
        if(command_pos<send_size || (read_reg(4)&4u)) return;
        if(raw_state==1 || raw_state==2) {command_phase=ack_pending=0;return;}
        command_phase=4; /* Keep replies that arrived during the last TX bits. */
    }
    if(command_phase==4) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) {
            ++send_failures;network_info.last_code=800u+(send_failures>99u?99u:send_failures);
            /* A lost WebSocket/Ogg frame cannot be silently discarded while
               retaining this stream. The completed failure owns no binary tail. */
            command_phase=ack_pending=0;
#ifdef Z7_STREAM_TRANSPORT
            if(stream_receive) {fault(28);return;}
#endif
            clear_reply();return;
        }
        if(contains(reply,"\r\nSEND OK\r\n")) {
#ifdef Z7_LATENCY_PROBE
            uint32_t ms=current_time-perf_tx_ms;++transport_perf[8];transport_perf[9]+=send_size;
            transport_perf[10]+=ms;if(ms>transport_perf[11]) transport_perf[11]=ms;
#endif
            send_failures=0;command_phase=ack_pending=0;clear_reply();return;
        }
        if(resync && contains(reply,"\r\nOK\r\n")) {resync=0;command_phase=ack_pending=0;clear_reply();return;}
        return;
    }
    if(command_phase!=2) return;
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(passive_read) {
        if(contains(reply,"\r\nERROR\r\n") || contains(reply,"\r\nFAIL\r\n")) {
            if(passive_read==2u || passive_header) {fault(43);return;}
            /* An empty socket or a temporary allocation failure can reject a
               read without closing SSL. Query availability, don't tear it down. */
            passive_read=2u;begin_command("AT+CIPRECVLEN?\r\n",4000);return;
        }
        if(contains(reply,"\r\nOK\r\n")) {
            if(passive_read==2u) {
                const char *p=reply;
                while(*p && !starts(p,"+CIPRECVLEN:")) ++p;
                if(!*p) {fault(24);return;}
                p+=12;uint32_t count=0,n=0;
                while(*p>='0' && *p<='9') {count=count*10u+(uint32_t)(*p++-'0');++n;if(count>65535u) {fault(24);return;}}
                if(!n || (*p!=',' && *p!='\r')) {fault(24);return;}
                passive_unread=count;
                if(count>receive_priority_diag[2]) receive_priority_diag[2]=count;
                if(count) passive_pending=1;
                passive_retry_at=current_time+(count?0u:20u);
            } else if(!passive_header || ipd_left) {fault(24);return;}
#ifdef Z7_LATENCY_PROBE
            if(passive_read==1u) {
                uint32_t ms=current_time-perf_rx_ms;++transport_perf[1];transport_perf[2]+=perf_rx_bytes;
                transport_perf[3]+=ms;if(ms>transport_perf[4]) transport_perf[4]=ms;
                transport_perf[7]=current_time;transport_perf[13]=perf_rx_bytes;
            }
#endif
            passive_read=command_phase=0;clear_reply();
        }
        return;
    }
#endif
    if(peer && setup_step==SETUP_SERVER && !raw_state) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) fault(29);
        else if(contains(reply,">")) {
            command_phase=0;line_used=reply_size=reply_line_start=0;reply[0]=0;raw_state=1;
            network_info.phase=4;network_info.client=0;enabled=1;
            (void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);
        }
        return;
    }
    if(network_info.phase==4 && ack_pending) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) {
            /* Rebuild the cloud session rather than lose its queued append. */
            ++send_failures;network_info.last_code=800u+(send_failures>99u?99u:send_failures);
            command_phase=ack_pending=0;
#ifdef Z7_STREAM_TRANSPORT
            if(stream_receive) {fault(28);return;}
#endif
            clear_reply();
            if(send_failures>8u) fault(28);
            else if(!resync) {resync=1;begin_command("AT\r\n",2000);}
            return;
        }
        else if(contains(reply,">")) {command_phase=3;command_pos=0;reply_size=reply_line_start=0;reply[0]=0;command_deadline=current_time+30000;}
        return; // An earlier OK is not the CIPSEND prompt.
    }
    if(network_info.phase==4 && resync) {
        /* The AT resync probe reply belongs to the probe, not to the setup/TLS
           state machine: swallowing it prevents a stray phase=3 transition. */
        if(contains(reply,"\r\nOK\r\n") || contains(reply,"\r\nERROR\r\n") || contains(reply,"\r\nFAIL\r\n")) {
            resync=0;command_phase=ack_pending=0;clear_reply();
        }
        return;
    }
    int failure=contains(reply,"\r\nERROR\r\n") || contains(reply,"\r\nFAIL\r\n");
    if(!contains(reply,"\r\nOK\r\n") && !failure) return;
    if(raw_state==6 || raw_state==7) {
        command_phase=0;clear_reply();line_used=0;
        if(failure) {if(!network_info.error) network_info.error=41;raw_state=0;network_info.phase=6;return;}
        if(raw_state==6) {raw_state=7;begin_command("AT+CIPCLOSE\r\n",4000);}
        else {
            raw_state=0;enabled=0;stream_receive=0;
#ifdef Z7_NETWORK_CAPABILITIES
            if(!network_info.error) tls_mode=tls_connected=tls_closing=0;
#endif
            network_info.client=0xffffffffu;network_info.phase=network_info.error?6:0;
        }
        return;
    }
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {capability_result(failure);return;}
    uint32_t tls_count=sizeof(tls_commands)/sizeof(tls_commands[0]);
    if(tls_mode && (tls_closing || (setup_step==SETUP_COUNT && tls_step<tls_count))) {tls_result(failure);return;}
#endif
    if(failure && setup_step!=SETUP_STOP_SERVER && setup_step!=SETUP_SIGNAL
#ifdef Z7_STREAM_TRANSPORT
       && setup_step!=SETUP_CLOSE_SOCKET
#endif
       ) {fault(29);return;} // An absent old server/socket may ERROR.
    if(setup_step==SETUP_VERSION) {
        // Bare OK is insufficient. A fresh version-bearing query must respond
        // without echoing its command, including CRCR echo variants.
        if(!contains(reply,"AT version:") || !contains(reply,"SDK version:") || contains(reply,"AT+GMR")) {fault(30);return;}
        network_info.echo_verified=1;
    }
    if(setup_step==SETUP_JOIN) wipe(join_command,sizeof(join_command));
    if(setup_step==SETUP_SIGNAL) {
        if(failure)wifi_identity_clear(&station);else (void)wifi_identity_parse(&station,reply);
        link_quality[0]=0x31514c57u;link_quality[1]=2;
        const char *p=reply,*last=0;
        while(*p) {if(*p=='"') last=p;++p;}
        if(last && last[1]==',') {
            p=last+2;uint32_t channel=0,magnitude=0,negative=0;
            while(*p>='0' && *p<='9') channel=channel*10u+(uint32_t)(*p++-'0');
            if(*p==',') {++p;if(*p=='-') {negative=1;++p;}
                while(*p>='0' && *p<='9') magnitude=magnitude*10u+(uint32_t)(*p++-'0');
                if(channel>=1u && channel<=14u && magnitude<=127u) {
                    link_quality[1]=1;link_quality[2]=channel;
                    link_quality[3]=negative?0u-magnitude:magnitude;
                }
            }
        }
        link_quality[4]=current_time;
#ifdef Z7_MMU_CACHE
        cache_clean_range((const void *)link_quality,sizeof(link_quality));
#endif
    }
    if(setup_step==SETUP_IP) {network_info.sta_ip=parse_ip(reply);if(!network_info.sta_ip) {fault(31);return;}}
    command_phase=0;clear_reply();
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode && setup_step==SETUP_ACTIVE_RECEIVE) {setup_step=SETUP_COUNT;tls_issue();return;}
#endif
    if(++setup_step<SETUP_COUNT) issue_setup();
    else {network_info.phase=3;enabled=1;(void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);}
}
static void raw_cleanup(void) {
    if(raw_state==2) {
        if(command_phase==3) command_tick();
        if(command_phase || (read_reg(4)&4u)) {
            if((int32_t)(current_time-raw_deadline)>=0) {raw_state=RAW_UNCONFIRMED;command_phase=ack_pending=0;network_info.phase=6;network_info.error=42;}
            return;
        }
        raw_state=3;raw_deadline=current_time+1100;
    } else if(raw_state==3 && (int32_t)(current_time-raw_deadline)>=0) {
        write_reg(16,'+');write_reg(16,'+');write_reg(16,'+');raw_state=4;raw_deadline=current_time+4000;
    } else if(raw_state==4) {
        if(!(read_reg(4)&4u)) {raw_state=5;raw_deadline=current_time+1000;}
        else if((int32_t)(current_time-raw_deadline)>=0) {raw_state=RAW_UNCONFIRMED;command_phase=ack_pending=0;network_info.phase=6;network_info.error=42;}
    } else if(raw_state==5 && (int32_t)(current_time-raw_deadline)>=0) {
        raw_state=6;begin_command("AT+CIPMODE=0\r\n",4000);
    }
}
void network_init(wifi_read_fn r,wifi_write_fn w) {
    read_reg=r;write_reg=w;wifi_identity_clear(&station);
#ifdef Z7_LATENCY_PROBE
    for(uint32_t i=0;i<16u;++i) transport_perf[i]=0;
    perf_rx_ms=perf_rx_bytes=perf_tx_ms=perf_report_ms=0;
#endif
#ifdef Z7_NETWORK_CAPABILITIES
    cloud_suspended=cloud_resume_pending=0;
#endif
    for(uint32_t i=0;i<5u;++i) link_quality[i]=0;
#ifdef Z7_STREAM_TRANSPORT
    volatile uint32_t *duplex_words=(volatile uint32_t *)&duplex_diag;
    for(uint32_t i=0;i<sizeof(duplex_diag)/4u;++i) duplex_words[i]=0;
    duplex_diag.magic=0x31505544u;
    wipe((char *)duplex_reply_shape,sizeof(duplex_reply_shape));
#endif
    volatile uint32_t *words=(volatile uint32_t *)&network_info;for(uint32_t i=0;i<16;++i) words[i]=0;
    words=(volatile uint32_t *)&network_report;for(uint32_t i=0;i<16;++i) words[i]=0;
    words=(volatile uint32_t *)&network_config;for(uint32_t i=0;i<32;++i) words[i]=0;
#ifdef Z7_NETWORK_CAPABILITIES
    words=(volatile uint32_t *)&network_capabilities;for(uint32_t i=0;i<16;++i) words[i]=0;
    network_capabilities.magic=0x43415031u;capability_query=0;
    tls_mode=tls_step=tls_wait_until=tls_time_deadline=tls_connected=tls_closing=0;
#ifdef Z7_ARM_TLS
    packet_close_pending=0;
#endif
#endif
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    uart_sync_needed=uart_recovery=uart_recovery_pos=uart_recovery_deadline=0;
    passive_enabled=passive_pending=passive_read=passive_header=passive_retry_at=passive_unread=0;
    upload_yield_started=upload_yielding=0;
    for(uint32_t i=0;i<5u;++i) receive_priority_diag[i]=0;
    receive_priority_diag[0]=0x31505852u;
    for(uint32_t i=0;i<25u;++i) duplex_fault_state[i]=0;
    for(uint32_t i=0;i<8u;++i) first_uart_fault[i]=0;
    for(uint32_t i=0;i<7u;++i) link_recovery_diag[i]=0;
    link_recovery_diag[0]=0x314b4e4cu;link_recovering=link_recovery_started=0;
#endif
    network_info.magic=0x3154454eu;network_info.client=0xffffffffu;network_config.magic=0x3146434eu;
    /* The ESP8266 AT firmware runs at 115200 (divider 434); force it at init
       because an earlier JTAG session may have left another divider. */
    write_reg(12,15);write_reg(8,434);
    enabled=ipd_left=line_used=frame_used=frame_ready=command_phase=ack_pending=prompt_padding=0;wipe(join_command,sizeof(join_command));
    send_size=32;peer=peer_port=raw_state=raw_deadline=0;stream_receive=0;
    report_ms=(uint32_t)-200;publish_report(0);
}
int network_owns_ui(void) {return enabled!=0;}
#ifdef Z7_NETWORK_CAPABILITIES
/* A transient AT/SNTP/TLS failure otherwise needs a re-flash: re-arm a fresh
   cloud setup from the saved credentials after a short backoff. */
static void retry_setup(void) {
    if(cloud_suspended) return;
    /* Counters are sticky. A stale UART error must not invalidate the new
       configuration between re-arming it and its next service tick. */
    write_reg(12,8);network_info.overflow=network_info.framing=0;
#ifdef Z7_STREAM_TRANSPORT
    upload_yielding=0;
    passive_pending=passive_read=passive_header=passive_retry_at=passive_unread=0;
#endif
    stream_receive=0;
    tls_mode=tls_step=tls_wait_until=tls_connected=tls_closing=0;
#ifdef Z7_ARM_TLS
    packet_close_pending=0;
#endif
    enabled=0;command_phase=ack_pending=resync=0;
    ipd_left=frame_used=frame_ready=0;line_used=0;prompt_padding=0;raw_state=0;
    network_info.phase=0;network_info.error=0;
    network_config.magic=0x3146434eu;
    network_config.action=3;
    network_config.reserved=0;
    network_config.port=0;
    network_config.ssid_bytes=saved_ssid_bytes;
    network_config.password_bytes=saved_pass_bytes;
    for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
    for(uint32_t i=0;i<saved_ssid_bytes;++i) network_config.credentials[i]=saved_ssid[i];
    for(uint32_t i=0;i<saved_pass_bytes;++i) network_config.credentials[33+i]=saved_pass[i];
    network_info.config_ack=network_config.request_sequence;
    network_config.request_sequence=network_info.config_ack+1u;
    fence();
}
void network_cloud_run(int run) {
    if(!run) {
        cloud_suspended=1;cloud_resume_pending=0;network_stop();return;
    }
    if(cloud_suspended) {
        cloud_suspended=0;cloud_resume_pending=1;
        saved_retry_count=0;saved_retry_at=current_time;
    }
    /* A quick resume waits for the guarded close. Normal background ticks
       must not reopen a transport explicitly quiesced by the host loader. */
    if(cloud_resume_pending && network_info.phase==0 && saved_ssid_bytes) {
        cloud_resume_pending=0;retry_setup();
    } else if((network_info.phase==4 || network_info.phase==8)
#ifdef Z7_ARM_TLS
              && !packet_close_pending
#endif
    ) cloud_resume_pending=0;
}
#ifdef Z7_STREAM_TRANSPORT
static void recover_uart(void) {
    /* Finish any normal-mode <=2048-byte binary block, terminate the rejected
       remainder as a line, then require a fresh AT reply before Wi-Fi setup.
       Drain incoming audio without dispatching it to the failed cloud stream. */
    for(uint32_t n=0;n<2048u && (read_reg(4)&1u);++n) {
        uint32_t value=read_reg(20);if(!(value&0x80000000u)) break;
        ++network_info.received_bytes;
        if((uart_recovery==4 || (uart_recovery==3 && uart_recovery_pos==4u)) && reply_size+1<sizeof(reply)) {
            reply[reply_size++]=(char)value;reply[reply_size]=0;
        }
    }
    if((int32_t)(current_time-uart_recovery_deadline)>=0) {
        uart_recovery=0;saved_retry_at=current_time+5000u;fault(27);return;
    }
    if(uart_recovery==1) {
        for(uint32_t n=0;n<8u && uart_recovery_pos<2050u && (read_reg(4)&2u);++n) {
            write_reg(16,uart_recovery_pos<2048u?0u:uart_recovery_pos==2048u?'\r':'\n');
            ++uart_recovery_pos;
        }
        if(uart_recovery_pos==2050u && !(read_reg(4)&4u)) {
            uart_recovery=2;uart_recovery_pos=current_time+100u;
        }
    } else if(uart_recovery==2 && (int32_t)(current_time-uart_recovery_pos)>=0) {
        uart_recovery=3;uart_recovery_pos=0;reply_size=reply_line_start=0;reply[0]=0;
    } else if(uart_recovery==3) {
        const char probe[]="AT\r\n";
        while(uart_recovery_pos<4u && (read_reg(4)&2u)) write_reg(16,(uint8_t)probe[uart_recovery_pos++]);
        if(uart_recovery_pos==4u && !(read_reg(4)&4u)) uart_recovery=4;
    } else if(uart_recovery==4 && contains(reply,"\r\nOK\r\n")) {
        uart_sync_needed=uart_recovery=0;clear_reply();retry_setup();
    }
}
#endif
static void retry_faulted(void) {
    /* AT ownership is unknown after a failed drain/escape. A guarded external
       recovery is required before any setup bytes may enter this UART. */
    if(raw_state==RAW_UNCONFIRMED) return;
    if(cloud_suspended || network_info.phase!=6 || !tls_mode || !saved_ssid_bytes || saved_retry_count>=6) return;
    uint32_t error=network_info.error;
    if(error!=26 && error!=27 && error!=28 && error!=29 && error!=32 && error!=34 && error!=43 &&
       error!=44 && error!=45 && error!=46 && error!=47 && error!=48 && error!=24 && error!=41 && error!=42) return;
    if((int32_t)(current_time-saved_retry_at)<0) return;
    ++saved_retry_count;saved_retry_at=current_time+5000u;
#ifdef Z7_STREAM_TRANSPORT
    if(error==26u || error==32u || error==34u || error==41u || ((error==28u || error==29u || error==43u) && saved_retry_count>=3u)) {
        stream_receive=0;uart_sync_needed=uart_recovery=0;
        link_recovering=1;link_recovery_started=current_time;
        ++link_recovery_diag[1];link_recovery_diag[6]=error;
        wifi_recover_begin(read_reg,write_reg,current_time);return;
    }
    if(uart_sync_needed) {
        uart_recovery=1;uart_recovery_pos=0;uart_recovery_deadline=current_time+5000u;
        stream_receive=0;reply_size=reply_line_start=0;reply[0]=0;return;
    }
#endif
    retry_setup();
}
#endif

void network_tick(struct ui_state *state,uint32_t now) {
    model=state;current_time=now;++network_info.sequence;fence();
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
    if(link_recovering) {
        int result=wifi_recover_tick(now);
        link_recovery_diag[4]=wifi_recover_phase();link_recovery_diag[5]=now-link_recovery_started;
        if(result) {
            link_recovering=0;
            if(result>0) {++link_recovery_diag[2];network_info.overflow=network_info.framing=0;retry_setup();}
            else {++link_recovery_diag[3];saved_retry_at=now+5000u;}
        }
        cache_clean_range((const void *)link_recovery_diag,sizeof(link_recovery_diag));
        goto done;
    }
#endif
    configuration();network_info.overflow=read_reg(32);network_info.framing=read_reg(36);
#ifdef Z7_NETWORK_CAPABILITIES
#ifdef Z7_STREAM_TRANSPORT
    if(uart_recovery) {recover_uart();goto done;}
#endif
    retry_faulted();
#ifdef Z7_STREAM_TRANSPORT
    if(uart_recovery) {recover_uart();goto done;}
#endif
#endif
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode && tls_wait_until && network_info.phase==1 && (int32_t)(now-tls_wait_until)>=0) {
        tls_wait_until=0;tls_issue();
    }
#endif
    /* Counters latch until a deliberate UART reset. Fault the session once;
       re-faulting phase 6 every millisecond prevents any recovery progressing. */
    if(network_info.phase!=6 && (network_info.overflow || network_info.framing) && (!raw_state || !network_info.error)) fault(32);
    if(raw_state && raw_state<=5) {
        /* Passthrough RX is data, never AT replies or the LAN frame format. */
        for(uint32_t budget=0;budget<2048 && (read_reg(4)&1u);++budget) {
            uint32_t value=read_reg(20);
            if(!(value&0x80000000u)) break;
            ++network_info.received_bytes;
            if(raw_state==1 && stream_receive) stream_receive((uint8_t)value);
        }
        if(raw_state==1) command_tick();else raw_cleanup();
        goto done;
    }
    if(network_info.phase!=6 && network_info.phase!=0) {
        if(command_phase && (int32_t)(current_time-command_deadline)>=0) fault(27);
        // Promote a fully transmitted command BEFORE draining its response.
        // RX remains buffered while FIFO/prefetch/PHY is still busy.
        if(command_phase==1 && command_pos==command_size && !(read_reg(4)&4u)) {
            command_phase=2;wipe(command,sizeof(command));reply_size=reply_line_start=0;reply[0]=0;
        } else if(command_phase==3 && command_pos==send_size && !(read_reg(4)&4u)) {
            command_phase=4; /* RX may already contain the early SEND OK. */
        }
        if(network_info.phase==6) goto done;
        if((command_phase==1 && command_pos>0) || (command_phase==3 && command_pos>0 && !stream_receive)) {
            command_tick();goto done;
        }
        for(uint32_t budget=0;budget<2048 && (read_reg(4)&1u);++budget) {
            uint32_t value=read_reg(20);if(!(value&0x80000000u)) break;receive((uint8_t)value);if(network_info.phase==6) break;
        }
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
        uint32_t was_reading=passive_read;
#endif
        command_tick();
        if(network_info.phase==4) {
            commit_frame();
            if(ack_pending && !command_phase) send_ack_command();
#if defined(Z7_STREAM_TRANSPORT) && defined(Z7_NETWORK_CAPABILITIES)
            /* Return ownership once after each read, so the microphone can
               queue a due upload before the next receive transaction. */
            if(passive_enabled && passive_pending && !passive_read && !was_reading &&
               stream_receive && !command_phase && !ack_pending && !line_used &&
               (int32_t)(current_time-passive_retry_at)>=0) {
                uint32_t query=passive_pending==2u;
                passive_pending=0;passive_read=query?2u:1u;passive_header=0;
#ifdef Z7_LATENCY_PROBE
                perf_rx_ms=current_time;perf_rx_bytes=0;
#endif
                begin_command(query?"AT+CIPRECVLEN?\r\n":PASSIVE_READ_COMMAND,4000);
            }
#endif
        }
    }
done:
#ifdef Z7_LATENCY_PROBE
    if(now-perf_report_ms>=100u) {
        perf_report_ms=now;transport_perf[0]=0x31465054u;transport_perf[6]=PASSIVE_READ_BYTES;transport_perf[12]=now;
        if(passive_unread>transport_perf[5]) transport_perf[5]=passive_unread;
        cache_clean_range((const void *)transport_perf,sizeof(transport_perf));
    }
#endif
#ifdef Z7_ARM_TLS
    /* Complete the owned binary transaction before issuing the close command. */
    if(packet_close_pending && (network_info.phase==4 || network_info.phase==8) &&
       !command_phase && !ack_pending && !ipd_left && !line_used && !(read_reg(4)&1u)) {
        packet_close_pending=0;stream_receive=0;tls_closing=1;tls_wait_until=0;network_info.phase=7;
        begin_command("AT+CIPCLOSE=0\r\n",4000);
    }
#endif
    network_info.epoch=state->epoch;fence();++network_info.sequence;fence();publish_report(now);
}
