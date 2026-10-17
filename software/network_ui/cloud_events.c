#include "cloud_events.h"
#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "../../third_party/jsmn/jsmn.h"

static int cloud_json_scalar(const char *s,int end,int *at,uint32_t *cp);
static int cloud_json_equal(const char *s,const jsmntok_t *t,const char *value) {
    if(t->type!=JSMN_STRING) return 0;
    int i=t->start;
    while(i<t->end && *value) {
        uint32_t cp;if(!cloud_json_scalar(s,t->end,&i,&cp) || cp!=(uint8_t)*value++) return 0;
    }
    return i==t->end && !*value;
}
static int cloud_json_member(const char *s,const jsmntok_t *t,int count,int object,const char *key) {
    if(object<0 || t[object].type!=JSMN_OBJECT) return -1;
    int found=-1;
    for(int i=object+1;i<count && t[i].start<t[object].end;++i) {
        if(t[i].parent!=object || !cloud_json_equal(s,t+i,key)) continue;
        if(found!=-1 || i+1>=count || t[i+1].parent!=i) return -1;
        found=i+1;
    }
    return found;
}
static int cloud_json_hex4(const char *s,int end,int *at,uint32_t *value) {
    *value=0;
    for(int i=0;i<4;++i) {
        if(*at==end) return 0;
        uint32_t c=(uint8_t)s[(*at)++],digit;
        if(c>='0' && c<='9') digit=c-'0';
        else if(c>='a' && c<='f') digit=c-'a'+10;
        else if(c>='A' && c<='F') digit=c-'A'+10;
        else return 0;
        *value=(*value<<4)|digit;
    }
    return 1;
}
static int cloud_json_scalar(const char *s,int end,int *at,uint32_t *cp) {
    uint32_t c=(uint8_t)s[(*at)++];
    if(c=='\\') {
        if(*at==end) return 0;
        c=(uint8_t)s[(*at)++];
        if(c=='"' || c=='\\' || c=='/') {*cp=c;return 1;}
        if(c=='n' || c=='r' || c=='t') {*cp=' ';return 1;}
        if(c!='u' || !cloud_json_hex4(s,end,at,cp)) return 0;
        if(*cp>=0xd800u && *cp<=0xdbffu) {
            uint32_t low;
            if(end-*at<6 || s[*at]!='\\' || s[*at+1]!='u') return 0;
            *at+=2;
            if(!cloud_json_hex4(s,end,at,&low) || low<0xdc00u || low>0xdfffu) return 0;
            *cp=0x10000u+((*cp-0xd800u)<<10)+(low-0xdc00u);
        }
    } else {
        uint32_t more,min;
        if(c<0x20u) return 0;
        if(c<0x80u) {*cp=c;more=0;min=0;}
        else if(c>=0xc2u && c<=0xdfu) {*cp=c&31u;more=1;min=0x80u;}
        else if(c>=0xe0u && c<=0xefu) {*cp=c&15u;more=2;min=0x800u;}
        else if(c>=0xf0u && c<=0xf4u) {*cp=c&7u;more=3;min=0x10000u;}
        else return 0;
        if(end-*at<(int)more) return 0;
        for(uint32_t i=0;i<more;++i) {
            c=(uint8_t)s[(*at)++];if((c&0xc0u)!=0x80u) return 0;
            *cp=(*cp<<6)|(c&63u);
        }
        if(*cp<min) return 0;
    }
    if(*cp=='\n' || *cp=='\r' || *cp=='\t') *cp=' ';
    return *cp>=0x20u && *cp!=0x7fu && *cp<=0x10ffffu && !(*cp>=0xd800u && *cp<=0xdfffu);
}
static enum cloud_parse_code cloud_json_text(const char *s,const jsmntok_t *t,int token,uint32_t *out) {
    if(token<0 || t[token].type!=JSMN_STRING) return CLOUD_SCHEMA;
    int at=t[token].start;uint32_t n=0;
    while(at<t[token].end) {
        if(n==UI_MAX_TEXT-1) return CLOUD_LIMIT;
        if(!cloud_json_scalar(s,t[token].end,&at,out+n)) return CLOUD_JSON;
        ++n;
    }
    out[n]=0;return n?CLOUD_OK:CLOUD_SCHEMA;
}
static enum cloud_parse_code cloud_json_ascii(const char *s,const jsmntok_t *t,int token,char *out) {
    if(token<0 || t[token].type!=JSMN_STRING) return CLOUD_SCHEMA;
    int at=t[token].start;uint32_t n=0;
    while(at<t[token].end) {
        uint32_t cp;
        if(n==CLOUD_ID_BYTES-1) return CLOUD_LIMIT;
        if(!cloud_json_scalar(s,t[token].end,&at,&cp) || cp<0x20u || cp>0x7eu) return CLOUD_SCHEMA;
        out[n++]=(char)cp;
    }
    out[n]=0;return n?CLOUD_OK:CLOUD_SCHEMA;
}
static enum cloud_parse_code cloud_json_identifier(const char *s,const jsmntok_t *t,int token,char *out) {    if(token<0 || t[token].type!=JSMN_STRING) return CLOUD_SCHEMA;
    int at=t[token].start;uint32_t n=0;
    while(at<t[token].end) {
        uint32_t cp;
        if(n==CLOUD_ID_BYTES-1) return CLOUD_LIMIT;
        if(!cloud_json_scalar(s,t[token].end,&at,&cp) || cp<0x21u || cp>0x7eu) return CLOUD_SCHEMA;
        out[n++]=(char)cp;
    }
    out[n]=0;return n?CLOUD_OK:CLOUD_SCHEMA;
}
enum cloud_parse_code cloud_event_parse(const char *s,uint32_t bytes,struct cloud_event *event) {
    *event=(struct cloud_event){0};
    if(!bytes || bytes>CLOUD_JSON_BYTES) return CLOUD_LIMIT;
    jsmn_parser parser;jsmntok_t tokens[128];jsmn_init(&parser);
    int count=jsmn_parse(&parser,s,bytes,tokens,128);
    if(count==JSMN_ERROR_NOMEM) return CLOUD_LIMIT;
    if(count<1 || tokens[0].type!=JSMN_OBJECT) return CLOUD_JSON;
    for(uint32_t i=(uint32_t)tokens[0].end;i<bytes;++i) {
        if(s[i]!=' ' && s[i]!='\n' && s[i]!='\r' && s[i]!='\t') return CLOUD_JSON;
    }
    int type=cloud_json_member(s,tokens,count,0,"type"),value;
    if(type<0 || tokens[type].type!=JSMN_STRING) return CLOUD_SCHEMA;
    enum cloud_parse_code code;
    if(cloud_json_equal(s,tokens+type,"conversation.item.created")) {
        int item=cloud_json_member(s,tokens,count,0,"item");
        if(item<0 || tokens[item].type!=JSMN_OBJECT) return CLOUD_SCHEMA;
        value=cloud_json_member(s,tokens,count,0,"previous_item_id");
        if(value<0 || tokens[value].type==JSMN_PRIMITIVE) return CLOUD_OK;
        code=cloud_json_identifier(s,tokens,value,event->previous_id);if(code) return code;
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,item,"id"),event->item_id);if(code) return code;
        event->kind=CLOUD_ITEM_LINK;return CLOUD_OK;
    }
    if(cloud_json_equal(s,tokens+type,"conversation.item.input_audio_transcription.completed")) {
        event->kind=CLOUD_ASR_FINAL;
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,0,"item_id"),event->item_id);if(code) return code;
        return cloud_json_text(s,tokens,cloud_json_member(s,tokens,count,0,"transcript"),event->text);
    }
    if(cloud_json_equal(s,tokens+type,"response.text.delta")) {event->kind=CLOUD_TRANSLATION_DELTA;return CLOUD_OK;}
    if(cloud_json_equal(s,tokens+type,"response.text.done")) {
        event->kind=CLOUD_TRANSLATION;
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,0,"response_id"),event->response_id);if(code) return code;
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,0,"item_id"),event->item_id);if(code) return code;
        return cloud_json_text(s,tokens,cloud_json_member(s,tokens,count,0,"text"),event->text);
    }
    if(cloud_json_equal(s,tokens+type,"response.done")) {
        event->kind=CLOUD_RESPONSE_DONE;int response=cloud_json_member(s,tokens,count,0,"response");
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,response,"id"),event->response_id);if(code) return code;
        value=cloud_json_member(s,tokens,count,response,"status");
        if(value<0 || tokens[value].type!=JSMN_STRING) return CLOUD_SCHEMA;
        event->completed=(uint32_t)cloud_json_equal(s,tokens+value,"completed");return CLOUD_OK;
    }
    if(cloud_json_equal(s,tokens+type,"input_audio_buffer.speech_started")) {event->kind=CLOUD_SPEECH_STARTED;return CLOUD_OK;}
    if(cloud_json_equal(s,tokens+type,"session.created")) {event->kind=CLOUD_SESSION_CREATED;return CLOUD_OK;}
    if(cloud_json_equal(s,tokens+type,"session.updated")) {event->kind=CLOUD_SESSION_UPDATED;return CLOUD_OK;}
    if(cloud_json_equal(s,tokens+type,"session.finished")) {event->kind=CLOUD_FINISHED;return CLOUD_OK;}
    if(cloud_json_equal(s,tokens+type,"error") || cloud_json_equal(s,tokens+type,"conversation.item.input_audio_transcription.failed")) {
        event->kind=CLOUD_ERROR;int error=cloud_json_member(s,tokens,count,0,"error");
        code=cloud_json_identifier(s,tokens,cloud_json_member(s,tokens,count,error,"code"),event->error_code);
        if(code) return code;
        return cloud_json_ascii(s,tokens,cloud_json_member(s,tokens,count,error,"message"),event->error_message);
    }
    return CLOUD_OK;
}
