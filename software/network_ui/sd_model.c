/* SD boot: the board Qwen assets are files on the TF card (qwen.tok and
   qwen.tqw) rather than BOOT.BIN partitions, so the FSBL hands over within
   seconds. Once startup has settled this reads them from the FAT32 root
   straight into their fixed DDR windows with the SD controller's ADMA2,
   hashes what has arrived in bounded slices of the service loop, and passes
   each digest to the pinned check in remote_display.c. The FSBL left the card
   selected in 4-bit high-speed transfer state; nothing here re-initialises it. */
#include <stdint.h>
#include <string.h>
#include "sd_model.h"
#include "remote_display.h"
#include "sd_boot_runtime.h"
#include "mbedtls/sha256.h"
#include "glossary.h"
#include "../subtitle_ui/ui_model.h"

#define RUN_BYTES (4u*1024u*1024u)   /* one CMD18: 8192 blocks, 64 ADMA2 lines of 64 KiB */

/* magic, state, SD interrupt status on error, start ms, ready ms, DMA bytes,
   verified bytes, read commands, microseconds spent hashing, hashing slices */
volatile uint32_t sd_model_diag[10];

#ifdef SD_MODEL_HOST
/* tests/sd_model_test.c serves the reads from a disk image. */
int card_start(uint32_t lba,uint32_t blocks,uint8_t *dest);
int card_poll(void);
void card_abort(void);
uint32_t card_ticks(void);
#define TICKS_PER_MS 1000u
#define RUNTIME
static void card_setup(void) {}
static void invalidate(const uint8_t *p,uint32_t bytes) {(void)p;(void)bytes;}
static void publish(void) {}
#else
#include "framebuffer_hw.h"
#include "../ps_bringup/cache.h"
#define TICKS_PER_MS TIMER_TICKS_PER_MS
#define RUNTIME __attribute__((section(".sd_runtime"),aligned(32)))
#define SD0 0xe0100000u
struct adma2 {uint16_t attribute,length;uint32_t address;};
static struct adma2 table[RUN_BYTES/65536u] RUNTIME;
static uint32_t reg(uint32_t offset) {return *(volatile uint32_t *)(SD0+offset);}
static void set(uint32_t offset,uint32_t value) {*(volatile uint32_t *)(SD0+offset)=value;}
static uint32_t card_ticks(void) {return *(volatile uint32_t *)0xf8f00200u;}
static void card_setup(void) {
    volatile uint8_t *host=(volatile uint8_t *)(SD0+0x28u);
    *host=(uint8_t)((*host&~0x18u)|0x10u);   /* ADMA2 with 32-bit descriptors */
    set(0x34,0xf3ffffffu);                     /* latch every normal and error status */
}
/* DMA into dest; 0 if the CMD/DAT lines stay busy. */
static int card_start(uint32_t lba,uint32_t blocks,uint8_t *dest) {
    uint32_t start=card_ticks();
    while(reg(0x24)&3u) if(card_ticks()-start>10u*TICKS_PER_MS) return 0;
    uint32_t bytes=blocks*512u,lines=(bytes+65535u)/65536u;
    for(uint32_t i=0;i<lines;++i) {
        uint32_t n=bytes-i*65536u;
        table[i].attribute=(uint16_t)(0x21u|(i+1u==lines?2u:0u));   /* valid, transfer, end */
        table[i].length=(uint16_t)(n>=65536u?0u:n);                   /* 0 means 64 KiB */
        table[i].address=(uint32_t)(uintptr_t)dest+i*65536u;
    }
    cache_clean_range(table,lines*sizeof(table[0]));
    set(0x58,(uint32_t)(uintptr_t)table);
    set(0x04,blocks<<16|512u);
    set(0x08,lba);
    set(0x30,0xffffffffu);
    /* CMD18 + Auto CMD12 or CMD17, R1 with CRC/index checks, DMA read. */
    set(0x0c,blocks>1u?0x123a0037u:0x113a0013u);
    return 1;
}
/* 1 complete, 0 running, -1 error. */
static int card_poll(void) {
    uint32_t status=reg(0x30);
    if(status&0x8000u) {sd_model_diag[2]=status;set(0x30,0xffffffffu);return -1;}
    if(!(status&2u)) return 0;
    set(0x30,0xffffffffu);return 1;
}
static void card_abort(void) {*(volatile uint8_t *)(SD0+0x2fu)=6u;}   /* reset CMD and DAT lines */
/* L2 is off (start.S), so DCIMVAC alone drops lines the DMA has replaced. */
static void invalidate(const uint8_t *p,uint32_t bytes) {
    for(uintptr_t a=(uintptr_t)p&~(uintptr_t)31u;a<(uintptr_t)p+bytes;a+=32u)
        __asm__ volatile("mcr p15,0,%0,c7,c6,1"::"r"(a):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static void publish(void) {cache_clean_range((const void *)sd_model_diag,sizeof(sd_model_diag));}
#endif

static uint8_t sector[512] RUNTIME;
static uint8_t glossary_file[GLOSSARY_FILE_BYTES] RUNTIME;
/* Load order: the tokenizer (window 1), then the model (window 0). */
static const char names[2][12]={"QWEN    TOK","QWEN    TQW"};
static const uint32_t windows[2]={1u,0u};

static struct {
    uint32_t state,io_error;
    uint32_t fat,data,spc,clusters,cached;   /* clusters: first invalid cluster number */
    uint32_t first[2],size[2];
    uint32_t slot,cluster,loaded,hashed,flight,flight_ms,done;
    uint8_t *window;
    mbedtls_sha256_context sha;
} sd;

static uint32_t le16(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8;}
static uint32_t le32(const uint8_t *p) {return le16(p)|le16(p+2)<<16;}
static uint32_t cluster_lba(uint32_t cluster) {return sd.data+(cluster-2u)*sd.spc;}

static int read_sector(uint32_t lba) {
    uint32_t start=card_ticks();int done=0;
    sd.cached=~0u;
    if(card_start(lba,1u,sector))
        while(!(done=card_poll()) && card_ticks()-start<100u*TICKS_PER_MS) {}
    if(done<=0) {card_abort();sd.io_error=1;return 0;}
    invalidate(sector,512u);return 1;
}
/* FAT entry of cluster; 0 with io_error set when the sector is unreadable. */
static uint32_t fat_next(uint32_t cluster) {
    uint32_t lba=sd.fat+cluster/128u;
    if(lba!=sd.cached && !read_sector(lba)) return 0;
    sd.cached=lba;
    return le32(sector+cluster%128u*4u)&0x0fffffffu;
}

/* FAT32 on an MBR partition or the whole card, then both root entries. */
static uint32_t mount(void) {
    uint32_t part=0;
    if(!read_sector(0)) return UI_MODEL_READ_ERROR;
    if(memcmp(sector+82,"FAT32   ",8)) {
        for(uint32_t i=0;i<4u && !part;++i) {
            const uint8_t *entry=sector+446u+16u*i;
            if(entry[4]==0x0bu || entry[4]==0x0cu) part=le32(entry+8);
        }
        if(!part || le16(sector+510)!=0xaa55u) return UI_MODEL_MISSING;
        if(!read_sector(part)) return UI_MODEL_READ_ERROR;
    }
    uint32_t spc=sector[13],reserved=le16(sector+14),fats=sector[16],total=le32(sector+32);
    uint32_t fat_size=le32(sector+36),root=le32(sector+44);
    if(le16(sector+510)!=0xaa55u || memcmp(sector+82,"FAT32   ",8) || le16(sector+11)!=512u ||
       !spc || (spc&(spc-1u)) || !reserved || !fats || fats>2u || !fat_size || fat_size>(1u<<24) ||
       total<=reserved+fats*fat_size) return UI_MODEL_MISSING;
    sd.spc=spc;sd.fat=part+reserved;sd.data=sd.fat+fats*fat_size;
    sd.clusters=2u+(total-reserved-fats*fat_size)/spc;
    if(sd.clusters>fat_size*128u) sd.clusters=fat_size*128u;
    uint32_t found=0,glossary_first=0,glossary_size=0;
    for(uint32_t cluster=root,hops=0;cluster>=2u && cluster<sd.clusters && hops<sd.clusters;++hops) {
        for(uint32_t s=0;s<spc;++s) {
            if(!read_sector(cluster_lba(cluster)+s)) return UI_MODEL_READ_ERROR;
            for(const uint8_t *e=sector;e<sector+512;e+=32) {
                if(!e[0]) goto listed;
                /* deleted, long-name part, volume label or directory */
                if(e[0]==0xe5u || (e[11]&0x18u)) continue;
                for(uint32_t i=0;i<2u;++i) if(!memcmp(e,names[i],11)) {
                    sd.first[i]=le16(e+20)<<16|le16(e+26);sd.size[i]=le32(e+28);found|=1u<<i;
                }
                if(!memcmp(e,"GLOSSARYTXT",11)) {glossary_first=le16(e+20)<<16|le16(e+26);glossary_size=le32(e+28);}
            }
        }
        cluster=fat_next(cluster);
        if(sd.io_error) return UI_MODEL_READ_ERROR;
    }
listed:
    /* Optional glossary.txt, small enough to read here before the session. */
    if(glossary_size && glossary_size<=GLOSSARY_FILE_BYTES) {
        uint32_t got=0;
        for(uint32_t cluster=glossary_first;got<glossary_size;) {
            if(cluster<2u || cluster>=sd.clusters) break;   /* a broken glossary is skipped, not fatal */
            for(uint32_t s=0;s<spc && got<glossary_size;++s) {
                if(!read_sector(cluster_lba(cluster)+s)) return UI_MODEL_READ_ERROR;
                uint32_t n=glossary_size-got>512u?512u:glossary_size-got;
                memcpy(glossary_file+got,sector,n);got+=n;
            }
            if(got<glossary_size) {cluster=fat_next(cluster);if(sd.io_error) return UI_MODEL_READ_ERROR;}
        }
        if(got==glossary_size) (void)glossary_load(glossary_file,glossary_size);
    }
    for(uint32_t i=0;i<2u;++i) {
        uint32_t bytes;(void)remote_llm_window(windows[i],&bytes);
        if(!(found&(1u<<i)) || sd.size[i]!=bytes || sd.first[i]<2u || sd.first[i]>=sd.clusters) return UI_MODEL_MISSING;
    }
    return UI_MODEL_LOADING;
}

static void begin(uint32_t slot) {
    uint32_t bytes;
    sd.slot=slot;sd.window=remote_llm_window(windows[slot],&bytes);
    sd.cluster=sd.first[slot];sd.loaded=sd.hashed=0;
    mbedtls_sha256_init(&sd.sha);(void)mbedtls_sha256_starts(&sd.sha,0);
}

/* DMA the longest contiguous cluster run (at most RUN_BYTES) from the loaded
   position. The final partial block lands in the unused gap after the window.
   0 if the chain is broken or the card does not answer. */
static int next_run(uint32_t now) {
    uint32_t cluster_bytes=sd.spc*512u,left=sd.size[sd.slot]-sd.loaded;
    uint32_t clusters_left=(left+cluster_bytes-1u)/cluster_bytes,limit=RUN_BYTES/cluster_bytes,k=1,next=0;
    if(sd.cluster<2u || sd.cluster>=sd.clusters) return 0;
    for(;;) {
        if(k==clusters_left) break;
        next=fat_next(sd.cluster+k-1u);
        if(next<2u || next>=sd.clusters) return 0;
        if(next!=sd.cluster+k || k==limit) break;
        ++k;next=0;
    }
    uint32_t bytes=k*cluster_bytes;if(bytes>left) bytes=left;
    if(!card_start(cluster_lba(sd.cluster),(bytes+511u)/512u,sd.window+sd.loaded)) {sd.io_error=1;return 0;}
    sd.flight=bytes;sd.flight_ms=now;sd.cluster=next;++sd_model_diag[7];
    return 1;
}

/* 2 KiB pieces keep the overshoot past the budget small. */
static void hash(uint32_t budget) {
    uint32_t start=card_ticks();
    if(sd.hashed>=sd.loaded) return;
    while(sd.hashed<sd.loaded && card_ticks()-start<budget) {
        uint32_t n=sd.loaded-sd.hashed;if(n>2048u) n=2048u;
        (void)mbedtls_sha256_update(&sd.sha,sd.window+sd.hashed,n);sd.hashed+=n;
    }
    sd_model_diag[8]+=(card_ticks()-start)*1000u/TICKS_PER_MS;++sd_model_diag[9];
}

static void stop(uint32_t state) {sd.state=state;sd_model_diag[1]=state;remote_llm_lock(0);publish();}

void sd_model_tick(uint32_t now,int busy) {
    if(sd.state==UI_MODEL_IDLE) {sd.state=UI_MODEL_WAITING;sd_model_diag[0]=0x314d4453u;sd_model_diag[1]=sd.state;}
    if(sd.state==UI_MODEL_WAITING) {
        if(!sd_boot_settled()) return;
        /* The PC upload path stays out of both windows until this finishes. */
        sd_model_diag[3]=now;remote_llm_lock(1);card_setup();
        uint32_t state=mount();
        if(state!=UI_MODEL_LOADING) {stop(state);return;}
        sd.state=sd_model_diag[1]=state;begin(0);publish();return;
    }
    if(sd.state!=UI_MODEL_LOADING) return;
    if(sd.flight) {
        int done=card_poll();
        if(done<0 || (!done && now-sd.flight_ms>5000u)) {card_abort();stop(UI_MODEL_READ_ERROR);return;}
        if(done) {
            invalidate(sd.window+sd.loaded,sd.flight);
            sd.loaded+=sd.flight;sd_model_diag[5]+=sd.flight;sd.flight=0;publish();
        }
    }
    if(!sd.flight && sd.loaded<sd.size[sd.slot] && !next_run(now)) {
        stop(sd.io_error?UI_MODEL_READ_ERROR:UI_MODEL_MISSING);return;
    }
    /* The cloud session starts by itself after boot, so the busy slice sets the
       load time: 150 us measured 3.3 MB/s (123 s) with recognition unaffected. */
    hash((busy?8u:12u)*(TICKS_PER_MS/20u));   /* 400 us while a session runs, else 600 us */
    sd_model_diag[6]=sd.done+sd.hashed;
    if(sd.hashed<sd.size[sd.slot]) return;
    uint8_t digest[32];(void)mbedtls_sha256_finish(&sd.sha,digest);
    if(!remote_llm_window_verified(windows[sd.slot],digest)) {stop(UI_MODEL_BAD_DIGEST);return;}
    sd.done+=sd.size[sd.slot];
    if(!sd.slot) {begin(1u);publish();return;}
    sd_model_diag[4]=now;stop(UI_MODEL_READY);
}

uint32_t sd_model_status(uint32_t *percent) {
    uint32_t total=sd.size[0]+sd.size[1];
    *percent=sd.state==UI_MODEL_READY?100u:(total<100u?0u:(sd.done+sd.hashed)/(total/100u));
    if(*percent>100u) *percent=100u;
    return sd.state;
}

#ifdef SD_MODEL_HOST
void sd_model_reset(void) {memset(&sd,0,sizeof(sd));memset((void *)sd_model_diag,0,sizeof(sd_model_diag));}
#endif
