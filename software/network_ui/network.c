#include "network.h"
_Static_assert(sizeof(struct network_status)==64,"Network public ABI");
_Static_assert(sizeof(struct network_configuration)==128,"Network private configuration ABI");
__attribute__((section(".network_mailbox"),used)) volatile struct network_status network_info;
__attribute__((section(".network_config"),used)) volatile struct network_configuration network_config;
#ifdef Z7_NETWORK_CAPABILITIES
_Static_assert(sizeof(struct network_capability_status)==64,"Capability public ABI");
__attribute__((section(".network_capabilities"),used)) volatile struct network_capability_status network_capabilities;
static uint32_t capability_query;
static const char *const capability_commands[]={"AT+CIPSSLSIZE?\r\n","AT+CIPSSLCCONF?\r\n",
    "AT+CIPSSLCSNI?\r\n","AT+CIPSSLCCN?\r\n","AT+CIPSNTPCFG?\r\n"};
static const char *const capability_tags[]={"+CIPSSLSIZE:","+CIPSSLCCONF:",
    "+CIPSSLCSNI:","+CIPSSLCCN:","+CIPSNTPCFG:"};
#endif
static wifi_read_fn read_reg;static wifi_write_fn write_reg;
static struct ui_state *model;
static uint32_t current_time,setup_step,enabled,ipd_left,ipd_client,line_used,frame_used,frame_expected,frame_ready;
static uint32_t command_phase,command_size,command_pos,command_deadline,reply_size,reply_line_start,ack_pending,last_request,prompt_padding;
static char command[320],reply[512],line[128],join_command[240];
enum setup_state { SETUP_ECHO,SETUP_VERSION,SETUP_STOP_SERVER,SETUP_VOLATILE,
    SETUP_STATION,SETUP_JOIN,SETUP_IP,SETUP_MULTIPLEX,SETUP_IPD_SIMPLE,
    SETUP_ACTIVE_RECEIVE,SETUP_SERVER,SETUP_COUNT };
static uint8_t frame[NET_FRAME_BYTES],ack[32];
static void fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static uint32_t length(const char *s) {uint32_t n=0;while(s[n]) ++n;return n;}
static int same(const char *a,const char *b) {while(*a && *a==*b) {++a;++b;}return *a==*b;}
static int contains(const char *s,const char *key) {
    for(uint32_t i=0;s[i];++i) {uint32_t j=0;while(key[j] && s[i+j] && key[j]==s[i+j]) ++j;if(!key[j]) return 1;}return 0;
}
static void wipe(char *p,uint32_t n) {volatile char *v=p;while(n--) *v++=0;fence();}
static void fault(uint32_t error) {
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {network_capabilities.phase=6;network_capabilities.error=error;}
#endif
    network_info.phase=6;network_info.error=error;command_phase=0;ack_pending=0;
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
    n+=decimal(text+n,network_info.client);text[n++]=',';n+=decimal(text+n,32);
    text[n++]='\r';text[n++]='\n';text[n]=0;begin_command(text,4000);
}
static void issue_setup(void) {
    static const char *const commands[]={"ATE0\r\n","AT+GMR\r\n","AT+CIPSERVER=0\r\n","AT+SYSSTORE=0\r\n",
        "AT+CWMODE=1\r\n",0,"AT+CIFSR\r\n","AT+CIPMUX=1\r\n","AT+CIPDINFO=0\r\n","AT+CIPRECVMODE=0\r\n","AT+CIPSERVER=1,8266\r\n"};
    _Static_assert(sizeof(commands)/sizeof(commands[0])==SETUP_COUNT,"Setup command count");
    network_info.last_code=setup_step;
    begin_command(setup_step==SETUP_JOIN?join_command:commands[setup_step],setup_step==SETUP_JOIN?25000u:4000u);
}
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
    int valid=network_config.magic==0x3146434eu && network_config.action==1 && sn>=1 && sn<=32 && pn>=8 && pn<=64 &&
        (network_config.port==0 || network_config.port==8266) && network_config.reserved==0;
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
    net_ack(ack,frame,code,model);ack_pending=1;frame_used=frame_ready=0;
}
static void line_event(void) {
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {
        if(same(line,"ready")) fault(34);
        return; // Queries do not own a Wi-Fi link or TCP client.
    }
#endif
    if(contains(line,"WIFI DISCONNECT") && network_info.phase>=3) {fault(23);return;}
    if(same(line,"ready") && network_info.echo_verified) {fault(34);return;}
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
        if(network_info.phase==4 && ipd_client==network_info.client) frame_byte(byte);
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
    if((int32_t)(current_time-command_deadline)>=0) {fault(27);return;}
    if(command_phase==1) {
        // The bounded drain may leave old RX bytes queued. Send only after
        // that backlog is empty, so it cannot certify a new transaction.
        if(!command_pos && (read_reg(4)&1u)) return;
        for(uint32_t budget=0;budget<64 && command_pos<command_size && (read_reg(4)&2u);++budget)
            write_reg(16,(uint8_t)command[command_pos++]);
        if(command_pos<command_size || (read_reg(4)&4u)) return;
        command_phase=2;wipe(command,sizeof(command));reply_size=0;reply[0]=0;
    } else if(command_phase==3) {
        for(uint32_t budget=0;budget<32 && command_pos<32 && (read_reg(4)&2u);++budget) write_reg(16,ack[command_pos++]);
        if(command_pos<32 || (read_reg(4)&4u)) return;
        command_phase=4;reply_size=0;reply[0]=0;
    }
    if(command_phase==4) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) fault(28);
        else if(contains(reply,"\r\nSEND OK\r\n")) {command_phase=ack_pending=0;wipe(reply,sizeof(reply));}
        return;
    }
    if(command_phase!=2) return;
    if(network_info.phase==4 && ack_pending) {
        if(contains(reply,"ERROR") || contains(reply,"FAIL")) fault(28);
        else if(contains(reply,">")) {command_phase=3;command_pos=0;reply_size=0;reply[0]=0;command_deadline=current_time+4000;}
        return; // An earlier OK is not the CIPSEND prompt.
    }
    int failure=contains(reply,"\r\nERROR\r\n") || contains(reply,"\r\nFAIL\r\n");
    if(!contains(reply,"\r\nOK\r\n") && !failure) return;
#ifdef Z7_NETWORK_CAPABILITIES
    if(network_capabilities.phase==1) {capability_result(failure);return;}
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
    if(++setup_step<SETUP_COUNT) issue_setup();
    else {network_info.phase=3;enabled=1;(void)ui_control(model,UI_CLEAR);(void)ui_control(model,UI_START);}
}
void network_init(wifi_read_fn r,wifi_write_fn w) {
    read_reg=r;write_reg=w;
    volatile uint32_t *words=(volatile uint32_t *)&network_info;for(uint32_t i=0;i<16;++i) words[i]=0;
    words=(volatile uint32_t *)&network_config;for(uint32_t i=0;i<32;++i) words[i]=0;
#ifdef Z7_NETWORK_CAPABILITIES
    words=(volatile uint32_t *)&network_capabilities;for(uint32_t i=0;i<16;++i) words[i]=0;
    network_capabilities.magic=0x43415031u;capability_query=0;
#endif
    network_info.magic=0x3154454eu;network_info.client=0xffffffffu;network_config.magic=0x3146434eu;
    enabled=ipd_left=line_used=frame_used=frame_ready=command_phase=ack_pending=prompt_padding=0;wipe(join_command,sizeof(join_command));
}
int network_owns_ui(void) {return enabled!=0;}
void network_tick(struct ui_state *state,uint32_t now) {
    model=state;current_time=now;++network_info.sequence;fence();
    configuration();network_info.overflow=read_reg(32);network_info.framing=read_reg(36);
    if(network_info.overflow || network_info.framing) fault(32);
    if(network_info.phase!=6 && network_info.phase!=0) {
        if(command_phase && (int32_t)(current_time-command_deadline)>=0) fault(27);
        // Promote a fully transmitted command BEFORE draining its response.
        // RX remains buffered while FIFO/prefetch/PHY is still busy.
        if(command_phase==1 && command_pos==command_size && !(read_reg(4)&4u)) {
            command_phase=2;wipe(command,sizeof(command));reply_size=0;reply[0]=0;
        } else if(command_phase==3 && command_pos==32 && !(read_reg(4)&4u)) {
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
    network_info.epoch=state->epoch;fence();++network_info.sequence;fence();
}
