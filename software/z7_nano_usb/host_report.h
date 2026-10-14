#ifndef Z7_USB_HOST_REPORT_H
#define Z7_USB_HOST_REPORT_H
#include <stdint.h>
struct host_report {
    uint32_t magic,state,phase,error,vid,pid,daddr,speed;
    uint32_t config_length,hid_count,report_count,last_length,portsc,usbsts,heartbeat,stop_request;
};
struct host_snapshot {
    uint32_t device_length,config_length,hid_length[4],hid_protocol[4],raw_count,reserved;
    uint8_t device[32],config[512],hid[4][512];
    struct {uint8_t interface,length,reserved[2],data[64];} raw[16];
};
extern volatile struct host_report report;
extern struct host_snapshot snapshot;
#endif
