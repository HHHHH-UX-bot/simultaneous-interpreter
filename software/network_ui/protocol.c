#include "protocol.h"
#include "../subtitle_ui/ui_assets.h"
uint32_t net_word(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
void net_put(uint8_t *p,uint32_t v) {for(uint32_t i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i));}
int net_glyph_available(uint32_t cp) {
    for(uint32_t f=0;f<ui_font_count;++f) {
        uint32_t lo=0,hi=ui_fonts[f].count;
        while(lo<hi) {uint32_t m=lo+(hi-lo)/2;if(ui_fonts[f].glyphs[m].codepoint<cp) lo=m+1;else hi=m;}
        if(lo==ui_fonts[f].count || ui_fonts[f].glyphs[lo].codepoint!=cp) return 0;
    }
    return 1;
}
static uint32_t decode(const uint8_t *p,uint32_t bytes,uint32_t *text) {
    uint32_t i=0,n=0;
    while(i<bytes) {
        uint32_t cp=p[i++],more=0,min=0;
        if(cp<0x80u) {}
        else if(cp>=0xc2u && cp<=0xdfu) {more=1;min=0x80;cp&=31u;}
        else if(cp>=0xe0u && cp<=0xefu) {more=2;min=0x800;cp&=15u;}
        else if(cp>=0xf0u && cp<=0xf4u) {more=3;min=0x10000;cp&=7u;}
        else return NET_UTF8;
        if(i+more>bytes) return NET_UTF8;
        for(uint32_t j=0;j<more;++j) {uint32_t b=p[i++];if((b&0xc0u)!=0x80u) return NET_UTF8;cp=(cp<<6)|(b&63u);}
        if(cp<min || cp<0x20u || cp==0x7fu || cp>0x10ffffu || (cp>=0xd800u && cp<=0xdfffu)) return NET_UTF8;
        if(n>=UI_MAX_TEXT-1u) return NET_LENGTH;
        if(!net_glyph_available(cp)) return NET_GLYPH;
        text[n++]=cp;
    }
    text[n]=0;return n?NET_OK:NET_LENGTH;
}
uint32_t net_apply(struct ui_state *s,const uint8_t *f,uint32_t bytes,uint32_t *last) {
    if(!s || !f || !last || bytes<NET_HEADER_BYTES) return NET_HEADER;
    if(net_word(f)!=0x31425553u || net_word(f+4)!=1u || net_word(f+12)>5u) return NET_HEADER;
    uint32_t n=net_word(f+32),request=net_word(f+8),type=net_word(f+12);
    if(n>NET_TEXT_BYTES || bytes!=NET_HEADER_BYTES+n) return NET_LENGTH;
    if(!request || request<=*last) return NET_SEQUENCE;
    *last=request;
    if(type==0u) return n?NET_LENGTH:NET_OK;
    if(net_word(f+16)!=s->epoch) return NET_EPOCH;
    if(type==1u) {
        if(n || net_word(f+20)>UI_TOGGLE_SOURCE || net_word(f+24) || net_word(f+28)) return NET_HEADER;
        return ui_control(s,(enum ui_command)net_word(f+20))?NET_OK:NET_MODEL;
    }
    uint32_t text[UI_MAX_TEXT];
    if(type==4u) {if(n) return NET_LENGTH;text[0]=0;}
    else {uint32_t code=decode(f+NET_HEADER_BYTES,n,text);if(code) return code;}
    struct ui_event e={(enum ui_event_type)(type-2u),net_word(f+16),net_word(f+20),net_word(f+24),net_word(f+28),text};
    return ui_apply_event(s,&e)?NET_OK:NET_MODEL;
}
void net_ack(uint8_t *a,const uint8_t *f,uint32_t code,const struct ui_state *s) {
    uint32_t w[]={0x314b4341u,1u,net_word(f+8),net_word(f+12),code,s->epoch,s->history_count,s->final_count};
    for(uint32_t i=0;i<8;++i) net_put(a+i*4u,w[i]);
}
