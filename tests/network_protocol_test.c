#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
static struct ui_state state,before;
static uint8_t frame[NET_FRAME_BYTES+32];
static uint32_t last,request,checks;
static void check(int yes,const char *message) {++checks;if(!yes) {fprintf(stderr,"FAIL %s\n",message);exit(1);}}
static uint32_t make(uint32_t type,uint32_t segment,uint32_t rev,uint32_t source_rev,const char *text) {
    uint32_t n=text?(uint32_t)strlen(text):0;
    uint32_t words[]={0x31425553,1,++request,type,state.epoch,segment,rev,source_rev,n};
    for(uint32_t i=0;i<9;++i) net_put(frame+4*i,words[i]);
    if(n) memcpy(frame+NET_HEADER_BYTES,text,n);
    return NET_HEADER_BYTES+n;
}
static void unchanged(uint32_t n,uint32_t code,const char *m) {
    before=state;check(net_apply(&state,frame,n,&last)==code,m);check(memcmp(&before,&state,sizeof(state))==0,"bad input leaves entire model unchanged");
}
int main(void) {
    ui_init(&state);check(ui_control(&state,UI_START),"start");
    uint32_t n=make(0,0,0,0,0);check(net_apply(&state,frame,n,&last)==NET_OK,"hello");
    check(net_apply(&state,frame,n,&last)==NET_SEQUENCE,"duplicate request");
    n=make(3,1,2,0,"Please check the interface before connecting the module.");
    check(net_apply(&state,frame,n,&last)==NET_OK && state.source_final,"UTF-8 ASCII ASR final");
    n=make(4,1,1,2,0);check(net_apply(&state,frame,n,&last)==NET_OK && state.stage==UI_TRANSLATING,"ordered translating");
    n=make(5,1,1,2,"开始之前，请确认每个人都能清楚地听到发言。");
    check(net_apply(&state,frame,n,&last)==NET_OK && state.final_count==1 && state.history_count==1,"UTF-8 Chinese final");
    uint8_t ack[32];net_ack(ack,frame,0,&state);
    check(net_word(ack)==0x314b4341 && net_word(ack+8)==request && net_word(ack+20)==state.epoch && net_word(ack+28)==1,"exact ACK framing");
    n=make(3,2,1,0,"A");net_put(frame+16,state.epoch-1);unchanged(n,NET_EPOCH,"stale epoch");
    n=make(3,2,1,0,"A");net_put(frame,0);unchanged(n,NET_HEADER,"bad magic");
    n=make(3,2,1,0,"A");net_put(frame+4,2);unchanged(n,NET_HEADER,"version mismatch");
    n=make(3,2,1,0,"A");unchanged(n-1,NET_LENGTH,"length mismatch");
    n=make(3,2,1,0,"A");net_put(frame+32,NET_TEXT_BYTES+1);unchanged(n,NET_LENGTH,"oversize");
    const unsigned char *bad[]={ (const unsigned char *)"\xc0\xaf",(const unsigned char *)"\xe0\x80\xaf",
        (const unsigned char *)"\xed\xa0\x80",(const unsigned char *)"\xf4\x90\x80\x80",
        (const unsigned char *)"\xe4\xb8",(const unsigned char *)"\x80",(const unsigned char *)"\n"};
    for(uint32_t i=0;i<7;++i) {n=make(3,2,1,0,(const char *)bad[i]);unchanged(n,NET_UTF8,"malformed/scalar/control UTF-8");}
    n=make(3,2,1,0,"\xf0\x9f\x98\x80");unchanged(n,NET_GLYPH,"unknown glyph");
    char boundary[257];memset(boundary,'A',256);boundary[256]=0;
    n=make(3,2,1,0,boundary);unchanged(n,NET_LENGTH,"256 scalars exceeds model");
    boundary[255]=0;n=make(3,2,1,0,boundary);check(net_apply(&state,frame,n,&last)==NET_OK,"255 scalar boundary");
    n=make(4,2,1,1,0);check(net_apply(&state,frame,n,&last)==NET_OK,"second translating");
    n=make(5,2,1,1,"A");check(net_apply(&state,frame,n,&last)==NET_OK,"second final");
    uint32_t old_epoch=state.epoch;
    n=make(1,UI_PAUSE,0,0,0);check(net_apply(&state,frame,n,&last)==NET_OK,"network pause");
    n=make(1,UI_RESUME,0,0,0);check(net_apply(&state,frame,n,&last)==NET_OK && state.epoch==old_epoch+1 && state.final_count==2,"network resume retains finals/new epoch");
    n=make(5,3,1,1,"A");net_put(frame+16,old_epoch);unchanged(n,NET_EPOCH,"old result after resume");
    for(uint32_t seg=1;seg<=6;++seg) {
        n=make(3,seg,1,0,"A");check(net_apply(&state,frame,n,&last)==NET_OK,"bounded history ASR");
        n=make(4,seg,1,1,0);check(net_apply(&state,frame,n,&last)==NET_OK,"bounded history translating");
        n=make(5,seg,1,1,"A");check(net_apply(&state,frame,n,&last)==NET_OK,"bounded history final");
    }
    check(state.history_count==4 && state.final_count==8,"history remains bounded");
    n=make(1,UI_CLEAR,0,0,0);check(net_apply(&state,frame,n,&last)==NET_OK && state.history_count==0 && state.session==UI_IDLE,"network clear");
    printf("PASS_NETWORK_PROTOCOL checks=%u\n",checks);return 0;
}
