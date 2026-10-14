#include <stdint.h>
#include "usb_hw.h"

struct usb_report {
    uint32_t magic,state,phase,error,controller_id,capability,hcsparams,hccparams;
    uint32_t phy_vid,phy_pid,scratch,portsc,usbmode,viewport,cpu_sctlr,reserved;
};
__attribute__((section(".mailbox"),used)) volatile struct usb_report report;
static inline uint32_t rd(uint32_t address) {return *(volatile uint32_t *)address;}
static inline void wr(uint32_t address,uint32_t value) {*(volatile uint32_t *)address=value;}
static inline void barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
static uint64_t ticks(void) {
    uint32_t high,low;
    do {high=rd(0xf8f00204u);low=rd(0xf8f00200u);} while(high!=rd(0xf8f00204u));
    return ((uint64_t)high<<32)|low;
}
__attribute__((noreturn)) static void finish(uint32_t error) {
    report.error=error;barrier();report.state=error?0xbad0bad0u:0x600d600du;barrier();
    for(;;) __asm__ volatile("wfe");
}
static void delay_ms(uint32_t ms) {
    uint64_t start=ticks();
    while(ticks()-start<(uint64_t)ms*333333u) {}
}
static void wait_bits(uint32_t address,uint32_t mask,uint32_t expected,uint32_t error) {
    uint64_t start=ticks();
    while((rd(address)&mask)!=expected) {
        if(ticks()-start>500u*333333u) finish(error);
    }
}
static uint8_t ulpi(uint8_t address,int write,uint8_t value) {
    if(!(rd(USB_BASE+0x170u)&0x08000000u)) {
        wr(USB_BASE+0x170u,0x80000000u);barrier();
        wait_bits(USB_BASE+0x170u,0x80000000u,0,5);
    }
    wr(USB_BASE+0x170u,0x40000000u|(write?0x20000000u:0)|((uint32_t)address<<16)|value);
    barrier();wait_bits(USB_BASE+0x170u,0x40000000u,0,6);
    report.viewport=rd(USB_BASE+0x170u);
    return (uint8_t)(report.viewport>>8);
}
int main(void) {
    volatile uint32_t *words=(volatile uint32_t *)&report;
    for(uint32_t i=0;i<sizeof(report)/4u;++i) words[i]=0;
    report.magic=0x55535031u;report.state=1;report.phase=1;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(report.cpu_sctlr));
    if(report.cpu_sctlr&0x3805u) finish(1);
    if(!(rd(0xf8f00208u)&1)) finish(2);
    report.phase=2;
    wr(0xf8000008u,0xdf0du);
    for(uint32_t i=0;i<sizeof(usb_mio_config)/sizeof(usb_mio_config[0]);++i) {
        const uint32_t *p=usb_mio_config[i];
        wr(p[0],(rd(p[0])&~p[1])|(p[2]&p[1]));
    }
    wr(0xf8000700u+4u*USB_RESET_MIO,USB_RESET_CONFIG);
    wr(0xf800012cu,rd(0xf800012cu)|4u); // USB0 CPU_1x APB clock only.
    wr(0xf8000004u,0x767bu);barrier();
    const uint32_t pin=1u<<(USB_RESET_MIO-32u);
    wr(0xe000a008u,((~pin&0xffffu)<<16)); // Masked GPIO bank1 low half: reset asserted.
    wr(0xe000a244u,rd(0xe000a244u)|pin);
    wr(0xe000a248u,rd(0xe000a248u)|pin);barrier();delay_ms(10);
    wr(0xe000a008u,((~pin&0xffffu)<<16)|pin);barrier();delay_ms(20);
    report.phase=3;
    report.controller_id=rd(USB_BASE);
    report.capability=rd(USB_BASE+0x100u);
    report.hcsparams=rd(USB_BASE+0x104u);report.hccparams=rd(USB_BASE+0x108u);
    if((report.capability&0xffu)!=0x40u || (report.capability>>16)!=0x100u ||
       (report.hcsparams&15u)!=1u) finish(3);
    wr(USB_BASE+0x140u,2u);barrier();wait_bits(USB_BASE+0x140u,2u,0,4);
    report.phase=4;
    // Validate ULPI write/read first, as in the Linux ULPI initialization sequence.
    // RESETB restored the scratch register to its documented default of zero.
    ulpi(0x16,1,0x5a);report.scratch=ulpi(0x16,0,0);
    ulpi(0x16,1,0);
    if(report.scratch!=0x5au) finish(8);
    report.phy_vid=(uint32_t)ulpi(0,0,0)|((uint32_t)ulpi(1,0,0)<<8);
    report.phy_pid=(uint32_t)ulpi(2,0,0)|((uint32_t)ulpi(3,0,0)<<8);
    if(report.phy_vid!=0x0424u || report.phy_pid!=7u) finish(7);
    report.phase=5;
    wr(USB_BASE+0x1a8u,3u);barrier(); // Finish direct PHY accesses before Host owns ULPI.
    wr(USB_BASE+0x184u,(rd(USB_BASE+0x184u)&~0x2au)|0x1000u);barrier();delay_ms(100);
    report.portsc=rd(USB_BASE+0x184u);report.usbmode=rd(USB_BASE+0x1a8u);
    if((report.usbmode&3u)!=3u) finish(9);
    if(report.portsc&0x10u) finish(10);
    report.phase=6;finish(0);
}
