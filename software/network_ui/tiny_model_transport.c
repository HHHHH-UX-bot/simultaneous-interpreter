#include "tiny_model_transport.h"
#include <string.h>

static uint32_t crc_table[256];
static int crc_initialized;
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void wr32(uint8_t *p, uint32_t v) {
    unsigned i;for(i=0;i<4;++i)p[i]=(uint8_t)(v>>(8u*i));
}
static void crc_init(void) {
    uint32_t i,j,v;if(crc_initialized)return;
    for(i=0;i<256;++i){v=i;for(j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);crc_table[i]=v;}
    crc_initialized=1;
}
static uint32_t crc_update(uint32_t crc,const uint8_t *p,size_t n) {
    while(n--)crc=crc_table[(crc^*p++)&255u]^(crc>>8);
    return crc;
}
uint32_t tmt_crc32(const void *data,size_t bytes) {
    crc_init();return ~crc_update(0xffffffffu,(const uint8_t *)data,bytes);
}
static uint32_t packet_crc(const uint8_t *p,size_t n) {
    uint32_t crc=crc_update(0xffffffffu,p,28);
    if(n>32)crc=crc_update(crc,p+32,n-32);
    return ~crc;
}
static size_t ack(const struct tiny_model_transport *t,const uint8_t *p,
                   uint32_t code,uint8_t *out) {
    wr32(out,TMT_MAGIC);wr32(out+4,rd32(p+4)|0x80u);
    memcpy(out+8,p+8,12);wr32(out+20,t->received);wr32(out+24,code);
    wr32(out+28,packet_crc(out,32));return 32;
}
int tmt_init(struct tiny_model_transport *t,void *buffer,uint32_t capacity,
             uint32_t expected_bytes,const uint8_t expected_sha256[32]) {
    if(!t)return 0;
    memset(t,0,sizeof(*t));crc_init();mbedtls_sha256_init(&t->sha);
    if(!buffer || !expected_sha256 || !expected_bytes || expected_bytes>capacity)return 0;
    t->buffer=(uint8_t *)buffer;t->capacity=capacity;t->expected_bytes=expected_bytes;
    memcpy(t->expected_sha256,expected_sha256,32);t->configured=1;return 1;
}
void tmt_set_locked(struct tiny_model_transport *t,int locked) {if(t)t->locked=(uint8_t)(locked!=0);}
int tmt_is_ready(const struct tiny_model_transport *t) {return t && t->configured && t->ready;}
size_t tmt_handle(struct tiny_model_transport *t,uint64_t owner,
                  const uint8_t *p,size_t n,uint8_t out[32]) {
    uint32_t op,lo,hi,offset,count,code= TMT_OK;
    uint8_t digest[32];
    if(!t || !p || !out || n<32 || rd32(p)!=TMT_MAGIC)return 0;
    if(n>32u+TMT_CHUNK_BYTES)return ack(t,p,TMT_BAD_FORMAT,out);
    if(packet_crc(p,n)!=rd32(p+28))return ack(t,p,TMT_BAD_CRC,out);
    op=rd32(p+4);lo=rd32(p+8);hi=rd32(p+12);offset=rd32(p+20);count=rd32(p+24);
    if(!t->configured)return ack(t,p,TMT_WRONG_MODEL,out);
    if(op==TMT_STATUS) {
        if(n!=32 || count)return ack(t,p,TMT_BAD_FORMAT,out);
        return ack(t,p,t->ready?TMT_READY:TMT_OK,out);
    }
    if(t->locked)return ack(t,p,TMT_BUSY,out);
    if(op==TMT_BEGIN) {
        if(n!=64 || count!=32 || !(lo|hi))return ack(t,p,TMT_BAD_FORMAT,out);
        if(offset!=t->expected_bytes || memcmp(p+32,t->expected_sha256,32))
            return ack(t,p,TMT_WRONG_MODEL,out);
        if(t->active || t->ready) {
            if(owner!=t->owner || lo!=t->session_lo || hi!=t->session_hi)
                return ack(t,p,TMT_BUSY,out);
            return ack(t,p,t->ready?TMT_READY:TMT_OK,out);
        }
        t->owner=owner;t->session_lo=lo;t->session_hi=hi;t->received=0;t->ready=0;
        if(mbedtls_sha256_starts(&t->sha,0))return ack(t,p,TMT_HASH_ERROR,out);
        t->active=1;return ack(t,p,TMT_OK,out);
    }
    if(owner!=t->owner || lo!=t->session_lo || hi!=t->session_hi || (!t->active && !t->ready))
        return ack(t,p,TMT_BAD_SESSION,out);
    if(op==TMT_ABORT) {
        if(n!=32 || count)return ack(t,p,TMT_BAD_FORMAT,out);
        t->active=0;t->ready=0;t->received=0;return ack(t,p,TMT_OK,out);
    }
    if(op==TMT_DATA) {
        if(!count || count>TMT_CHUNK_BYTES || n!=32u+count ||
           offset>t->expected_bytes || count>t->expected_bytes-offset)
            return ack(t,p,TMT_BAD_FORMAT,out);
        if(offset<t->received) {
            /* A duplicate is acknowledged only if it equals committed bytes. */
            code=(count<=t->received-offset && !memcmp(t->buffer+offset,p+32,count))
                ?(t->ready?TMT_READY:TMT_OK):TMT_OFFSET;
            return ack(t,p,code,out);
        }
        if(offset!=t->received || !t->active)return ack(t,p,TMT_OFFSET,out);
        if(mbedtls_sha256_update(&t->sha,p+32,count)) {
            t->active=0;t->ready=0;return ack(t,p,TMT_HASH_ERROR,out);
        }
        memcpy(t->buffer+offset,p+32,count);t->received+=count;
        return ack(t,p,TMT_OK,out);
    }
    if(op==TMT_FINISH) {
        if(n!=32 || count || offset!=t->expected_bytes)return ack(t,p,TMT_BAD_FORMAT,out);
        if(t->ready)return ack(t,p,TMT_READY,out);
        if(t->received!=t->expected_bytes)return ack(t,p,TMT_NOT_COMPLETE,out);
        if(mbedtls_sha256_finish(&t->sha,digest))code=TMT_HASH_ERROR;
        else if(memcmp(digest,t->expected_sha256,32))code=TMT_BAD_DIGEST;
        else {t->ready=1;code=TMT_READY;}
        t->active=0;return ack(t,p,code,out);
    }
    return ack(t,p,TMT_BAD_FORMAT,out);
}
