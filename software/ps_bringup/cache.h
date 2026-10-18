#ifndef Z7_CACHE_H
#define Z7_CACHE_H

/* Clean D-cache lines covering [start, start+bytes) to the point of coherency.
   Required before a DMA engine (VDMA) or JTAG reads memory written by the CPU
   once the MMU/caches are enabled. Addresses and lengths are physical and
   identity mapped by start.S. */
void cache_clean_range(const void *start, unsigned bytes);

#endif
