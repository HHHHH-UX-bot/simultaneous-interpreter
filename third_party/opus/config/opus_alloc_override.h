#ifndef OPUS_ALLOC_OVERRIDE_H
#define OPUS_ALLOC_OVERRIDE_H

/* Force-included when compiling third_party/opus: route all Opus allocations
   through the bare-metal static pool in software/network_ui/opus_codec.c. */
#include <stddef.h>

#define OVERRIDE_OPUS_ALLOC 1
#define OVERRIDE_OPUS_FREE 1
#define OVERRIDE_OPUS_ALLOC_SCRATCH 1

void *opus_alloc(size_t size);
void *opus_alloc_scratch(size_t size);
void opus_free(void *ptr);

#endif
