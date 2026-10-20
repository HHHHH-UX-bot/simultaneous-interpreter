#include "cloud_wire.h"
#include "cloud_events.h"
#include <string.h>
#define WSLAY_VERSION "1.1.1"
#include "../../third_party/wslay/lib/includes/wslay/wslay.h"
struct cloud_wire_context {
    wslay_frame_context_ptr frame;
    int (*message)(const char *,uint32_t,void *);
    void *user;
    struct cloud_wire_stats stats;
    uint32_t available,active,frame_read,frame_size,opcode,fin,fragmented,used,control_used,out_used;
    uint8_t incoming,control[125],out[131];
    char json[CLOUD_JSON_BYTES];
};
__attribute__((section(".cloud_runtime"))) static struct cloud_wire_context cloud_wire;
__attribute__((section(".cloud_masks"),used)) uint8_t cloud_masks[128];
static ssize_t cloud_wire_recv(uint8_t *buffer,size_t size,int flags,void *user) {
    (void)flags;(void)user;
    if(!cloud_wire.available || !size) return -1;
    buffer[0]=cloud_wire.incoming;cloud_wire.available=0;return 1;
}
static ssize_t cloud_wire_send(const uint8_t *data,size_t size,int flags,void *user) {
    (void)flags;(void)user;
    if(size>sizeof(cloud_wire.out)-cloud_wire.out_used) return -1;
    memcpy(cloud_wire.out+cloud_wire.out_used,data,size);cloud_wire.out_used+=(uint32_t)size;return (ssize_t)size;
}
static int cloud_wire_mask(uint8_t *data,size_t size,void *user) {
    (void)user;
    if(size!=4 || cloud_wire.stats.mask_used==32) return -1;
    memcpy(data,cloud_masks+cloud_wire.stats.mask_used*4,4);++cloud_wire.stats.mask_used;return 0;
}
void cloud_wire_init(int (*message)(const char *,uint32_t,void *),void *user) {
    memset(&cloud_wire,0,sizeof(cloud_wire));cloud_wire.message=message;cloud_wire.user=user;
    const struct wslay_frame_callbacks callbacks={cloud_wire_send,cloud_wire_recv,cloud_wire_mask};
    if(wslay_frame_context_init(&cloud_wire.frame,&callbacks,0)) cloud_wire.stats.error=1;
}
static int cloud_wire_control(void) {
    if(cloud_wire.opcode==WSLAY_PONG) return 0;
    if(cloud_wire.opcode==WSLAY_CONNECTION_CLOSE && cloud_wire.control_used==1) return 1;
    if(cloud_wire.opcode==WSLAY_CONNECTION_CLOSE && cloud_wire.control_used>=2u)
        cloud_wire.stats.close_code=((uint32_t)cloud_wire.control[0]<<8)|cloud_wire.control[1];
    /* RFC 6455 permits replying to the latest pending ping; close takes priority. */
    cloud_wire.out_used=0;
    struct wslay_frame_iocb out={0};out.fin=1;out.mask=1;
    out.opcode=cloud_wire.opcode==WSLAY_PING?WSLAY_PONG:WSLAY_CONNECTION_CLOSE;
    out.payload_length=out.data_length=cloud_wire.control_used;out.data=cloud_wire.control;
    if(wslay_frame_send(cloud_wire.frame,&out)!=(ssize_t)cloud_wire.control_used) return 1;
    if(cloud_wire.opcode==WSLAY_PING) ++cloud_wire.stats.pings;else cloud_wire.stats.closed=1;
    return 0;
}
void cloud_wire_byte(uint8_t byte) {
    if(cloud_wire.stats.error) return;
    if(cloud_wire.stats.closed) {cloud_wire.stats.error=2;return;}
    cloud_wire.incoming=byte;cloud_wire.available=1;
    for(;;) {
        struct wslay_frame_iocb in={0};ssize_t got=wslay_frame_recv(cloud_wire.frame,&in);
        if(got==WSLAY_ERR_WANT_READ) return;
        if(got<0) {cloud_wire.stats.error=3;return;}
        if(!cloud_wire.active) {
            if(in.rsv || in.mask || (in.opcode!=WSLAY_TEXT_FRAME && in.opcode!=WSLAY_CONTINUATION_FRAME &&
                in.opcode!=WSLAY_PING && in.opcode!=WSLAY_PONG && in.opcode!=WSLAY_CONNECTION_CLOSE)) {cloud_wire.stats.error=4;return;}
            if((in.opcode==WSLAY_TEXT_FRAME && cloud_wire.fragmented) ||
               (in.opcode==WSLAY_CONTINUATION_FRAME && !cloud_wire.fragmented)) {cloud_wire.stats.error=5;return;}
            cloud_wire.active=1;cloud_wire.frame_read=0;cloud_wire.opcode=in.opcode;cloud_wire.fin=in.fin;
            if(in.payload_length>CLOUD_JSON_BYTES ||
               (!wslay_is_ctrl_frame(in.opcode) && in.payload_length>CLOUD_JSON_BYTES-cloud_wire.used)) {cloud_wire.stats.error=6;return;}
            cloud_wire.frame_size=(uint32_t)in.payload_length;cloud_wire.control_used=0;
        }
        if(wslay_is_ctrl_frame(cloud_wire.opcode)) {
            memcpy(cloud_wire.control+cloud_wire.control_used,in.data,in.data_length);cloud_wire.control_used+=(uint32_t)in.data_length;
        } else {
            memcpy(cloud_wire.json+cloud_wire.used,in.data,in.data_length);cloud_wire.used+=(uint32_t)in.data_length;
        }
        cloud_wire.frame_read+=(uint32_t)in.data_length;
        if(cloud_wire.frame_read==cloud_wire.frame_size) {
            cloud_wire.active=0;
            if(wslay_is_ctrl_frame(cloud_wire.opcode)) {
                if(cloud_wire_control()) {cloud_wire.stats.error=7;return;}
                if(cloud_wire.stats.closed) return;
            } else {
                cloud_wire.fragmented=!cloud_wire.fin;
                if(cloud_wire.fin) {
                    ++cloud_wire.stats.messages;
                    if(cloud_wire.message(cloud_wire.json,cloud_wire.used,cloud_wire.user)) {cloud_wire.stats.error=8;return;}
                    cloud_wire.used=0;
                }
            }
        }
    }
}
const struct cloud_wire_stats *cloud_wire_status(void) {return &cloud_wire.stats;}
const uint8_t *cloud_wire_reply(uint32_t *bytes) {*bytes=cloud_wire.out_used;return cloud_wire.out;}
void cloud_wire_reply_sent(void) {cloud_wire.out_used=0;}
