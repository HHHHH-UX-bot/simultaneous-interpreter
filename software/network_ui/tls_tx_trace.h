#ifndef TLS_TX_TRACE_H
#define TLS_TX_TRACE_H
#include <stdint.h>
#include <stddef.h>

/* Diagnostic-only rolling fingerprints; payload bytes are never retained. */
static inline void tls_tx_trace_begin(volatile uint32_t *trace,uint32_t session) {
    for(uint32_t i=0;i<260u;++i) trace[i]=0;
    trace[0]=0x31545854u;trace[1]=session;trace[3]=2166136261u;
}
static inline void tls_tx_trace_add(volatile uint32_t *trace,const uint8_t *p,size_t bytes) {
    uint32_t count=trace[2],hash=trace[3];
    for(size_t i=0;i<bytes;++i) {
        hash=(hash^p[i])*16777619u;++count;
        if(!(count&255u)) {
            uint32_t block=count/256u,at=4u+2u*((block-1u)%128u);
            trace[at]=block;trace[at+1u]=hash;hash=2166136261u;
        }
    }
    trace[2]=count;trace[3]=hash;
}
#endif
