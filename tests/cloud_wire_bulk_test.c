#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cloud_wire.h"
static unsigned calls,checks;
static char expected[3001];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"bulk WS failure line %d\n",__LINE__);exit(1);}}while(0)
static int receive(const char *p,uint32_t n,void *u) {
    (void)u;
    if(!calls) CHECK(n==3000 && !memcmp(p,expected,n));
    else CHECK(calls==1 && n==6 && !memcmp(p,"onetwo",6));
    ++calls;return 0;
}
int main(void) {
    uint8_t wire[3020];unsigned n=0;
    memset(expected,'a',3000);expected[3000]=0;
    wire[n++]=0x81;wire[n++]=126;wire[n++]=3000>>8;wire[n++]=3000&255;
    memcpy(wire+n,expected,3000);n+=3000;
    const uint8_t tail[]={0x01,3,'o','n','e',0x89,2,'x','y',0x80,3,'t','w','o'};
    memcpy(wire+n,tail,sizeof(tail));n+=sizeof(tail);
    const unsigned blocks[]={1,2,3,7,31,125,127,1024,2048,4096,8192};
    for(unsigned i=0;i<sizeof(blocks)/sizeof(blocks[0]);++i) {
        calls=0;cloud_wire_init(receive,0);
        for(unsigned at=0;at<n;) {unsigned len=n-at;if(len>blocks[i])len=blocks[i];cloud_wire_bytes(wire+at,len);at+=len;}
        CHECK(!cloud_wire_status()->error && cloud_wire_status()->messages==2 && cloud_wire_status()->pings==1 && calls==2);
        uint32_t reply;CHECK(cloud_wire_reply(&reply) && reply==8);cloud_wire_reply_sent();
    }
    cloud_wire_init(receive,0);const uint8_t big[]={0x81,127,0,0,0,0,0,1,0,1,'x'};
    cloud_wire_bytes(big,sizeof(big));CHECK(cloud_wire_status()->error==6);
    cloud_wire_init(receive,0);const uint8_t masked[]={0x81,0x81,0,0,0,0,'x'};
    cloud_wire_bytes(masked,sizeof(masked));CHECK(cloud_wire_status()->error==4);
    printf("PASS_CLOUD_WIRE_BULK checks=%u arbitrary splits, merged frames, fragmented messages, interleaved ping and frame bounds\n",checks);return 0;
}
