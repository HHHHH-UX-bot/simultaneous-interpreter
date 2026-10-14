#include "usb_board.h"
#include "usb_hw.h"
#include "host_report.h"
#include "tusb.h"
#include "host/hcd.h"
#include "portable/ehci/ehci_api.h"

uint32_t usb_read(uintptr_t address) {return *(volatile uint32_t *)address;}
void usb_write(uintptr_t address,uint32_t value) {
    *(volatile uint32_t *)address=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint64_t ticks(void) {
    uint32_t hi,lo;
    do {hi=usb_read(0xf8f00204u);lo=usb_read(0xf8f00200u);} while(hi!=usb_read(0xf8f00204u));
    return ((uint64_t)hi<<32)|lo;
}
uint32_t tusb_time_millis_api(void) {return (uint32_t)(ticks()/333333u);}
void usb_stack_assert(void) {usb_fail(0x1000u);}
__attribute__((noreturn)) void usb_fail(uint32_t error) {
    report.error=error;
    report.usbsts=usb_read(USB_BASE+0x144u);
    report.portsc=usb_read(USB_BASE+0x184u);
    usb_write(USB_BASE+0x148u,0);
    usb_write(USB_BASE+0x140u,usb_read(USB_BASE+0x140u)&~1u);
    __asm__ volatile("dsb sy" ::: "memory");
    report.state=0xbad0bad0u;
    __asm__ volatile("dsb sy" ::: "memory");
    for(;;) __asm__ volatile("wfe");
}
void usb_wait(uintptr_t address,uint32_t mask,uint32_t expected,uint32_t error) {
    uint32_t start=tusb_time_millis_api();
    __asm__ volatile("dsb sy" ::: "memory");
    while((usb_read(address)&mask)!=expected) {
        if(tusb_time_millis_api()-start>=500u) usb_fail(error);
    }
}
static uint8_t ulpi(uint8_t address,bool write,uint8_t value) {
    if(!(usb_read(USB_BASE+0x170u)&0x08000000u)) {
        usb_write(USB_BASE+0x170u,0x80000000u);
        usb_wait(USB_BASE+0x170u,0x80000000u,0,5);
    }
    usb_write(USB_BASE+0x170u,0x40000000u|(write?0x20000000u:0)|((uint32_t)address<<16)|value);
    usb_wait(USB_BASE+0x170u,0x40000000u,0,6);
    return (uint8_t)(usb_read(USB_BASE+0x170u)>>8);
}
void usb_board_init(void) {
    usb_write(0xf8000008u,0xdf0du);
    for(uint32_t i=0;i<sizeof(usb_mio_config)/sizeof(usb_mio_config[0]);++i) {
        const uint32_t *p=usb_mio_config[i];
        usb_write(p[0],(usb_read(p[0])&~p[1])|(p[2]&p[1]));
    }
    usb_write(0xf8000700u+4u*USB_RESET_MIO,USB_RESET_CONFIG);
    usb_write(0xf800012cu,usb_read(0xf800012cu)|4u);
    usb_write(0xf8000004u,0x767bu);
    const uint32_t pin=1u<<(USB_RESET_MIO-32u);
    usb_write(0xe000a008u,((~pin&0xffffu)<<16));
    usb_write(0xe000a244u,usb_read(0xe000a244u)|pin);
    usb_write(0xe000a248u,usb_read(0xe000a248u)|pin);
    tusb_time_delay_ms_api(10);
    usb_write(0xe000a008u,((~pin&0xffffu)<<16)|pin);
    tusb_time_delay_ms_api(20);
    if(usb_read(USB_BASE+0x100u)!=0x01000040u) usb_fail(3);
    usb_write(USB_BASE+0x140u,2);
    usb_wait(USB_BASE+0x140u,2,0,4);
    ulpi(0x16,true,0x5a);
    uint8_t scratch=ulpi(0x16,false,0);
    ulpi(0x16,true,0);
    if(scratch!=0x5a) usb_fail(8);
    uint32_t vid=ulpi(0,false,0);vid|=(uint32_t)ulpi(1,false,0)<<8;
    uint32_t pid=ulpi(2,false,0);pid|=(uint32_t)ulpi(3,false,0)<<8;
    if(vid!=0x424u || pid!=7) usb_fail(7);
    usb_write(USB_BASE+0x1a8u,3);
    usb_write(USB_BASE+0x184u,(usb_read(USB_BASE+0x184u)&~0x2au)|0x1000u);
    if(usb_read(USB_BASE+0x184u)&0x10u) usb_fail(10);
}

static bool poll_enabled;
bool usb_poll_enabled(void) {return poll_enabled;}
void hcd_int_enable(uint8_t rhport) {(void)rhport;poll_enabled=true;}
void hcd_int_disable(uint8_t rhport) {(void)rhport;poll_enabled=false;}
bool hcd_init(uint8_t rhport,const tusb_rhport_init_t *init) {
    if(rhport || init->role!=TUSB_ROLE_HOST) return false;
    bool ready=ehci_init(rhport,USB_BASE+0x100u,USB_BASE+0x140u);
    usb_wait(USB_BASE+0x144u,0x1000u,0,11);
    return ready;
}
bool hcd_deinit(uint8_t rhport) {poll_enabled=false;return ehci_deinit(rhport);}
bool hcd_dcache_clean(const void *address,uint32_t size) {
    (void)address;(void)size;__asm__ volatile("dsb sy" ::: "memory");return true;
}
bool hcd_dcache_invalidate(const void *address,uint32_t size) {return hcd_dcache_clean(address,size);}
bool hcd_dcache_clean_invalidate(const void *address,uint32_t size) {return hcd_dcache_clean(address,size);}
