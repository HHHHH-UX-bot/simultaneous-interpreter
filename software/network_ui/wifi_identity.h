#ifndef WIFI_IDENTITY_H
#define WIFI_IDENTITY_H
#include <stdint.h>
/* Public association identity only; never read the configuration password. */
struct wifi_identity {uint32_t connected;uint8_t name[33];};
static inline void wifi_identity_clear(struct wifi_identity *s) {
 s->connected=0;for(uint32_t i=0;i<33u;++i)s->name[i]=0;
}
static inline int wifi_identity_parse(struct wifi_identity *s,const char *reply) {
 const char tag[]="+CWJAP:\"";const char *p=reply;
 wifi_identity_clear(s);
 while(*p){uint32_t i=0;while(tag[i] && p[i] && tag[i]==p[i])++i;if(!tag[i]){p+=i;break;}++p;}
 if(!*p)return 0;
 uint8_t name[33]={0};uint32_t n=0;
 while(*p && *p!='"'){
  uint8_t c=(uint8_t)*p++;
  if(c=='\\'){if(*p!='\\' && *p!='"' && *p!=',')return 0;c=(uint8_t)*p++;}
  if(c<32u || c==127u || n==32u)return 0;
  name[n++]=c;
 }
 if(!n || *p!='"' || p[1]!=',')return 0;
 for(uint32_t i=0;i<=n;++i)s->name[i]=name[i];
 s->connected=1;return 1;
}
#endif
