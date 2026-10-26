/* Single-core bounded allocator for tokenizer working memory. The model and
   inference arena have separate fixed DDR windows; newlib's heap is unused. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifndef TINY_HEAP_BYTES
#define TINY_HEAP_BYTES (8u*1024u*1024u)
#endif
#ifdef TINY_HEAP_TEST
static uint64_t test_pool[(TINY_HEAP_BYTES+7u)/8u];
#define TINY_HEAP_BASE ((uintptr_t)test_pool)
#define HEAP_MALLOC tiny_test_malloc
#define HEAP_CALLOC tiny_test_calloc
#define HEAP_REALLOC tiny_test_realloc
#define HEAP_FREE tiny_test_free
#define HEAP_ERRNO tiny_test_errno
#else
#ifndef TINY_HEAP_BASE
#define TINY_HEAP_BASE ((uintptr_t)0x1f000000u)
#endif
#define HEAP_MALLOC malloc
#define HEAP_CALLOC calloc
#define HEAP_REALLOC realloc
#define HEAP_FREE free
#define HEAP_ERRNO __errno
#endif

#define HEAP_ALIGNMENT 8u
struct heap_block {size_t bytes;struct heap_block *next;unsigned available;};
#define HEADER_BYTES ((sizeof(struct heap_block)+HEAP_ALIGNMENT-1u)&~(size_t)(HEAP_ALIGNMENT-1u))
static struct heap_block *first;
static int heap_errno;
int *HEAP_ERRNO(void) {return &heap_errno;}

static void initialize(void) {
    uintptr_t base=TINY_HEAP_BASE;
    uintptr_t aligned=(base+HEAP_ALIGNMENT-1u)&~(uintptr_t)(HEAP_ALIGNMENT-1u);
    size_t prefix=(size_t)(aligned-base);
    if(first || TINY_HEAP_BYTES<=prefix+HEADER_BYTES)return;
    first=(struct heap_block *)aligned;
    first->bytes=((size_t)TINY_HEAP_BYTES-prefix-HEADER_BYTES)&~(size_t)(HEAP_ALIGNMENT-1u);
    first->next=NULL;first->available=1;
}
static int align_request(size_t bytes,size_t *aligned) {
    if(bytes>SIZE_MAX-(HEAP_ALIGNMENT-1u)){heap_errno=ENOMEM;return 0;}
    *aligned=(bytes+HEAP_ALIGNMENT-1u)&~(size_t)(HEAP_ALIGNMENT-1u);return 1;
}
static void join_next(struct heap_block *block) {
    struct heap_block *next=block->next;
    if(next && next->available){block->bytes+=HEADER_BYTES+next->bytes;block->next=next->next;}
}
static void split(struct heap_block *block,size_t bytes) {
    struct heap_block *tail;
    if(block->bytes-bytes<HEADER_BYTES+HEAP_ALIGNMENT)return;
    tail=(struct heap_block *)((uint8_t *)block+HEADER_BYTES+bytes);
    tail->bytes=block->bytes-bytes-HEADER_BYTES;tail->next=block->next;tail->available=1;
    block->bytes=bytes;block->next=tail;join_next(tail);
}
static struct heap_block *find_block(void *ptr,struct heap_block **previous) {
    struct heap_block *block,*before=NULL;
    for(block=first;block;before=block,block=block->next) {
        if((uint8_t *)block+HEADER_BYTES==(uint8_t *)ptr) {
            if(previous)*previous=before;
            return block;
        }
    }
    heap_errno=EINVAL;return NULL;
}
void *HEAP_MALLOC(size_t bytes) {
    struct heap_block *block;size_t aligned;
    if(!bytes)return NULL;
    if(!align_request(bytes,&aligned))return NULL;
    initialize();
    for(block=first;block;block=block->next)if(block->available && block->bytes>=aligned) {
        split(block,aligned);block->available=0;
        return (uint8_t *)block+HEADER_BYTES;
    }
    heap_errno=ENOMEM;return NULL;
}
void HEAP_FREE(void *ptr) {
    struct heap_block *block,*before;
    if(!ptr)return;
    block=find_block(ptr,&before);
    if(!block)return;
    if(block->available){heap_errno=EINVAL;return;}
    block->available=1;join_next(block);
    if(before && before->available)join_next(before);
}
void *HEAP_CALLOC(size_t count,size_t bytes) {
    void *ptr;size_t total;
    if(bytes && count>SIZE_MAX/bytes){heap_errno=ENOMEM;return NULL;}
    total=count*bytes;ptr=HEAP_MALLOC(total);
    if(ptr)memset(ptr,0,total);
    return ptr;
}
void *HEAP_REALLOC(void *ptr,size_t bytes) {
    struct heap_block *block;size_t aligned;void *replacement;
    if(!ptr)return HEAP_MALLOC(bytes);
    if(!bytes){HEAP_FREE(ptr);return NULL;}
    block=find_block(ptr,NULL);
    if(!block || block->available){heap_errno=EINVAL;return NULL;}
    if(!align_request(bytes,&aligned))return NULL;
    if(block->bytes>=aligned){split(block,aligned);return ptr;}
    if(block->next && block->next->available &&
       block->bytes+HEADER_BYTES+block->next->bytes>=aligned) {
        join_next(block);split(block,aligned);return ptr;
    }
    replacement=HEAP_MALLOC(bytes);
    if(!replacement)return NULL;
    memcpy(replacement,ptr,block->bytes);HEAP_FREE(ptr);return replacement;
}
