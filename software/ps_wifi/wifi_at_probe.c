#include "wifi_at_probe.h"
_Static_assert(sizeof(struct wifi_status)==64,"Wi-Fi status ABI");
__attribute__((section(".wifi_mailbox"),used)) volatile struct wifi_status wifi_info;
static wifi_read_fn reg_read;
static wifi_write_fn reg_write;
static char response[512];
static uint32_t response_size,step,tx_pos,deadline;
static const char *pending;
static const char *const commands[]={"AT\r\n","ATE0\r\n","AT\r\n","AT+GMR\r\n"
#ifdef Z7_UART_921600
    ,"AT+UART_CUR?\r\n"
#endif
};
#ifdef Z7_UART_921600
enum { BAUD_AT=100,BAUD_SET,BAUD_SETTLE };
static uint32_t settle_limit;
#endif
static void fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static int contains(const char *text,const char *needle) {
    for(uint32_t i=0;text[i];++i) {
        uint32_t j=0;while(needle[j] && text[i+j] && text[i+j]==needle[j]) ++j;
        if(!needle[j]) return 1;
    }
    return 0;
}
static void error(uint32_t code) {wifi_info.phase=6;wifi_info.error=code;pending=0;}
static void issue(uint32_t now) {
    response_size=0;response[0]=0;tx_pos=0;deadline=now+3000u;pending=commands[step];
}
#ifdef Z7_UART_921600
static void baud_issue(uint32_t now,uint32_t state) {
    step=state;wifi_info.step=state;
    response_size=0;response[0]=0;tx_pos=0;deadline=now+3000u;
    pending=state==BAUD_AT?"\r\nAT\r\n":"AT+UART_CUR=921600,8,1,0,0\r\n";
}
static int divider_idle(void) {return (reg_read(4)&0x10cu)==0x100u;}
static int high_divider(void) {
    reg_write(12,15);reg_write(8,54);
    if(reg_read(8)!=54u) {error(9);return 0;}
    wifi_info.reserved0=54;return 1;
}
static uint32_t module_baud(void) {
    const char *p=response,*prefix="+UART_CUR:",*suffix=",8,1,0,0\r\n";
    for(;*p;++p) {
        uint32_t i=0;while(prefix[i] && p[i] && prefix[i]==p[i]) ++i;
        if(!prefix[i]) break;
    }
    if(!*p) return 0;
    p+=10;
    uint32_t value=0,digits=0;
    while(*p>='0' && *p<='9' && digits<7u) {value=value*10u+(uint32_t)(*p++-'0');++digits;}
    if(!digits || value<912384u || value>930816u) return 0;
    for(uint32_t i=0;suffix[i];++i) if(p[i]!=suffix[i]) return 0;
    return value;
}
#endif
void wifi_probe_init(wifi_read_fn read,wifi_write_fn write,uint32_t now) {
    reg_read=read;reg_write=write;
    /* The module corrupts the first byte after a long idle: warm the link once. */
    write(16,13);for(volatile uint32_t i=0;i<20000;++i){}
    write(12,15);
    volatile uint32_t *w=(volatile uint32_t *)&wifi_info;
    for(uint32_t i=0;i<16u;++i) w[i]=0;
    wifi_info.sequence=1;fence();
    wifi_info.magic=0x57505331u;wifi_info.phase=1;
    step=0;pending=0;
    if(reg_read(0)!=0x57494631u) {error(1);goto done;}
    if(reg_read(8)!=434u
#ifdef Z7_UART_921600
        && reg_read(8)!=54u
#endif
    ) {error(2);goto done;}
    // This reset does not change ESP firmware, power or nonvolatile settings.
    reg_write(12,15);
#ifdef Z7_UART_921600
    wifi_info.reserved0=reg_read(8);baud_issue(now,BAUD_AT);
#else
    issue(now);
#endif
done:
    fence();++wifi_info.sequence;fence();
}
void wifi_probe_tick(uint32_t now) {
    if(wifi_info.phase==6u) return;
    ++wifi_info.sequence;fence();
    ++wifi_info.ticks;
    wifi_info.rx_received=reg_read(44);wifi_info.tx_started=reg_read(40);
    wifi_info.rx_overflow=reg_read(32);wifi_info.framing_error=reg_read(36);
#ifdef Z7_UART_921600
    if(step==BAUD_SETTLE) {
        // UART_CUR may acknowledge at either rate. Observe a guard interval,
        // then prove the new rate with fresh AT/echo/GMR/config transactions.
        if((int32_t)(now-settle_limit)>=0) {error(9);goto done;}
        if((int32_t)(now-deadline)>=0 && divider_idle() && high_divider()) baud_issue(now,BAUD_AT);
        goto done;
    }
    if(step<BAUD_AT && (wifi_info.rx_overflow || wifi_info.framing_error)) {error(3);goto done;}
#else
    if(wifi_info.rx_overflow || wifi_info.framing_error) {error(3);goto done;}
#endif
    if(wifi_info.phase==2u) goto done;
    // Expired observations cannot certify a command, even if an OK is now queued.
    if((int32_t)(now-deadline)>=0) {
#ifdef Z7_UART_921600
        // After a JTAG reload the ESP may still be at the accepted fast rate.
        // Only the two known rates are attempted; never write UART_DEF/flash.
        if(step==BAUD_AT && reg_read(8)==434u && divider_idle() && high_divider()) {
            baud_issue(now,BAUD_AT);goto done;
        }
#endif
        error(8);goto done;
    }
    if(pending) {
        if(tx_pos==0u) {
            // No command has been sent yet: discard residual replies first.
            for(uint32_t budget=0;budget<512u && (reg_read(4)&1u);++budget) (void)reg_read(20);
            if(reg_read(4)&1u) goto done;
        }
        for(uint32_t budget=0;budget<64u && pending[tx_pos] && (reg_read(4)&2u);++budget)
            reg_write(16,(uint8_t)pending[tx_pos++]);
        // TX busy includes the FIFO, prefetch and PHY's final stop bit. Only
        // bytes observed after this boundary may certify this transaction.
        if(pending[tx_pos] || (reg_read(4)&4u)) goto done;
        pending=0;
#ifdef Z7_UART_921600
        if(step==BAUD_SET) {
            step=BAUD_SETTLE;wifi_info.step=step;deadline=now+100u;settle_limit=now+3000u;goto done;
        }
#endif
    }
    for(uint32_t budget=0;budget<512u && (reg_read(4)&1u);++budget) {
        uint32_t v=reg_read(20);
        if(!(v&0x80000000u)) break;
        if(response_size+1u>=sizeof(response)) {error(4);goto done;}
        response[response_size++]=(char)(v&255u);response[response_size]=0;
    }
#ifdef Z7_UART_921600
    if(step==BAUD_AT) {
        if(contains(response,"\r\nOK\r\n")) {
            if(reg_read(8)==434u) baud_issue(now,BAUD_SET);
            else {reg_write(12,15);step=0;issue(now);wifi_info.step=0;}
        }
        goto done;
    }
#endif
    if(contains(response,"\r\nERROR\r\n") || contains(response,"\r\nFAIL\r\n")) {error(5);goto done;}
    if(contains(response,"\r\nOK\r\n")) {
        if(step==0u) wifi_info.at_ok=1;
        // Do not regard ATE0's OK alone as proof. The next AT must not echo.
        if(step==2u) {
            if(contains(response,"AT\r\n")) {error(6);goto done;}
            wifi_info.echo_off=1;
        }
        if(step==3u) {
            if(!contains(response,"version:")) {error(7);goto done;}
            uint32_t hash=2166136261u;
            for(uint32_t i=0;i<response_size;++i) hash=(hash^(uint8_t)response[i])*16777619u;
            wifi_info.firmware_hash=hash;wifi_info.firmware_ok=1;
#ifdef Z7_UART_921600
            ++step;issue(now);
#else
            wifi_info.phase=2;
#endif
#ifdef Z7_UART_921600
        } else if(step==4u) {
            wifi_info.reserved1=module_baud();
            if(!wifi_info.reserved1) {error(10);goto done;}
            wifi_info.phase=2;
#endif
        } else {++step;issue(now);}
        wifi_info.step=step;goto done;
    }
done:
    fence();++wifi_info.sequence;fence();
}
