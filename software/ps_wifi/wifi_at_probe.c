#include "wifi_at_probe.h"
_Static_assert(sizeof(struct wifi_status)==64,"Wi-Fi status ABI");
__attribute__((section(".wifi_mailbox"),used)) volatile struct wifi_status wifi_info;
static wifi_read_fn reg_read;
static wifi_write_fn reg_write;
static char response[512];
static uint32_t response_size,step,tx_pos,deadline;
static const char *pending;
static const char *const commands[]={"AT\r\n","ATE0\r\n","AT\r\n","AT+GMR\r\n"};
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
void wifi_probe_init(wifi_read_fn read,wifi_write_fn write,uint32_t now) {
    reg_read=read;reg_write=write;
    volatile uint32_t *w=(volatile uint32_t *)&wifi_info;
    for(uint32_t i=0;i<16u;++i) w[i]=0;
    wifi_info.sequence=1;fence();
    wifi_info.magic=0x57505331u;wifi_info.phase=1;
    step=0;pending=0;
    if(reg_read(0)!=0x57494631u) {error(1);goto done;}
    if(reg_read(8)!=434u) {error(2);goto done;}
    // This reset does not change ESP firmware, power or nonvolatile settings.
    reg_write(12,15);issue(now);
done:
    fence();++wifi_info.sequence;fence();
}
void wifi_probe_tick(uint32_t now) {
    if(wifi_info.phase==6u) return;
    ++wifi_info.sequence;fence();
    ++wifi_info.ticks;
    wifi_info.rx_received=reg_read(44);wifi_info.tx_started=reg_read(40);
    wifi_info.rx_overflow=reg_read(32);wifi_info.framing_error=reg_read(36);
    if(wifi_info.rx_overflow || wifi_info.framing_error) {error(3);goto done;}
    if(wifi_info.phase==2u) goto done;
    // Expired observations cannot certify a command, even if an OK is now queued.
    if((int32_t)(now-deadline)>=0) {error(8);goto done;}
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
    }
    for(uint32_t budget=0;budget<512u && (reg_read(4)&1u);++budget) {
        uint32_t v=reg_read(20);
        if(!(v&0x80000000u)) break;
        if(response_size+1u>=sizeof(response)) {error(4);goto done;}
        response[response_size++]=(char)(v&255u);response[response_size]=0;
    }
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
            wifi_info.firmware_hash=hash;wifi_info.firmware_ok=1;wifi_info.phase=2;
        } else {++step;issue(now);}
        wifi_info.step=step;goto done;
    }
done:
    fence();++wifi_info.sequence;fence();
}
