/* File transport pilot: PS alone owns timed CIPSEND and IPD reception.
   The optional receiver consumes bounded cloud events; WSS setup stays host-side. */
#include "stream_pilot.h"
#include "network.h"
_Static_assert(sizeof(struct stream_pilot_status)==64,"Stream pilot public ABI");
__attribute__((section(".stream_pilot"),used)) volatile struct stream_pilot_status stream_pilot;
static struct stream_pilot_status live;
static const uint8_t *upload;
static uint8_t *download;
static uint32_t cursor,count,received,receive_overflow,report_ms;
static void (*consume)(uint8_t);
static uint32_t word(uint32_t offset) {
    uint32_t value=0;for(uint32_t i=0;i<4;++i) value|=(uint32_t)upload[offset+i]<<(8*i);
    return value;
}
static void fence(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static void publish(uint32_t now) {
    if(now-report_ms<200u && stream_pilot.phase==live.phase && stream_pilot.ack==live.ack && stream_pilot.error==live.error) return;
    report_ms=now;++stream_pilot.sequence;fence();
    volatile uint32_t *out=(volatile uint32_t *)&stream_pilot;
    const uint32_t *in=(const uint32_t *)&live;
    for(uint32_t i=0;i<16;++i) if(i!=1 && i!=4) out[i]=in[i];
    fence();++stream_pilot.sequence;fence();
}
static void receive_byte(uint8_t byte) {
    if(consume) {consume(byte);++received;return;}
    if(received==STREAM_PILOT_CAPACITY) {receive_overflow=1;return;}
    download[received++]=byte;
}
static int valid_file(void) {
    count=word(4);uint32_t size=word(8),at=12,previous=0;
    if(word(0)!=0x31465453u || !count || count>4096 || size>STREAM_PILOT_CAPACITY || size<12) return 0;
    for(uint32_t i=0;i<count;++i) {
        if(at+8>size) return 0;
        uint32_t due=word(at),bytes=word(at+4);
        if(due<previous || due>60000 || !bytes || bytes>2048 || at+8+bytes>size) return 0;
        previous=due;at+=8+bytes;
    }
    return at==size;
}
void stream_pilot_init(const uint8_t *tx,uint8_t *rx) {
    consume=0;
    upload=tx;download=rx;cursor=count=received=receive_overflow=0;
    volatile uint32_t *p=(volatile uint32_t *)&stream_pilot;
    for(uint32_t i=0;i<16;++i) p[i]=0;
    p=(volatile uint32_t *)&live;for(uint32_t i=0;i<16;++i) p[i]=0;
    live.magic=0x31505453u;report_ms=(uint32_t)-200;publish(0);
}
void stream_pilot_set_receiver(void (*receive)(uint8_t)) {consume=receive;}
void stream_pilot_tick(uint32_t now) {
    live.received=received;
    if(receive_overflow) live.error=3;
    if(stream_pilot.request!=live.ack) {
        uint32_t request=stream_pilot.request;
        if(request==1 && live.phase==0) {
            if(!valid_file()) {live.error=1;live.phase=6;}
            else if(!network_stream_attach(receive_byte)) {live.error=2;live.phase=6;}
            else {cursor=12;live.phase=1;live.start_ms=now;}
            live.ack=request;
        } else if(request==2 && (live.phase==1 || live.phase==2 || live.phase==6)) {
            if(!network_stream_busy() && network_stream_detach()) {live.phase=3;live.ack=request;}
        } else if(request==3 && live.phase==3 && network_info.phase==0) {
            uint32_t *p=(uint32_t *)&live;for(uint32_t i=0;i<16;++i) p[i]=0;
            live.magic=0x31505453u;live.ack=3;cursor=count=received=receive_overflow=0;
        }
    }
    if(live.phase==1 || live.phase==2) {
        live.network_error=network_info.error;
        live.overflow=network_info.overflow;live.framing=network_info.framing;
        if(live.error || network_info.phase!=4 || network_info.error || network_info.overflow || network_info.framing) {
            if(!live.error) live.error=4;
            live.phase=6;
        } else if(live.phase==1) {
            if(live.frames==count) {
                if(!network_stream_busy()) {live.phase=2;live.sent_ms=now-live.start_ms;}
            } else {
                uint32_t elapsed=now-live.start_ms,due=word(cursor),bytes=word(cursor+4);
                if((int32_t)(elapsed-due)>=0 && network_send(upload+cursor+8,bytes)) {
                    uint32_t lag=elapsed-due;
                    if(lag>live.max_lag_ms) live.max_lag_ms=lag;
                    ++live.frames;live.transmitted+=bytes;cursor+=8+bytes;
                }
            }
        }
    }
    live.busy=(uint32_t)network_stream_busy();publish(now);
}
