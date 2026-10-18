#include "cache.h"

/* Cortex-A9 L1 data cache line is 32 bytes. DCCMVAC cleans the line containing
   the given address to the point of coherency (L2/DDR), which is what the
   VDMA and the debugging host read. */
void cache_clean_range(const void *start, unsigned bytes) {
    unsigned address = (unsigned)start & ~31u;
    const unsigned end = (unsigned)start + bytes;
    while (address < end) {
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(address) : "memory");
        address += 32u;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}
