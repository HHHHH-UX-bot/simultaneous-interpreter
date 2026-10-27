#include "glossary.h"

static char text[GLOSSARY_FILE_BYTES];
static struct {uint16_t zh,zh_bytes,en,en_bytes;} pairs[GLOSSARY_PAIRS];
static uint32_t count,skipped;

static int valid_utf8(const char *s,uint32_t n) {
    for(uint32_t i=0;i<n;) {
        uint32_t c=(uint8_t)s[i],more,cp;
        if(c<0x20u || c==0x7fu) return 0;
        if(c<0x80u) {++i;continue;}
        if(c>=0xc2u && c<=0xdfu) {more=1;cp=c&31u;}
        else if(c>=0xe0u && c<=0xefu) {more=2;cp=c&15u;}
        else if(c>=0xf0u && c<=0xf4u) {more=3;cp=c&7u;}
        else return 0;
        if(more>n-i-1u) return 0;
        for(uint32_t k=1;k<=more;++k) {
            uint32_t b=(uint8_t)s[i+k];if((b&0xc0u)!=0x80u) return 0;
            cp=(cp<<6)|(b&63u);
        }
        if((more==2 && (cp<0x800u || (cp>=0xd800u && cp<=0xdfffu))) || (more==3 && (cp<0x10000u || cp>0x10ffffu))) return 0;
        i+=more+1u;
    }
    return 1;
}
static uint32_t escaped(const char *s,uint32_t n) {
    uint32_t bytes=n;
    for(uint32_t i=0;i<n;++i) if(s[i]=='"' || s[i]=='\\') ++bytes;
    return bytes;
}
static int blank(char c) {return c==' ' || c=='\t' || c=='\r';}

uint32_t glossary_load(const uint8_t *data,uint32_t bytes) {
    count=skipped=0;
    if(bytes>GLOSSARY_FILE_BYTES) bytes=GLOSSARY_FILE_BYTES;
    for(uint32_t i=0;i<bytes;++i) text[i]=(char)data[i];
    uint32_t at=bytes>=3u && (uint8_t)text[0]==0xefu && (uint8_t)text[1]==0xbbu && (uint8_t)text[2]==0xbfu?3u:0u;
    /* ,"corpus":{"phrases":{}} around the pairs, then "k":"v" plus a comma each. */
    uint32_t json=24u;
    while(at<bytes) {
        uint32_t end=at;while(end<bytes && text[end]!='\n') ++end;
        uint32_t a=at,b=end;at=end+1u;
        while(a<b && blank(text[a])) ++a;
        while(b>a && blank(text[b-1u])) --b;
        if(a==b || text[a]=='#') continue;
        uint32_t split=a;while(split<b && text[split]!='=') ++split;
        uint32_t zh_end=split,en=split+1u;
        while(zh_end>a && blank(text[zh_end-1u])) --zh_end;
        while(en<b && blank(text[en])) ++en;
        if(split==b || zh_end==a || en==b || !valid_utf8(text+a,zh_end-a) || !valid_utf8(text+en,b-en)) {++skipped;continue;}
        uint32_t need=escaped(text+a,zh_end-a)+escaped(text+en,b-en)+6u;
        if(count==GLOSSARY_PAIRS || json+need>GLOSSARY_JSON_BUDGET) {++skipped;continue;}
        pairs[count].zh=(uint16_t)a;pairs[count].zh_bytes=(uint16_t)(zh_end-a);
        pairs[count].en=(uint16_t)en;pairs[count].en_bytes=(uint16_t)(b-en);
        json+=need;++count;
    }
    return count;
}
uint32_t glossary_terms(void) {return count;}
uint32_t glossary_skipped(void) {return skipped;}

/* quote: a JSON string value, quoted and escaped; otherwise raw JSON syntax. */
static int put(char *out,uint32_t *used,uint32_t capacity,const char *s,uint32_t n,int quote) {
    if(*used+(quote?escaped(s,n)+2u:n)>capacity) return 0;
    if(quote) out[(*used)++]='"';
    for(uint32_t i=0;i<n;++i) {if(quote && (s[i]=='"' || s[i]=='\\')) out[(*used)++]='\\';out[(*used)++]=s[i];}
    if(quote) out[(*used)++]='"';
    return 1;
}
uint32_t glossary_session(const char *base,uint32_t base_bytes,int english_source,char *out,uint32_t capacity) {
    if(base_bytes<4u) return 0;
    for(uint32_t i=base_bytes-4u;i<base_bytes;++i) if(base[i]!='}') return 0;
    uint32_t used=0,head=count?base_bytes-3u:base_bytes;
    if(head>capacity) return 0;
    for(uint32_t i=0;i<head;++i) out[used++]=base[i];
    if(!count) return used;
    static const char open[]=",\"corpus\":{\"phrases\":{";
    if(!put(out,&used,capacity,open,sizeof(open)-1u,0)) return 0;
    for(uint32_t i=0;i<count;++i) {
        const char *key=text+(english_source?pairs[i].en:pairs[i].zh),*value=text+(english_source?pairs[i].zh:pairs[i].en);
        uint32_t key_bytes=english_source?pairs[i].en_bytes:pairs[i].zh_bytes,value_bytes=english_source?pairs[i].zh_bytes:pairs[i].en_bytes;
        if((i && !put(out,&used,capacity,",",1,0)) || !put(out,&used,capacity,key,key_bytes,1) ||
           !put(out,&used,capacity,":",1,0) || !put(out,&used,capacity,value,value_bytes,1)) return 0;
    }
    if(!put(out,&used,capacity,"}}}}}",5,0)) return 0;
    return used;
}
