/* Host test of software/network_ui/sd_model.c against FAT32 images built here:
   MBR and whole-card volumes, a two-cluster root directory, a fragmented model
   longer than one DMA run, a partial final block, and the failure states. The
   SD controller is replaced by synchronous copies from the image. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "../software/network_ui/sd_model.h"
#include "../software/network_ui/glossary.h"
#include "../software/subtitle_ui/ui_model.h"

void sd_model_reset(void);
extern volatile uint32_t sd_model_diag[10];

#define IMAGE_SECTORS 131072u   /* 64 MiB */
#define EOC 0x0fffffffu
static uint8_t *image;
static uint32_t part,spc,fat_lba,fat_size,data_lba,root[2],roots,entries;
static uint32_t commands,fail_at,pending,ticks,locked;
static uint8_t *window[2],*source[2],digest[2][32];
static uint32_t bytes[2],verified;

int card_start(uint32_t lba,uint32_t blocks,uint8_t *dest) {
    assert(!pending && blocks);
    if(++commands==fail_at) {pending=2;return 1;}
    assert(lba+blocks<=IMAGE_SECTORS);
    for(int i=0;i<2;++i) if(dest>=window[i] && dest<window[i]+bytes[i])
        assert(dest+blocks*512u<=window[i]+bytes[i]+511u);   /* only the final partial block may spill */
    memcpy(dest,image+(size_t)lba*512u,(size_t)blocks*512u);pending=1;return 1;
}
int card_poll(void) {int done=pending==1?1:-1;assert(pending);pending=0;return done;}
void card_abort(void) {}
uint32_t card_ticks(void) {return ++ticks;}
int sd_boot_settled(void) {return 1;}
void remote_llm_lock(int on) {locked=(uint32_t)on;}
uint8_t *remote_llm_window(uint32_t index,uint32_t *n) {*n=bytes[index];return window[index];}
int remote_llm_window_verified(uint32_t index,const uint8_t sha[32]) {
    if(memcmp(sha,digest[index],32)) return 0;
    verified|=1u<<index;return 1;
}

static void put16(uint8_t *p,uint32_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v) {put16(p,v);put16(p+2,v>>16);}
static uint8_t *cluster_at(uint32_t c) {return image+(size_t)(data_lba+(c-2u)*spc)*512u;}
static void fat_set(uint32_t c,uint32_t v) {for(uint32_t f=0;f<2u;++f) put32(image+(size_t)(fat_lba+f*fat_size)*512u+c*4u,v);}

static void format(int mbr,uint32_t sectors_per_cluster) {
    memset(image,0,(size_t)IMAGE_SECTORS*512u);
    part=mbr?2048u:0u;spc=sectors_per_cluster;
    uint32_t total=IMAGE_SECTORS-part,reserved=32u;
    fat_size=((total/spc+2u)*4u+511u)/512u;fat_lba=part+reserved;data_lba=fat_lba+2u*fat_size;
    uint8_t *b=image+(size_t)part*512u;
    b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSWIN4.1",8);put16(b+11,512);b[13]=(uint8_t)spc;
    put16(b+14,reserved);b[16]=2;put32(b+32,total);put32(b+36,fat_size);put32(b+44,2);
    memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    if(mbr) {uint8_t *e=image+446;e[4]=0x0c;put32(e+8,part);put32(e+12,total);image[510]=0x55;image[511]=0xaa;}
    fat_set(0,0x0ffffff8u);fat_set(1,EOC);fat_set(2,EOC);root[0]=2;roots=1;entries=0;
}
static void entry(const char *name,uint8_t attr,uint32_t first,uint32_t size) {
    uint32_t per=spc*16u,c=entries/per;
    if(c==roots) {root[roots]=root[roots-1u]+1u;fat_set(root[roots-1u],root[roots]);fat_set(root[roots],EOC);++roots;}
    uint8_t *e=cluster_at(root[c])+(entries%per)*32u;
    memcpy(e,name,11);e[11]=attr;put16(e+20,first>>16);put16(e+26,first);put32(e+28,size);++entries;
}
/* extents: {first cluster, count}; data copied cluster by cluster. */
static void file(const char *name,const uint8_t *data,uint32_t size,const uint32_t extents[][2],uint32_t n) {
    uint32_t cb=spc*512u,offset=0,previous=0;
    for(uint32_t i=0;i<n;++i) for(uint32_t k=0;k<extents[i][1];++k) {
        uint32_t c=extents[i][0]+k,chunk=size-offset<cb?size-offset:cb;
        assert(offset<size);memcpy(cluster_at(c),data+offset,chunk);offset+=chunk;
        if(previous) fat_set(previous,c);
        previous=c;
    }
    assert(offset==size);fat_set(previous,EOC);
    entry(name,0x20,extents[0][0],size);
}
static void assets(uint32_t tokenizer,uint32_t model) {
    for(int i=0;i<2;++i) {free(window[i]);free(source[i]);}
    bytes[0]=model;bytes[1]=tokenizer;
    for(int i=0;i<2;++i) {
        source[i]=malloc(bytes[i]);window[i]=malloc(bytes[i]+512u);assert(source[i] && window[i]);
        uint32_t x=0x9e3779b9u*(uint32_t)(i+1);
        for(uint32_t k=0;k<bytes[i];++k) {x^=x<<13;x^=x>>17;x^=x<<5;source[i][k]=(uint8_t)x;}
        memset(window[i],0,bytes[i]+512u);
        mbedtls_sha256_context c;mbedtls_sha256_init(&c);(void)mbedtls_sha256_starts(&c,0);
        (void)mbedtls_sha256_update(&c,source[i],bytes[i]);(void)mbedtls_sha256_finish(&c,digest[i]);
    }
}
static uint32_t run(void) {
    sd_model_reset();commands=pending=verified=locked=0;
    uint32_t last=0;
    for(uint32_t now=0;now<1000000u;++now) {
        sd_model_tick(now,(int)(now&1u));
        uint32_t percent,state=sd_model_status(&percent);
        if(state==UI_MODEL_LOADING) {assert(locked && percent>=last && percent<=100u);last=percent;}
        if(state>=UI_MODEL_READY) {assert(!locked);return state;}
    }
    assert(0);return 0;
}
static void loaded(void) {
    assert(verified==3u);
    for(int i=0;i<2;++i) assert(!memcmp(window[i],source[i],bytes[i]));
    uint32_t percent;assert(sd_model_status(&percent)==UI_MODEL_READY && percent==100u);
    assert(sd_model_diag[5]==bytes[0]+bytes[1] && sd_model_diag[6]==bytes[0]+bytes[1]);
}

int main(void) {
    image=malloc((size_t)IMAGE_SECTORS*512u);assert(image);
    /* MBR, 4 KiB clusters; root directory continues into a second cluster;
       the model is three extents, the first longer than one 4 MiB DMA run. */
    static const uint32_t tok[][2]={{10,49}},model[][2]={{100,1200},{2000,200},{1500,136}};
    assets(200003u,6291456u);format(1,8);
    entry("Z7SD       ",0x08,0,0);
    entry("A       TXT",0x0f,0,0);
    for(uint32_t i=2;i<128u;++i) {char n[12];snprintf(n,sizeof(n),"FILE%04uTXT",(unsigned)i);entry(n,0x20,0,0);}
    entry("\xe5WEN    TQW",0x20,3,6291456u);
    file("QWEN    TOK",source[1],bytes[1],tok,1);
    file("QWEN    TQW",source[0],bytes[0],model,3);
    entry("QWEN    TQW",0x10,5,0);
    static const char words[]="\xef\xbb\xbf# terms\n\xe4\xbc\x9a\xe8\xae\xae = meeting\r\nbad line\n\xe6\xb5\x8b\xe8\xaf\x95=test\n";
    static const uint32_t glossary_extent[][2]={{60,1}};
    file("GLOSSARYTXT",(const uint8_t *)words,sizeof(words)-1u,glossary_extent,1);
    assert(roots==2u);
    assert(run()==UI_MODEL_READY);loaded();
    assert(glossary_terms()==2u && glossary_skipped()==1u);
    uint32_t data_commands=sd_model_diag[7];
    assert(data_commands==5u);   /* tokenizer 1, model 1024+176, 200, 136 clusters */
    /* Broken chain: a free FAT entry in the middle of the model. */
    fat_set(2000u+99u,0);assert(run()==UI_MODEL_MISSING);fat_set(2000u+99u,2000u+100u);
    /* One changed byte. */
    cluster_at(1500)[7]^=1u;assert(run()==UI_MODEL_BAD_DIGEST);cluster_at(1500)[7]^=1u;
    /* A late read reports an SD error. */
    fail_at=0;assert(run()==UI_MODEL_READY);uint32_t all=commands;
    fail_at=all-1u;assert(run()==UI_MODEL_READ_ERROR);fail_at=0;
    /* Whole-card volume, 32 KiB clusters, model not a whole number of clusters. */
    static const uint32_t tok2[][2]={{3,1}},model2[][2]={{40,153}};
    assets(5000u,5000000u);format(0,64);
    file("QWEN    TOK",source[1],bytes[1],tok2,1);
    file("QWEN    TQW",source[0],bytes[0],model2,1);
    assert(run()==UI_MODEL_READY);loaded();
    /* Missing and wrong-sized files. */
    format(0,64);file("QWEN    TOK",source[1],bytes[1],tok2,1);
    assert(run()==UI_MODEL_MISSING);
    file("QWEN    TQW",source[0],bytes[0]-1u,model2,1);
    assert(run()==UI_MODEL_MISSING);
    printf("PASS_SD_MODEL mbr_fragmented=%u commands=%u whole_card missing size digest chain read_error\n",
           (unsigned)data_commands,(unsigned)all);
    return 0;
}
