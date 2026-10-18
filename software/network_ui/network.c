#include "network.h"
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
    SETUP_STATION,SETUP_JOIN,SETUP_IP,SETUP_MULTIPLEX,SETUP_IPD_SIMPLE,
    SETUP_ACTIVE_RECEIVE,SETUP_SERVER,SETUP_COUNT };
static uint8_t frame[NET_FRAME_BYTES],ack[2048];
static uint32_t send_size;
static uint32_t peer,peer_port,raw_state,raw_deadline;
static void (*stream_receive)(uint8_t);
#ifdef Z7_NETWORK_CAPABILITIES
/* action 3: trusted cloud TLS, action 4: close and return UART ownership.
   reserved=1 is the explicit wrong-CN diagnostic; no API key is involved. */
#define CLOUD_HOST "ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com"
static uint32_t tls_mode,tls_step,tls_wait_until,tls_time_deadline,tls_connected,tls_closing;
static const char *const tls_commands[]={
    "AT+CIPDOMAIN=\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSNTPCFG=1,0,\"ntp.aliyun.com\",\"ntp1.aliyun.com\"\r\n",
    "AT+CIPSNTPTIME?\r\n","AT+CIPSSLCCONF=0,2,0,0\r\n","AT+CIPSSLCCONF?\r\n",
    "AT+CIPSSLCSNI=0,\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSSLCCN=0,\"" CLOUD_HOST "\"\r\n",
    "AT+CIPSTART=0,\"SSL\",\"" CLOUD_HOST "\",443\r\n"};
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
static void wipe(char *p,uint32_t n) {volatile char *v=p;while(n--) *v++=0;fence();}
static void fault(uint32_t error) {
    if(raw_state>=6) {
        raw_state=command_phase=ack_pending=0;network_info.phase=6;
        if(!network_info.error) network_info.error=error;
        wipe(command,sizeof(command));wipe(reply,sizeof(reply));return;
    }
    if(raw_state) {
        if(!network_info.error) network_info.error=error;
        network_stop();return;
    }
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {network_capabilities.phase=6;network_capabilities.error=error;}
#endif
    network_info.phase=6;network_info.error=error;command_phase=0;ack_pending=0;resync=0;
    wipe(command,sizeof(command));wipe(reply,sizeof(reply));wipe(join_command,sizeof(join_command));
    if(enabled) (void)ui_control(model,UI_PAUSE);
}
static void begin_command(const char *text,uint32_t timeout) {
    command_size=length(text);
    if(command_size>=sizeof(command)) {fault(10);return;}
    for(uint32_t i=0;i<=command_size;++i) command[i]=text[i];
    command_pos=reply_size=0;reply[0]=0;command_deadline=current_time+timeout;command_phase=1;
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
    if(network_info.phase!=4 || command_phase || ack_pending || frame_ready || !size || size>sizeof(ack)) return 0;
    for(uint32_t i=0;i<size;++i) ack[i]=data[i];
    send_size=size;ack_pending=1;
    if(raw_state==1) {command_phase=3;command_pos=0;command_deadline=current_time+10000;}
    else send_ack_command();
    return 1;
}
int network_stream_attach(void (*receive)(uint8_t)) {
    /* phase 0: pilot attaches before the TCP client connects; phase 8: the
       trusted cloud TLS link is already up and the cloud client adopts it. */
    if(!receive || (network_info.phase!=0 && network_info.phase!=8) ||
       network_config.request_sequence!=network_info.config_ack ||
       command_phase || ack_pending || raw_state || stream_receive) return 0;
    stream_receive=receive;network_info.phase=4;network_info.client=0;resync=0;send_failures=0;
    return 1;
}
int network_stream_busy(void) {return command_phase || ack_pending;}
int network_stream_detach(void) {
    if(!stream_receive || network_stream_busy() || network_info.phase!=4 || ipd_left || line_used || (read_reg(4)&1u)) return 0;
    stream_receive=0;network_info.phase=0;network_info.client=0xffffffffu;
    ipd_left=line_used=prompt_padding=0;return 1;
}
void network_stop(void) {
    if(raw_state!=1) return;
    raw_state=2;raw_deadline=current_time+4000;network_info.phase=7;
    (void)ui_control(model,UI_PAUSE);
}
static void issue_setup(void) {
    static const char *const commands[]={"ATE0\r\n","AT+GMR\r\n","AT+CIPSERVER=0\r\n","AT+SYSSTORE=0\r\n",
        "AT+CWMODE=1\r\n",0,"AT+CIFSR\r\n","AT+CIPMUX=1\r\n","AT+CIPDINFO=0\r\n","AT+CIPRECVMODE=0\r\n","AT+CIPSERVER=1,8266\r\n"};
    _Static_assert(sizeof(commands)/sizeof(commands[0])==SETUP_COUNT,"Setup command count");
    network_info.last_code=setup_step;
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
    if(tls_step==6 && tls_mode==2) text="AT+CIPSSLCCN=0,\"wrong.invalid\"\r\n";
    begin_command(text,tls_step==7?25000u:4000u);
}
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
static void tls_result(int failure) {
    if(tls_closing) {
        if(failure && (tls_closing!=1 || tls_connected)) {fault(46);return;}
        command_phase=0;wipe(reply,sizeof(reply));line_used=0;
        if(tls_closing==1) {
            tls_connected=0;tls_closing=2;begin_command("AT+CIPMUX=0\r\n",4000);
        } else {
            tls_closing=tls_mode=0;network_info.phase=0;network_info.error=0;
            network_info.client=0xffffffffu;
        }
        return;
    }
    if(failure) {fault(43);return;}
    if(tls_step==0 && !contains(reply,"+CIPDOMAIN:")) {fault(44);return;}
    if(tls_step==4 && !contains(reply,"+CIPSSLCCONF:0,2,0,0\r\n")) {fault(44);return;}
    if(tls_step==2 && !tls_time_valid()) {
        if((int32_t)(current_time-tls_time_deadline)>=0) {fault(45);return;}
        command_phase=0;wipe(reply,sizeof(reply));tls_wait_until=current_time+1000u;return;
    }
    if(tls_step==7 && !tls_connected) {fault(44);return;}
    command_phase=0;wipe(reply,sizeof(reply));
    if(++tls_step<sizeof(tls_commands)/sizeof(tls_commands[0])) {
        if(tls_step==2) tls_time_deadline=current_time+15000u;
        tls_issue();
    } else {network_info.phase=8;network_info.client=0;}
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
    command_phase=0;wipe(reply,sizeof(reply));
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
    int valid=network_config.magic==0x3146434eu && (network_config.action==1 || cloud_tls) && sn>=1 && sn<=32 && pn>=8 && pn<=64 &&
        (!cloud_tls || (network_info.phase==0 && !enabled && network_config.reserved<=1 && !network_config.port)) &&
        (cloud_tls ||
        (network_config.reserved ? network_config.port>=1 && network_config.port<=65535 :
         (network_config.port==0 || network_config.port==8266)));
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
    for(uint32_t i=0;i<100;++i) network_config.credentials[i]=0;
    network_config.ssid_bytes=network_config.password_bytes=0;fence();network_info.config_ack=request;
    if(!valid || request!=network_config.request_sequence) {fault(11);return;}
    peer=network_config.reserved;peer_port=network_config.port;
#ifdef Z7_NETWORK_CAPABILITIES
    if(cloud_tls) {tls_mode=1u+network_config.reserved;tls_step=tls_wait_until=tls_connected=tls_closing=0;peer=0;}
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
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode) {
        if(same(line,"0,CONNECT")) tls_connected=1;
        if(same(line,"0,CLOSED")) {
            tls_connected=0;
            if(!tls_closing && network_info.phase==8) fault(47);
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
    if(command_phase==2 || command_phase==4) {
        if(reply_size+1>=sizeof(reply)) {fault(25);return;}
        reply[reply_size++]=(char)byte;reply[reply_size]=0;
    }
    if(byte=='\r') return;
    if(byte=='\n') {line[line_used]=0;line_event();line_used=0;return;}
    if(line_used+1>=sizeof(line)) {fault(26);return;}
    line[line_used++]=(char)byte;line[line_used]=0;
    if(byte==':' && ipd_header()) {
        // Remove the IPD prefix already collected in the response buffer.
        // It is notification framing, not an AT command reply.
        if(reply_line_start<=reply_size) {reply_size=reply_line_start;reply[reply_size]=0;}
    }
    if(byte=='>' && command_phase==2 && ack_pending && network_info.phase==4) {line_used=0;prompt_padding=1;}
}
static void command_tick(void) {
    if(!command_phase) return;
    if((int32_t)(current_time-command_deadline)>=0) {
        /* A stalled streaming send is a transient Wi-Fi/TLS hiccup: drop the
           frame and probe the AT command mode instead of killing the whole
           session. Setup/TLS commands still fault on timeout. */
        if(network_info.phase==4 && stream_receive) {
            ++send_failures;network_info.last_code=800u+(send_failures>99u?99u:send_failures);
            command_phase=ack_pending=0;command_size=command_pos=0;
            wipe(reply,sizeof(reply));reply_size=0;
            if(!resync) {resync=1;begin_command("AT\r\n",2000);return;}
        }
        fault(27);return;
    }
    if(command_phase==1) {
        // The bounded drain may leave old RX bytes queued. Send only after
        // that backlog is empty, so it cannot certify a new transaction.
        if(!command_pos && (read_reg(4)&1u)) return;
        for(uint32_t budget=0;budget<64 && command_pos<command_size && (read_reg(4)&2u);++budget)
            write_reg(16,(uint8_t)command[command_pos++]);
        if(command_pos<command_size || (read_reg(4)&4u)) return;
        command_phase=2;wipe(command,sizeof(command));reply_size=0;reply[0]=0;
    } else if(command_phase==3) {
        for(uint32_t budget=0;budget<2048 && command_pos<send_size && (read_reg(4)&2u);++budget) write_reg(16,ack[command_pos++]);
        if(command_pos<send_size || (read_reg(4)&4u)) return;
        if(raw_state==1 || raw_state==2) {command_phase=ack_pending=0;return;}
        command_phase=4;reply_size=0;reply[0]=0;
    }
    if(command_phase==4) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) {
            ++send_failures;network_info.last_code=800u+(send_failures>99u?99u:send_failures);command_phase=ack_pending=0;wipe(reply,sizeof(reply));reply_size=0;return;
        }
        if(contains(reply,"\r\nSEND OK\r\n")) {command_phase=ack_pending=0;wipe(reply,sizeof(reply));return;}
        if(resync && contains(reply,"\r\nOK\r\n")) {resync=0;command_phase=ack_pending=0;wipe(reply,sizeof(reply));return;}
        return;
    }
    if(command_phase!=2) return;
    if(peer && setup_step==SETUP_SERVER && !raw_state) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) fault(29);
        else if(contains(reply,">")) {
            command_phase=0;line_used=reply_size=0;reply[0]=0;raw_state=1;
            network_info.phase=4;network_info.client=0;enabled=1;
            (void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);
        }
        return;
    }
    if(network_info.phase==4 && ack_pending) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) fault(28);
        else if(contains(reply,">")) {command_phase=3;command_pos=0;reply_size=0;reply[0]=0;command_deadline=current_time+30000;}
        return; // An earlier OK is not the CIPSEND prompt.
    }
    int failure=contains(reply,"\r\nERROR\r\n") || contains(reply,"\r\nFAIL\r\n");
    if(!contains(reply,"\r\nOK\r\n") && !failure) return;
    if(raw_state>=6) {
        command_phase=0;wipe(reply,sizeof(reply));line_used=0;
        if(failure) {if(!network_info.error) network_info.error=41;raw_state=0;network_info.phase=6;return;}
        if(raw_state==6) {raw_state=7;begin_command("AT+CIPCLOSE\r\n",4000);}
        else {raw_state=0;enabled=0;network_info.client=0xffffffffu;network_info.phase=network_info.error?6:0;}
        return;
    }
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {capability_result(failure);return;}
    if(tls_mode && (tls_closing || setup_step==SETUP_COUNT)) {tls_result(failure);return;}
#endif
    if(failure && setup_step!=SETUP_STOP_SERVER) {fault(29);return;} // deleting an absent old server may ERROR.
    if(setup_step==SETUP_VERSION) {
        // Bare OK is insufficient. A fresh version-bearing query must respond
        // without echoing its command, including CRCR echo variants.
        if(!contains(reply,"AT version:") || !contains(reply,"SDK version:") || contains(reply,"AT+GMR")) {fault(30);return;}
        network_info.echo_verified=1;
    }
    if(setup_step==SETUP_JOIN) wipe(join_command,sizeof(join_command));
    if(setup_step==SETUP_IP) {network_info.sta_ip=parse_ip(reply);if(!network_info.sta_ip) {fault(31);return;}}
    command_phase=0;wipe(reply,sizeof(reply));
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
            if((int32_t)(current_time-raw_deadline)>=0) {raw_state=command_phase=ack_pending=0;network_info.phase=6;if(!network_info.error) network_info.error=42;}
            return;
        }
        raw_state=3;raw_deadline=current_time+21;
    } else if(raw_state==3 && (int32_t)(current_time-raw_deadline)>=0) {
        write_reg(16,'+');write_reg(16,'+');write_reg(16,'+');raw_state=4;raw_deadline=current_time+4000;
    } else if(raw_state==4) {
        if(!(read_reg(4)&4u)) {raw_state=5;raw_deadline=current_time+1000;}
        else if((int32_t)(current_time-raw_deadline)>=0) {raw_state=0;network_info.phase=6;if(!network_info.error) network_info.error=42;}
    } else if(raw_state==5 && (int32_t)(current_time-raw_deadline)>=0) {
        raw_state=6;begin_command("AT+CIPMODE=0\r\n",4000);
    }
}
void network_init(wifi_read_fn r,wifi_write_fn w) {
    read_reg=r;write_reg=w;
    volatile uint32_t *words=(volatile uint32_t *)&network_info;for(uint32_t i=0;i<16;++i) words[i]=0;
    words=(volatile uint32_t *)&network_report;for(uint32_t i=0;i<16;++i) words[i]=0;
    words=(volatile uint32_t *)&network_config;for(uint32_t i=0;i<32;++i) words[i]=0;
#ifdef Z7_NETWORK_CAPABILITIES
    words=(volatile uint32_t *)&network_capabilities;for(uint32_t i=0;i<16;++i) words[i]=0;
    network_capabilities.magic=0x43415031u;capability_query=0;
    tls_mode=tls_step=tls_wait_until=tls_time_deadline=tls_connected=tls_closing=0;
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
void network_tick(struct ui_state *state,uint32_t now) {
    model=state;current_time=now;++network_info.sequence;fence();
    configuration();network_info.overflow=read_reg(32);network_info.framing=read_reg(36);
#ifdef Z7_NETWORK_CAPABILITIES
    if(tls_mode && tls_wait_until && network_info.phase==1 && (int32_t)(now-tls_wait_until)>=0) {
        tls_wait_until=0;tls_issue();
    }
#endif
    if((network_info.overflow || network_info.framing) && (!raw_state || !network_info.error)) fault(32);
    if(raw_state && raw_state<=5) {
        /* Passthrough RX is data, never AT replies or the LAN frame format. */
        for(uint32_t budget=0;budget<2048 && (read_reg(4)&1u);++budget) {
            if(!(read_reg(20)&0x80000000u)) break;
            ++network_info.received_bytes;
        }
        if(raw_state==1) command_tick();else raw_cleanup();
        goto done;
    }
    if(network_info.phase!=6 && network_info.phase!=0) {
        if(command_phase && (int32_t)(current_time-command_deadline)>=0) fault(27);
        // Promote a fully transmitted command BEFORE draining its response.
        // RX remains buffered while FIFO/prefetch/PHY is still busy.
        if(command_phase==1 && command_pos==command_size && !(read_reg(4)&4u)) {
            command_phase=2;wipe(command,sizeof(command));reply_size=0;reply[0]=0;
        } else if(command_phase==3 && command_pos==send_size && !(read_reg(4)&4u)) {
            command_phase=4;reply_size=0;reply[0]=0;
        }
        if(network_info.phase==6) goto done;
        if((command_phase==1 && command_pos>0) || (command_phase==3 && command_pos>0)) {
            command_tick();goto done;
        }
        for(uint32_t budget=0;budget<2048 && (read_reg(4)&1u);++budget) {
            uint32_t value=read_reg(20);if(!(value&0x80000000u)) break;receive((uint8_t)value);if(network_info.phase==6) break;
        }
        command_tick();
        if(network_info.phase==4) {
            commit_frame();
            if(ack_pending && !command_phase) send_ack_command();
        }
    }
done:
    network_info.epoch=state->epoch;fence();++network_info.sequence;fence();publish_report(now);
}
