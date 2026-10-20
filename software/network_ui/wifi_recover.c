#include "wifi_recover.h"
enum { FAST, LEGACY, EXTRA, SLOW, RESET, BOOT_WAIT, STARTUP, SET_SPEED, SPEED_WAIT, VERIFY, QUERY, DONE, FAILED };
static wifi_read_fn rd;
static wifi_write_fn wr;
static uint32_t stage,deadline,position,used,attempt,rate,now_ms,rate_deadline;
static const char *command;
static char reply[128];
static void issue(uint32_t next,const char *text,uint32_t timeout) {
    stage=next;command=text;position=used=0;reply[0]=0;deadline=now_ms+timeout;
}
static void choose_rate(uint32_t divisor) {
    wr(12,15);rate=divisor;rate_deadline=now_ms+100u;
}
static int starts(const char *p,const char *text) {
    while(*text) if(*p++!=*text++) return 0;
    return 1;
}
static const char *find(const char *text) {
    for(const char *p=reply;*p;++p) if(starts(p,text)) return p;
    return 0;
}
static int has(const char *text) {return find(text)!=0;}
void wifi_recover_begin(wifi_read_fn read,wifi_write_fn write,uint32_t now) {
    rd=read;wr=write;now_ms=now;attempt=0;
    choose_rate(WIFI_STREAM_DIVISOR);issue(FAST,"\r\nAT\r\n",300u);
}
uint32_t wifi_recover_phase(void) {return stage;}
int wifi_recover_tick(uint32_t now) {
    now_ms=now;
    if(stage==DONE) return 1;
    if(stage==FAILED) return -1;
    if(rate) {
        if(rd(8)!=rate) {
            if((rd(4)&0x10cu)==0x100u) wr(8,rate);
            if(rd(8)!=rate) {
                if((int32_t)(now-rate_deadline)>=0) stage=FAILED;
                return 0;
            }
        }
        rate=0;
    }
    /* Keep UART ownership until the final stop bit; early RX stays buffered. */
    if(command && !command[position] && (rd(4)&4u)) {
        if((int32_t)(now-deadline)>=0) {stage=FAILED;return -1;}
        return 0;
    }
    for(uint32_t n=0;n<1024u && (rd(4)&1u);++n) {
        uint32_t v=rd(20);if(!(v&0x80000000u)) break;
        if(command && !command[position] && used+1u<sizeof(reply)) {
            reply[used++]=(char)v;reply[used]=0;
        }
    }
    if(stage==BOOT_WAIT || stage==SPEED_WAIT) {
        if((int32_t)(now-deadline)<0) return 0;
        if(stage==BOOT_WAIT) {choose_rate(434);attempt=0;issue(STARTUP,"\r\nAT\r\n",600u);}
        else {choose_rate(WIFI_STREAM_DIVISOR);issue(VERIFY,"AT\r\n",600u);}
        return 0;
    }
    if(command[position]) {
        for(uint32_t n=0;n<32u && command[position] && (rd(4)&2u);++n) wr(16,(uint8_t)command[position++]);
        if(command[position] || (rd(4)&4u)) {
            if((int32_t)(now-deadline)>=0) stage=FAILED;
            return 0;
        }
    }
    if(stage==RESET || stage==SET_SPEED) {
        stage=stage==RESET?BOOT_WAIT:SPEED_WAIT;
        command=0;deadline=now+(stage==BOOT_WAIT?2500u:50u);return 0;
    }
    if(has("\r\nOK\r\n")) {
        if(stage==FAST || stage==LEGACY || stage==EXTRA || stage==SLOW) issue(RESET,"AT+RST\r\n",600u);
        else if(stage==STARTUP) issue(SET_SPEED,WIFI_STREAM_UART_COMMAND,600u);
        else if(stage==VERIFY) issue(QUERY,"AT+UART_CUR?\r\n",600u);
        else if(stage==QUERY) {
            const char *p=find("+UART_CUR:");uint32_t baud=0;
            if(p) {p+=10;while(*p>='0' && *p<='9') {baud=baud*10u+(uint32_t)(*p++-'0');if(baud>2100000u) break;}}
            /* ESP8266 divides its 80 MHz UART clock by an integer: a requested
               921600 is reported as 930232. Do not reject that working rate
               after reset; still reject a different baud or UART format. */
            if(!p || baud<WIFI_STREAM_BAUD*980u/1000u || baud>WIFI_STREAM_BAUD*1020u/1000u || !starts(p,",8,1,0,0\r\n")) stage=FAILED;
            else {wr(12,8);stage=DONE;return 1;}
        }
    } else if((int32_t)(now-deadline)>=0) {
        if((stage==FAST || stage==LEGACY || stage==EXTRA || stage==SLOW || stage==STARTUP) && ++attempt<2u) issue(stage,"\r\nAT\r\n",400u);
        else if(stage==FAST) {attempt=0;choose_rate(WIFI_STREAM_DIVISOR==25u?54u:25u);issue(LEGACY,"\r\nAT\r\n",400u);}
        else if(stage==LEGACY && WIFI_STREAM_DIVISOR!=25u && WIFI_STREAM_DIVISOR!=54u) {attempt=0;choose_rate(54);issue(EXTRA,"\r\nAT\r\n",400u);}
        else if(stage==LEGACY || stage==EXTRA) {attempt=0;choose_rate(434);issue(SLOW,"\r\nAT\r\n",400u);}
        else stage=FAILED;
    }
    return stage==FAILED?-1:0;
}
