#include <string.h>
#include "touch_input.h"
#include "touch_format.h"
#if defined(__arm__)
static void publish_barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
#else
static void publish_barrier(void) {__asm__ volatile("" ::: "memory");}
#endif
static uint32_t le16(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8);}
void touch_acknowledge(volatile struct touch_link *link,const volatile struct touch_feedback *feedback) {
    uint32_t sequence=feedback->sequence;
    if(feedback->magic!=TOUCH_FEEDBACK_MAGIC || sequence&1u) return;
    publish_barrier();
    uint32_t generation=feedback->generation,read=feedback->read_taps;
    publish_barrier();
    if(feedback->sequence!=sequence || generation!=link->generation || read==link->tap_read ||
       read-link->tap_read>link->tap_write-link->tap_read) return;
    ++link->sequence;publish_barrier();link->tap_read=read;
    publish_barrier();++link->sequence;publish_barrier();
}
void touch_init(volatile struct touch_link *link,struct touch_decoder *decoder,uint32_t generation) {
    link->magic=0;publish_barrier();
    memset((void *)link,0,sizeof(*link));memset(decoder,0,sizeof(*decoder));
    link->generation=generation;link->min_x=4096;link->min_y=4096;
    publish_barrier();link->magic=TOUCH_LINK_MAGIC;publish_barrier();
}
int touch_receive(volatile struct touch_link *link,struct touch_decoder *decoder,const uint8_t *data,uint32_t size) {
    if(size!=TOUCH_REPORT_BYTES || data[0]!=TOUCH_REPORT_ID) return 1;
    uint32_t count=data[TOUCH_COUNT_OFFSET];
    if(count>TOUCH_SLOTS) return 2;
    uint32_t active=0,id=link->contact_id,raw_x=link->raw_x,raw_y=link->raw_y;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *p=data+1u+i*TOUCH_STRIDE;
        uint32_t x=le16(p+2),y=le16(p+4);
        if(x>TOUCH_RAW_MAX || y>TOUCH_RAW_MAX) return 3;
        if(p[0]&1u) {++active;raw_x=x;raw_y=y;id=p[1];}
        else if(p[1]==link->contact_id && active==0) {raw_x=x;raw_y=y;}
    }
    uint32_t previous=link->pressed;
    if(active>1u || (previous && active==1u && id!=link->contact_id)) decoder->blocked=1;
    if(active==0) decoder->blocked=0;
    uint32_t pressed=active==1u && !decoder->blocked;
    uint32_t x=raw_x*1023u/TOUCH_RAW_MAX,y=raw_y*599u/TOUCH_RAW_MAX;
    ++link->sequence;publish_barrier();
    ++link->reports;
    if(decoder->blocked) decoder->armed=0;
    if(pressed && !previous) {
        ++link->downs;decoder->press_x=x;decoder->press_y=y;decoder->armed=1;
    }
    if(pressed && previous && (x!=link->x || y!=link->y)) ++link->moves;
    if(!pressed && previous) {
        ++link->ups;
        if(decoder->armed && active==0) {
            uint32_t write=link->tap_write;
            if(write-link->tap_read<TOUCH_TAPS) {
                volatile struct touch_tap *tap=&link->taps[write%TOUCH_TAPS];
                tap->press_x=decoder->press_x;tap->press_y=decoder->press_y;tap->x=x;tap->y=y;
                publish_barrier();link->tap_write=write+1u;
            } else ++link->dropped_taps;
        }
        decoder->armed=0;
    }
    link->pressed=pressed;link->multitouch=decoder->blocked;link->x=x;link->y=y;
    link->raw_x=raw_x;link->raw_y=raw_y;link->contact_id=id;
    if(pressed) {
        if(raw_x<link->min_x) link->min_x=raw_x;
        if(raw_x>link->max_x) link->max_x=raw_x;
        if(raw_y<link->min_y) link->min_y=raw_y;
        if(raw_y>link->max_y) link->max_y=raw_y;
    }
    publish_barrier();++link->sequence;publish_barrier();
    return 0;
}
