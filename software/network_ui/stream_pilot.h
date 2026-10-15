#ifndef NETWORK_STREAM_PILOT_H
#define NETWORK_STREAM_PILOT_H
#include <stdint.h>
#define STREAM_PILOT_CAPACITY 0x80000u
#define STREAM_PILOT_TX_ADDRESS 0x01c00000u
#define STREAM_PILOT_RX_ADDRESS (STREAM_PILOT_TX_ADDRESS+STREAM_PILOT_CAPACITY)
struct stream_pilot_status {
    uint32_t magic,sequence,phase,error,request,ack,frames,transmitted;
    uint32_t received,max_lag_ms,start_ms,sent_ms,busy,network_error,overflow,framing;
};
extern volatile struct stream_pilot_status stream_pilot;
/* TX: magic,count,total_bytes, then {due_ms,size,payload} records.
   RX: exact incoming TCP bytes, append-only. Both areas have CAPACITY bytes. */
void stream_pilot_init(const uint8_t *tx,uint8_t *rx);
void stream_pilot_tick(uint32_t now);
#endif
