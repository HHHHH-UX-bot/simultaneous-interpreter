#include <stddef.h>
void *memcpy(void *dest,const void *source,size_t n) {
    volatile unsigned char *d=dest;const volatile unsigned char *s=source;
    for(size_t i=0;i<n;++i) d[i]=s[i];
    return dest;
}
void *memset(void *dest,int value,size_t n) {
    volatile unsigned char *d=dest;for(size_t i=0;i<n;++i) d[i]=(unsigned char)value;return dest;
}
void *memmove(void *dest,const void *source,size_t n) {
    volatile unsigned char *d=dest;const volatile unsigned char *s=source;
    if(d<s) for(size_t i=0;i<n;++i) d[i]=s[i];
    else for(size_t i=n;i>0;--i) d[i-1]=s[i-1];
    return dest;
}
