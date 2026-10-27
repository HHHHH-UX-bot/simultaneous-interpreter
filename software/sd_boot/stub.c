/* SD-boot relocation stub: place the CPU0 and CPU1 ELF segments exactly as
   xsdb dow does (file bytes copied, remainder zeroed), publish the CPU1 entry
   for the firmware, then enter the CPU0 image. Runs uncached from DDR. */
#include <stdint.h>
#include "sd_boot.h"

void stub_enter(uint32_t entry);   /* stub.S: caches back off, then jump */
static uint32_t timer(void) {return *(volatile uint32_t *)0xf8f00200u;}
/* Data reads are uncached here, so read a word at a time; the bitwise loop
   itself runs from the I-cache. p is word aligned (payload base + header). */
static uint32_t crc32(const uint8_t *p,uint32_t n) {
    uint32_t crc=0xffffffffu,i=0;
    for(;i+4u<=n;i+=4u) {
        crc^=*(const uint32_t *)(p+i);
        for(uint32_t k=0;k<32;++k) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    for(;i<n;++i) {crc^=p[i];for(uint32_t k=0;k<8;++k) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return ~crc;
}
static int overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {return a<b+m && b<a+n;}

void stub_main(void) {
    volatile struct sd_boot_info *info=(volatile struct sd_boot_info *)SD_BOOT_INFO_ADDRESS;
    const uint8_t *base=(const uint8_t *)SD_BOOT_PAYLOAD_ADDRESS;
    const struct sd_boot_payload_header *h=(const struct sd_boot_payload_header *)base;
    const uint32_t head=sizeof(*h);
    info->stub_start=timer();info->stub_end=0;
    info->magic=0;info->version=2;info->cpu1_entry=0;info->payload_crc=0;
    if(h->magic!=SD_BOOT_PAYLOAD_MAGIC || h->version!=1 || !h->count || h->count>64 ||
       h->data_bytes>SD_BOOT_PAYLOAD_LIMIT-head || h->count*sizeof(struct sd_boot_record)>h->data_bytes) {
        info->payload_crc=0xbad00001u;return;
    }
    if(crc32(base+head,h->data_bytes)!=h->crc32) {info->payload_crc=0xbad00002u;return;}
    const struct sd_boot_record *r=(const struct sd_boot_record *)(base+head);
    for(uint32_t i=0;i<h->count;++i) {
        uint32_t dest=r[i].dest,file=r[i].file_bytes,mem=r[i].mem_bytes,off=r[i].offset;
        if(file>mem || off<head || off>head+h->data_bytes || file>head+h->data_bytes-off ||
           dest+mem<dest || overlaps(dest,mem,SD_BOOT_STUB_ADDRESS,0x00800000u) ||
           overlaps(dest,mem,SD_BOOT_INFO_ADDRESS,0x1000u)) {info->payload_crc=0xbad00003u;return;}
        const uint8_t *src=base+off;uint8_t *dst=(uint8_t *)dest;uint32_t n=0;
        if(!((dest|off)&3u)) for(;n+4u<=file;n+=4u) *(volatile uint32_t *)(dst+n)=*(const uint32_t *)(src+n);
        for(;n<file;++n) dst[n]=src[n];
        for(;n<mem && (dest+n)&3u;++n) dst[n]=0;
        for(;n+4u<=mem;n+=4u) *(volatile uint32_t *)(dst+n)=0;
        for(;n<mem;++n) dst[n]=0;
    }
    info->cpu1_entry=h->cpu1_entry;info->payload_crc=h->crc32;info->stub_end=timer();
    __asm__ volatile("dsb sy":::"memory");
    info->magic=SD_BOOT_INFO_MAGIC;
    __asm__ volatile("dsb sy\n isb sy":::"memory");
    stub_enter(h->cpu0_entry);
}
