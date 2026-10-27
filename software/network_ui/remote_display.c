/* Direct cable remote framebuffer. Etherlite/PG160, IPv4 ARP/ICMP/UDP only.
   No private cloud/config memory is exposed; pointer requests are deduplicated. */
#include "remote_display.h"
#include "network.h"
#include <string.h>
#ifdef Z7_TINYQWEN
#include "tiny_model_transport.h"
#include "tiny_board_llm.h"
static struct tiny_model_transport llm_model_transport,llm_tokenizer_transport;
static const uint8_t llm_model_sha256[32]={0xe7,0xeb,0xb0,0xa8,0x43,0x5a,0xc9,0x01,0x73,0xef,0x9c,0xef,0xa7,0x53,0xad,0xc7,0xfe,0x2f,0x6a,0x5f,0xb8,0xfa,0x86,0x75,0x7a,0x98,0xf1,0x34,0x4d,0xcf,0x48,0x63};
static const uint8_t llm_tokenizer_sha256[32]={
    0xb6,0xaa,0x0f,0xa0,0x9e,0xc4,0x91,0x3e,0x88,0x92,0xcc,0x1f,0x25,0xc0,0xc7,0x6f,
    0x24,0xa7,0x2d,0xbd,0x30,0x74,0x13,0x17,0x82,0xbe,0xa5,0x8b,0x6a,0x94,0x4a,0xea};
int remote_llm_assets_ready(void) {
    return tmt_is_ready(&llm_model_transport) && tmt_is_ready(&llm_tokenizer_transport);
}
uint32_t remote_llm_asset_flags(void) {
    return (tmt_is_ready(&llm_model_transport)?1u:0u)|(tmt_is_ready(&llm_tokenizer_transport)?2u:0u);
}
void remote_llm_lock(int locked) {
    tmt_set_locked(&llm_model_transport,locked);tmt_set_locked(&llm_tokenizer_transport,locked);
}
/* SD boot: sd_model.c fills a window from the TF card and hashes it itself;
   readiness still needs the pinned digest held here. */
uint8_t *remote_llm_window(uint32_t index,uint32_t *bytes) {
    struct tiny_model_transport *t=index?&llm_tokenizer_transport:&llm_model_transport;
    *bytes=t->expected_bytes;return t->buffer;
}
int remote_llm_window_verified(uint32_t index,const uint8_t sha256[32]) {
    struct tiny_model_transport *t=index?&llm_tokenizer_transport:&llm_model_transport;
    if(t->active || memcmp(sha256,t->expected_sha256,32))return 0;
    t->received=t->expected_bytes;t->ready=1;return 1;
}
/* Warm CPU0 reload can retain DDR assets. Read every byte and require the
   pinned SHA again; a matching header alone never grants readiness. Each
   bounded step returns to the normal audio/UI/Ethernet service loop. */
void remote_llm_restore_tick(void) {
    static uint32_t index,offset,started;
    if(index>=2u)return;
    struct tiny_model_transport *t=index?&llm_tokenizer_transport:&llm_model_transport;
    if(t->active || t->ready || !t->configured){++index;offset=started=0;return;}
    if(!started) {
        if((!index && memcmp(t->buffer,"TQW1",4)) || (index && memcmp(t->buffer,"QWTOK01\0",8)) ||
           mbedtls_sha256_starts(&t->sha,0)){++index;offset=0;return;}
        started=1;
    }
    uint32_t n=t->expected_bytes-offset;if(n>4096u)n=4096u;
    if(mbedtls_sha256_update(&t->sha,t->buffer+offset,n)){++index;offset=started=0;return;}
    offset+=n;
    if(offset==t->expected_bytes) {
        uint8_t hash[32];
        if(!mbedtls_sha256_finish(&t->sha,hash) && !memcmp(hash,t->expected_sha256,32)) {
            t->received=t->expected_bytes;t->ready=1;
        }
        ++index;offset=started=0;
    }
}
#endif
#ifdef Z7_MMU_CACHE
#include "../ps_bringup/cache.h"
#endif
#ifndef REMOTE_READ
#define REMOTE_READ(o) (*(volatile uint32_t *)(0x40e00000u+(o)))
#define REMOTE_WRITE(o,v) (*(volatile uint32_t *)(0x40e00000u+(o))=(v))
#endif
#define PIXELS (1024u*600u)
#define MAGIC 0x3152375au /* Z7R1 */
#define PORT 4177u
#define CHUNK 1400u
#define STORE __attribute__((section(".remote_runtime")))
static uint32_t frozen[PIXELS] STORE;
static uint8_t encoded[2][PIXELS*5u] STORE;
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t remote_diag[16];
static const uint8_t mac[6]={2,0x37,0x4e,0x34,0,1},ip[4]={192,168,1,50};
static uint8_t rx[1536] __attribute__((aligned(4))),tx[1536] __attribute__((aligned(4)));
static uint8_t peer_mac[6],peer_ip[4];
static uint32_t now_ms,phy_at,phy_ready,cookie[2],lease,peer_port;
static const uint32_t *latest,*copy_source;
static uint32_t latest_gen,latest_epoch,copy_gen,copy_epoch,done_gen;
static uint32_t stage,at,encode_at,encode_bytes,run,colour,ready,ready_bytes,ready_epoch,ready_index,work_index;
static uint32_t last_sequence,last_command,last_epoch,pending,displayed;
static struct remote_pointer pointer;
/* Frozen finalized records only: fixed public ABI, never arbitrary RAM. */
#define HISTORY_STRIDE 2072u
static uint8_t history[32u+64u*HISTORY_STRIDE] STORE;
static uint32_t history_pending,history_sequence,history_id,history_bytes,history_count;
static uint16_t be16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t le32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void b16(uint8_t *p,uint32_t n) {p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void l32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8u*i));}
static uint32_t sum16(const uint8_t *p,uint32_t n) {uint32_t s=0;for(;n>=2u;n-=2u,p+=2)s+=be16(p);if(n)s+=(uint32_t)*p<<8;while(s>>16)s=(s&65535u)+(s>>16);return s;}
static int send_frame(uint32_t n) {
    for(uint32_t slot=0;slot<2u;++slot) {
        uint32_t base=slot*0x800u;
        if(REMOTE_READ(base+0x7fcu)&1u)continue;
        if(n<60u){memset(tx+n,0,60u-n);n=60u;}
        for(uint32_t i=0;i<n;i+=4u) {uint32_t word=0;for(uint32_t j=0;j<4u && i+j<n;++j)word|=(uint32_t)tx[i+j]<<(8u*j);REMOTE_WRITE(base+i,word);}
        REMOTE_WRITE(base+0x7f4u,n);REMOTE_WRITE(base+0x7fcu,1);++remote_diag[4];return 1;
    }
    ++remote_diag[5];return 0;
}
static void ethernet(const uint8_t *dst,uint16_t type) {memcpy(tx,dst,6);memcpy(tx+6,mac,6);b16(tx+12,type);}
static void ipv4(const uint8_t *dst,uint32_t bytes,uint32_t protocol) {
    uint8_t *p=tx+14;memset(p,0,20);p[0]=0x45;b16(p+2,20u+bytes);p[8]=64;p[9]=(uint8_t)protocol;memcpy(p+12,ip,4);memcpy(p+16,dst,4);b16(p+10,~sum16(p,20));
}
static void response(uint32_t op,uint32_t seq,uint32_t frame,uint32_t a,uint32_t b,const uint8_t *data,uint32_t bytes) {
    ethernet(peer_mac,0x800);ipv4(peer_ip,40u+bytes,17);uint8_t *u=tx+34;b16(u,PORT);b16(u+2,peer_port);b16(u+4,40u+bytes);b16(u+6,0);
    uint8_t *p=u+8;l32(p,MAGIC);l32(p+4,op);l32(p+8,cookie[0]);l32(p+12,cookie[1]);l32(p+16,seq);l32(p+20,frame);l32(p+24,a);l32(p+28,b);if(bytes)memcpy(p+32,data,bytes);(void)send_frame(74u+bytes);
}
static void status(uint32_t seq) {response(0x81u,seq,ready,ready_bytes,ready_epoch,0,0);}
static void handle_frame(uint32_t n) {
    if(n<42u)return;
    if(be16(rx+12)==0x806u) {
        uint8_t *a=rx+14;
        if(be16(a)!=1u || be16(a+2)!=0x800u || a[4]!=6 || a[5]!=4 || be16(a+6)!=1u || memcmp(a+24,ip,4))return;
        ethernet(rx+6,0x806);uint8_t *b=tx+14;memcpy(b,a,8);b16(b+6,2);memcpy(b+8,mac,6);memcpy(b+14,ip,4);memcpy(b+18,a+8,10);(void)send_frame(42);return;
    }
    if(be16(rx+12)!=0x800u)return;
    uint8_t *p=rx+14;uint32_t ihl=(p[0]&15u)*4u,total=be16(p+2);
    if(p[0]>>4!=4u || ihl<20u || ihl>60u || total<ihl || total>n-14u || (be16(p+6)&0x3fffu) || memcmp(p+16,ip,4) || sum16(p,ihl)!=65535u)return;
    if(p[9]==1u && total>=ihl+8u && p[ihl]==8u && sum16(p+ihl,total-ihl)==65535u) {
        ethernet(rx+6,0x800);ipv4(p+12,total-ihl,1);memcpy(tx+34,p+ihl,total-ihl);tx[34]=0;b16(tx+36,0);b16(tx+36,~sum16(tx+34,total-ihl));(void)send_frame(34u+total-ihl);return;
    }
    if(p[9]!=17u || total<ihl+8u)return;
    uint8_t *u=p+ihl;uint32_t un=be16(u+4);
    if(un<8u || un!=total-ihl)return;
#ifdef Z7_TINYQWEN
    uint32_t destination_port=be16(u+2);
    if(destination_port==4178u || destination_port==4179u) {
        uint32_t source_ip=(uint32_t)p[12]<<24|(uint32_t)p[13]<<16|(uint32_t)p[14]<<8|p[15];
        uint64_t owner=(uint64_t)be16(u)<<32|source_ip;
        uint8_t model_reply[TMT_HEADER_BYTES];
        struct tiny_model_transport *transport=destination_port==4178u?&llm_model_transport:&llm_tokenizer_transport;
        size_t count=tmt_handle(transport,owner,u+8,un-8u,model_reply);
        if(count) {
            uint32_t source_port=be16(u);
            ethernet(rx+6,0x800);ipv4(p+12,8u+(uint32_t)count,17);
            uint8_t *v=tx+34;b16(v,destination_port);b16(v+2,source_port);
            b16(v+4,8u+(uint32_t)count);b16(v+6,0);memcpy(v+8,model_reply,count);
            (void)send_frame(42u+(uint32_t)count);
        }
        return;
    }
    if(destination_port==TBL_PORT) {
        uint32_t source_ip=(uint32_t)p[12]<<24|(uint32_t)p[13]<<16|(uint32_t)p[14]<<8|p[15];
        uint64_t owner=(uint64_t)be16(u)<<32|source_ip;
        uint8_t llm_reply[1120];
        size_t count=tbl_handle_packet(owner,u+8,un-8u,llm_reply,sizeof(llm_reply));
        if(count) {
            uint32_t source_port=be16(u);
            ethernet(rx+6,0x800);ipv4(p+12,8u+(uint32_t)count,17);
            uint8_t *v=tx+34;b16(v,TBL_PORT);b16(v+2,source_port);
            b16(v+4,8u+(uint32_t)count);b16(v+6,0);memcpy(v+8,llm_reply,count);
            (void)send_frame(42u+(uint32_t)count);
        }
        return;
    }
#endif
    if(be16(u+2)!=PORT || un<40u)return;
    uint8_t *q=u+8;if(le32(q)!=MAGIC)return;
    uint32_t op=le32(q+4),lo=le32(q+8),hi=le32(q+12),seq=le32(q+16);
    if(op==1u && (!peer_port || now_ms-lease>5000u || (cookie[0]==lo && cookie[1]==hi && !memcmp(peer_ip,p+12,4)))) {
        if(!peer_port || cookie[0]!=lo || cookie[1]!=hi) {last_sequence=last_command=last_epoch=pending=0;history_pending=history_sequence=0;}
        memcpy(peer_mac,rx+6,6);memcpy(peer_ip,p+12,4);peer_port=be16(u);cookie[0]=lo;cookie[1]=hi;lease=now_ms;displayed=le32(q+20);status(seq);return;
    }
    if(!peer_port || cookie[0]!=lo || cookie[1]!=hi || memcmp(peer_ip,p+12,4) || peer_port!=be16(u) || now_ms-lease>5000u)return;
    lease=now_ms;
    if(op==2u) {
        uint32_t offset=le32(q+24);
        if(le32(q+20)!=ready || !ready || offset>=ready_bytes || offset%CHUNK){status(seq);return;}
        uint32_t size=ready_bytes-offset;if(size>CHUNK)size=CHUNK;
        response(0x82u,seq,ready,offset,ready_bytes,encoded[ready_index]+offset,size);
    } else if(op==4u && un==40u && seq) {
        if(seq==history_sequence) {
            if(!history_pending)response(0x84u,seq,history_id,history_bytes,history_count,0,0);
        } else if(!history_pending) {history_sequence=seq;history_pending=1;}
    } else if(op==5u && un==40u) {
        uint32_t offset=le32(q+24);
        if(!history_bytes || le32(q+20)!=history_id || offset>=history_bytes || offset%CHUNK)return;
        uint32_t size=history_bytes-offset;if(size>CHUNK)size=CHUNK;
        response(0x85u,seq,history_id,offset,history_bytes,history+offset,size);
    } else if(op==6u && un==40u) {
        uint8_t name[33];uint32_t connected=network_wifi_name(name),size=0;
        while(size<32u && name[size])++size;
        response(0x86u,seq,connected,size,1u,name,size);
    } else if(op==3u && un==52u && seq) {
        if(seq==last_sequence){response(0x83u,seq,last_command,last_epoch,remote_diag[8],0,0);return;}
        if(pending || (int32_t)(seq-last_sequence)<=0)return;
        uint16_t x0=(uint16_t)be16(q+32),y0=be16(q+34),x1=be16(q+36),y1=be16(q+38);
        if(x0>=1024u || x1>=1024u || y0>=600u || y1>=600u)return;
        pointer=(struct remote_pointer){seq,le32(q+40),x0,y0,x1,y1};pending=1;
    }
}
static int mdio(uint32_t phy,uint32_t reg,uint32_t write,uint32_t *value) {
    if(REMOTE_READ(0x7f0u)&1u)return 0;
    REMOTE_WRITE(0x7e4u,(phy<<5)|reg|(write?0u:0x400u));if(write)REMOTE_WRITE(0x7e8u,*value);REMOTE_WRITE(0x7f0u,9);
    for(uint32_t count=0;count<12000u;++count)if(!(REMOTE_READ(0x7f0u)&1u)){if(!write)*value=REMOTE_READ(0x7ecu)&65535u;return 1;}
    return 0;
}
void remote_display_init(uint32_t now) {
#ifdef Z7_TINYQWEN
    (void)tmt_init(&llm_model_transport,(void *)0x04000000u,401637888u,401637888u,llm_model_sha256);
    (void)tmt_init(&llm_tokenizer_transport,(void *)0x1c000000u,5277796u,5277796u,llm_tokenizer_sha256);
#endif
    memset((void *)remote_diag,0,sizeof(remote_diag));remote_diag[0]=MAGIC;now_ms=now;phy_at=now+500u;
    for(unsigned i=0;i<2;++i){REMOTE_WRITE(0x17fcu+i*0x800u,0);REMOTE_WRITE(i*0x800u+0x7fcu,0);}
    memset(tx,0,64);memcpy(tx,mac,6);
    for(unsigned i=0;i<2;++i){uint32_t word=le32(tx+4u*i);REMOTE_WRITE(i*4u,word);}
    REMOTE_WRITE(0x7f4u,6);REMOTE_WRITE(0x7fcu,3);REMOTE_WRITE(0x7f0u,8);
}
void remote_display_present(const uint32_t *pixels,uint32_t generation,uint32_t epoch) {latest=pixels;latest_gen=generation;latest_epoch=epoch;}
int remote_display_history_requested(void) {if(history_pending!=1u)return 0;history_pending=2;return 1;}
void remote_display_history_publish(const void *entries,uint32_t count,uint32_t stride,uint32_t discarded,uint32_t epoch,uint32_t final_count) {
    if(history_pending!=2u || count>64u || stride!=HISTORY_STRIDE)return;
    l32(history,0x31545348u);l32(history+4,count);l32(history+8,stride);l32(history+12,discarded);
    l32(history+16,epoch);l32(history+20,final_count);l32(history+24,0);l32(history+28,0);
    memcpy(history+32,entries,count*stride);history_bytes=32u+count*stride;history_count=count;
    ++history_id;if(!history_id)++history_id;history_pending=0;
    response(0x84u,history_sequence,history_id,history_bytes,history_count,0,0);
}
int remote_display_input(struct remote_pointer *event) {if(pending!=1u)return 0;*event=pointer;pending=2;return 1;}
void remote_display_ack(uint32_t command,uint32_t epoch) {if(!pending)return;last_sequence=pointer.sequence;last_command=command;last_epoch=epoch;pending=0;++remote_diag[8];remote_diag[9]=command;response(0x83u,last_sequence,command,epoch,remote_diag[8],0,0);}
static void emit_run(void) {
    uint8_t *p=encoded[work_index]+encode_bytes;p[0]=(uint8_t)run;p[1]=(uint8_t)(run>>8);p[2]=(uint8_t)(colour>>16);p[3]=(uint8_t)(colour>>8);p[4]=(uint8_t)colour;encode_bytes+=5u;
}
void remote_display_tick(uint32_t now) {
    now_ms=now;
    if((int32_t)(now-phy_at)>=0) {
        phy_at=now+1000u;uint32_t value=0;
        if(!phy_ready) {
            if(!mdio(1,2,0,&value) || value!=0x1cu){remote_diag[1]=1;return;}remote_diag[2]=value;
            if(!mdio(1,3,0,&value)){remote_diag[1]=2;return;}remote_diag[3]=value;
            value=0;int ok=mdio(1,31,1,&value);value=0;ok=ok&&mdio(1,9,1,&value);value=0x101;ok=ok&&mdio(1,4,1,&value);value=0x2000;ok=ok&&mdio(8,16,1,&value);value=0x1200;ok=ok&&mdio(1,0,1,&value);
            if(!ok){remote_diag[1]=3;return;}phy_ready=1;remote_diag[1]=4;
        }
        if(mdio(1,1,0,&value) && mdio(1,1,0,&value))remote_diag[10]=value;
        if(mdio(1,26,0,&value))remote_diag[11]=value;
#ifdef Z7_MMU_CACHE
        cache_clean_range((const void *)remote_diag,sizeof(remote_diag));
#endif
    }
    for(uint32_t slot=0;slot<2u;++slot) {
        uint32_t base=0x1000u+slot*0x800u;if(!(REMOTE_READ(base+0x7fcu)&1u))continue;
        for(uint32_t i=0;i<64u;i+=4u){uint32_t v=REMOTE_READ(base+i);for(unsigned j=0;j<4;++j)rx[i+j]=(uint8_t)(v>>(j*8u));}
        uint32_t length=be16(rx+12)==0x800u?14u+be16(rx+16):42u;
        if(length<=1514u)for(uint32_t i=64u;i<length;i+=4u){uint32_t v=REMOTE_READ(base+i);for(unsigned j=0;j<4;++j)rx[i+j]=(uint8_t)(v>>(j*8u));}
        REMOTE_WRITE(base+0x7fcu,0);++remote_diag[6];if(length<=1514u)handle_frame(length);
    }
    if(!peer_port || now-lease>5000u)return;
    if(!stage && (!ready || displayed==ready) && latest && latest_gen!=done_gen) {copy_source=latest;copy_gen=latest_gen;copy_epoch=latest_epoch;at=0;stage=1;}
    if(stage==1u) {
        uint32_t count=PIXELS-at;if(count>8192u)count=8192u;memcpy(frozen+at,copy_source+at,count*4u);at+=count;
        if(at==PIXELS){stage=2;encode_at=encode_bytes=run=0;work_index=ready_index^1u;}
    } else if(stage==2u) {
        uint32_t end=encode_at+4096u;if(end>PIXELS)end=PIXELS;
        while(encode_at<end) {uint32_t c=frozen[encode_at++]&0xffffffu;if(run && (c!=colour || run==65535u)){emit_run();run=0;}colour=c;++run;}
        if(encode_at==PIXELS){if(run)emit_run();ready_index=work_index;ready_bytes=encode_bytes;ready_epoch=copy_epoch;++ready;done_gen=copy_gen;stage=0;++remote_diag[7];}
    }
    remote_diag[12]=ready;remote_diag[13]=ready_bytes;remote_diag[14]=stage;remote_diag[15]=now;
}
