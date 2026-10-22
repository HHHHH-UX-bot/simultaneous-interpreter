#include <stdint.h>
#include <string.h>
#include "usb_board.h"
#include "usb_hw.h"
#include "host_report.h"
#include "tusb.h"
#include "touch_input.h"
#include "touch_format.h"

__attribute__((section(".mailbox"),used)) volatile struct host_report report;
__attribute__((section(".snapshot"),used)) struct host_snapshot snapshot;
__attribute__((section(".touch_link"),used)) volatile struct touch_link touch;
static struct touch_decoder touch_decoder;
_Static_assert(sizeof(struct host_snapshot)==3728,"Snapshot ABI changed");

void *memset(void *target,int value,size_t size) {
    uint8_t *p=target;while(size--) *p++=(uint8_t)value;return target;
}
void *memcpy(void *target,const void *source,size_t size) {
    uint8_t *d=target;const uint8_t *s=source;while(size--) *d++=*s++;return target;
}
void *memmove(void *target,const void *source,size_t size) {
    uint8_t *d=target;const uint8_t *s=source;
    if(d<=s) return memcpy(target,source,size);
    while(size) {--size;d[size]=s[size];}return target;
}
int memcmp(const void *left,const void *right,size_t size) {
    const uint8_t *a=left,*b=right;
    while(size--) {if(*a!=*b) return (int)*a-(int)*b;++a;++b;}return 0;
}
void tuh_enum_descriptor_device_cb(uint8_t address,const tusb_desc_device_t *device) {
    report.daddr=address;report.vid=device->idVendor;report.pid=device->idProduct;
    snapshot.device_length=sizeof(*device);memcpy(snapshot.device,device,sizeof(*device));
    report.phase=3;
}
bool tuh_enum_descriptor_configuration_cb(uint8_t address,uint8_t index,const tusb_desc_configuration_t *config) {
    (void)address;(void)index;
    uint16_t size=tu_le16toh(config->wTotalLength);
    if(size>sizeof(snapshot.config)) usb_fail(12);
    snapshot.config_length=size;report.config_length=size;
    memcpy(snapshot.config,config,size);return true;
}
void tuh_hid_mount_cb(uint8_t address,uint8_t interface,const uint8_t *descriptor,uint16_t size) {
    if(interface>=4 || !descriptor || size>sizeof(snapshot.hid[0])) usb_fail(13);
    if(report.vid!=TOUCH_VID || report.pid!=TOUCH_PID || interface!=0 ||
       size!=sizeof(touch_report_descriptor) || memcmp(descriptor,touch_report_descriptor,size)) usb_fail(19);
    snapshot.hid_length[interface]=size;
    snapshot.hid_protocol[interface]=tuh_hid_interface_protocol(address,interface);
    memcpy(snapshot.hid[interface],descriptor,size);
    ++report.hid_count;
    if(!tuh_hid_receive_report(address,interface)) usb_fail(14);
}
void tuh_hid_report_received_cb(uint8_t address,uint8_t interface,const uint8_t *data,uint16_t size) {
    if(size>64) usb_fail(15);
    uint32_t slot=snapshot.raw_count%16;
    snapshot.raw[slot].interface=interface;snapshot.raw[slot].length=(uint8_t)size;
    memcpy(snapshot.raw[slot].data,data,size);
    ++snapshot.raw_count;++report.report_count;report.last_length=size;
    int error=touch_receive(&touch,&touch_decoder,data,size);
    if(error) usb_fail(0x200u+(uint32_t)error);
    if(!tuh_hid_receive_report(address,interface)) usb_fail(16);
}
void tuh_mount_cb(uint8_t address) {report.speed=tuh_speed_get(address);report.phase=4;}
void tuh_umount_cb(uint8_t address) {(void)address;if(!report.stop_request) usb_fail(17);}
int main(void) {
    memset((void *)&report,0,sizeof(report));memset(&snapshot,0,sizeof(snapshot));
    report.magic=0x55534831u;report.state=1;report.phase=1;
    uint32_t sctlr;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
    if((sctlr&0x3805u) || !(usb_read(0xf8f00208u)&1)) usb_fail(1);
    touch_init(&touch,&touch_decoder,tusb_time_millis_api());
    usb_board_init();report.phase=2;
    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
    const tusb_rhport_init_t init={.role=TUSB_ROLE_HOST,.speed=TUSB_SPEED_HIGH};
    if(!tusb_init(0,&init)) usb_fail(2);
    uint32_t poll_ms=tusb_time_millis_api()-1u;
    while(!report.stop_request) {
        /* HID needs millisecond service. Repeated uncached USB/status writes
           between ticks only contend with CPU0's streaming audio traffic. */
        uint32_t now=tusb_time_millis_api();
        if(now==poll_ms) continue;
        poll_ms=now;
        if(usb_poll_enabled()) hcd_int_handler(0,false);
        tuh_task_ext(0,false);
        report.heartbeat=now;
        touch_acknowledge(&touch,(const volatile struct touch_feedback *)TOUCH_FEEDBACK_ADDRESS);
        report.portsc=usb_read(USB_BASE+0x184u);report.usbsts=usb_read(USB_BASE+0x144u);
    }
    if(!tuh_deinit(0)) usb_fail(18);
    report.usbsts=usb_read(USB_BASE+0x144u);report.phase=6;
    __asm__ volatile("dsb sy" ::: "memory");report.state=0x600d600du;
    for(;;) __asm__ volatile("wfe");
}
