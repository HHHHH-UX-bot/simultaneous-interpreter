#include "../software/network_ui/cloud_events.h"
#include <stdio.h>
#include <string.h>
static struct cloud_event event;
static unsigned checks;
#define CHECK(test) do {++checks;if(!(test)) {printf("failure line %u\n",(unsigned)__LINE__);return 1;}} while(0)
static int unit(void) {
    const char *json="{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"\\u4f60\\u597d\\n\\ud83d\\ude00\\\"\\\\\"}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK);
    CHECK(event.kind==CLOUD_TRANSLATION && !strcmp(event.response_id,"r1") && !strcmp(event.item_id,"i1"));
    CHECK(event.text[0]==0x4f60 && event.text[1]==0x597d && event.text[2]==' ' && event.text[3]==0x1f600 && event.text[4]=='"' && event.text[5]=='\\' && !event.text[6]);
    const char *invalid[]={"{", "{}{}", "[]", "{\"type\":4}","{\"type\":\"session.finished\",\"type\":\"response.done\"}",
        "{\"type\":\"response.text.done\",\"item_id\":\"i1\",\"text\":\"ok\"}",
        "{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"\\ud800\"}",
        "{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"\\udc00\"}",
        "{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"\300\257\"}",
        "{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"a\nb\"}",
        "{\"type\":\"error\",\"not_error\":{\"code\":\"ignored\"}}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) CHECK(cloud_event_parse(invalid[i],(uint32_t)strlen(invalid[i]),&event)!=CLOUD_OK);
    static char boundary[4098];
    const char *prefix="{\"type\":\"response.text.done\",\"response_id\":\"r1\",\"item_id\":\"i1\",\"text\":\"";
    size_t at=strlen(prefix);memcpy(boundary,prefix,at);memset(boundary+at,'x',255);strcpy(boundary+at+255,"\"}");
    CHECK(cloud_event_parse(boundary,(uint32_t)strlen(boundary),&event)==CLOUD_OK && event.text[254]=='x' && !event.text[255]);
    memset(boundary+at,'x',256);strcpy(boundary+at+256,"\"}");
    CHECK(cloud_event_parse(boundary,(uint32_t)strlen(boundary),&event)==CLOUD_LIMIT);
    CHECK(cloud_event_parse("",0,&event)==CLOUD_LIMIT);
    CHECK(cloud_event_parse(boundary,4097,&event)==CLOUD_LIMIT);
    json="{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"failed\"}}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK && !event.completed && event.kind==CLOUD_RESPONSE_DONE);
    json="{\"type\":\"error\",\"error\":{\"code\":\"invalid_parameter\"}}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK && event.kind==CLOUD_ERROR && !strcmp(event.error_code,"invalid_parameter"));
    json="{\"type\":\"session.finished\",\"other\":[{\"type\":\"error\"}]}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK && event.kind==CLOUD_FINISHED);
    json="{\"t\\u0079pe\":\"session\\u002efinished\"}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK && event.kind==CLOUD_FINISHED);
    json="{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"st\\u0061tus\":\"comple\\u0074ed\"}}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_OK && event.completed);
    json="{\"type\":\"session.finished\",\"t\\u0079pe\":\"session.finished\"}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_SCHEMA);
    json="{\"type\":\"response.text.done\",\"response_id\":\"r\",\"item_id\":\"i\",\"text\":\"a\",\"te\\u0078t\":\"b\"}";
    CHECK(cloud_event_parse(json,(uint32_t)strlen(json),&event)==CLOUD_SCHEMA);
    printf("PASS_CLOUD_EVENT_UNITS checks=%u\n",checks);return 0;
}
static void chars(const char *s) {
    putchar('[');for(unsigned i=0;s[i];++i) {if(i) putchar(',');printf("%u",(unsigned char)s[i]);}putchar(']');
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--unit")) return unit();
    char line[CLOUD_JSON_BYTES+2];
    while(fgets(line,sizeof(line),stdin)) {
        size_t n=strlen(line);if(n && line[n-1]=='\n') --n;
        unsigned code=cloud_event_parse(line,(uint32_t)n,&event);
        printf("[%u,%u,%u,",code,(unsigned)event.kind,event.completed);
        chars(event.item_id);putchar(',');chars(event.response_id);putchar(',');chars(event.error_code);printf(",[");
        for(unsigned i=0;event.text[i];++i) {if(i) putchar(',');printf("%u",event.text[i]);}
        puts("]]");
    }
    return ferror(stdin)!=0;
}
