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
#ifdef Z7_ARM_TLS
/* Mbed TLS 的证书解析所需的小型标准 C 字符串操作。 */
size_t strlen(const char *s) {size_t n=0;while(s[n]) ++n;return n;}
int memcmp(const void *a,const void *b,size_t n) {
    const unsigned char *x=a,*y=b;
    for(size_t i=0;i<n;++i) if(x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
char *strchr(const char *s,int c) {do {if(*s==(char)c) return (char *)s;} while(*s++);return 0;}
char *strstr(const char *s,const char *p) {
    for(;*s;++s) {size_t n=0;while(p[n] && s[n]==p[n]) ++n;if(!p[n]) return (char *)s;}
    return !*p?(char *)s:0;
}
/* TLS 固定池内部一致性遭破坏时停止，不能继续使用损坏的密钥状态。 */
__attribute__((noreturn)) void exit(int code) {(void)code;for(;;) {__asm__ volatile("wfi");}}
#endif
