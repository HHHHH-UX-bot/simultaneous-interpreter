#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t regs[2048],sent;static uint8_t packet[1536];static uint32_t packet_size;
static uint32_t mock_read(uint32_t o){return regs[o/4];}
static void mock_write(uint32_t o,uint32_t v){regs[o/4]=v;if((o==0x7fc || o==0xffc) && (v&1)){uint32_t b=o-0x7fc;packet_size=regs[(b+0x7f4)/4];memcpy(packet,(uint8_t *)regs+b,packet_size);++sent;regs[o/4]=0;}}
static uint32_t mock_wifi=1;
uint32_t network_wifi_name(uint8_t out[33]) {memset(out,0,33);if(mock_wifi)memcpy(out,"CYEA",4);return mock_wifi;}
#define REMOTE_READ mock_read
#define REMOTE_WRITE mock_write
#include "../software/network_ui/remote_display.c"
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL remote line %d\n",__LINE__);exit(1);}}while(0)
static uint32_t source[PIXELS];
static void request_packet(uint32_t op,uint32_t seq,uint32_t frame,uint32_t offset,int valid_cookie) {
    memset(rx,0,sizeof(rx));memcpy(rx+6,"CLIENT",6);b16(rx+12,0x800);uint8_t *p=rx+14;p[0]=0x45;p[8]=64;p[9]=17;b16(p+2,op==3?72:60);memcpy(p+12,"\xc0\xa8\x01\x66",4);memcpy(p+16,ip,4);b16(p+10,~sum16(p,20));uint8_t *u=p+20;b16(u,50000);b16(u+2,PORT);b16(u+4,op==3?52:40);uint8_t *q=u+8;l32(q,MAGIC);l32(q+4,op);l32(q+8,valid_cookie?123:999);l32(q+12,456);l32(q+16,seq);l32(q+20,frame);l32(q+24,offset);
    if(op==3){b16(q+32,900);b16(q+34,556);b16(q+36,900);b16(q+38,556);l32(q+40,42);}
}
int main(int argc,char **argv) {
    CHECK(argc==2);phy_at=100000u;remote_display_init(0);phy_at=100000u;
    request_packet(1,1,0,0,1);handle_frame(74);CHECK(peer_port==50000 && le32(packet+46)==0x81);
    for(uint32_t i=0;i<PIXELS;++i)source[i]=i<200000u?0x123456u:((i%71u)?0xeef3f2u:i*17u);
    remote_display_present(source,7,42);for(uint32_t i=0;i<300;++i)remote_display_tick(i+1);
    CHECK(ready==1 && ready_epoch==42 && ready_bytes>0 && ready_bytes<=PIXELS*5u);
    uint32_t pixel=0;
    for(uint32_t at=0;at<ready_bytes;at+=5) {uint8_t *p=encoded[ready_index]+at;uint32_t n=p[0]|(uint32_t)p[1]<<8,c=(uint32_t)p[2]<<16|(uint32_t)p[3]<<8|p[4];CHECK(n && pixel+n<=PIXELS);while(n--){CHECK(c==(source[pixel]&0xffffffu));++pixel;}}
    CHECK(pixel==PIXELS);FILE *f=fopen(argv[1],"wb");CHECK(f!=0);CHECK(fwrite(encoded[ready_index],1,ready_bytes,f)==ready_bytes);fclose(f);
    request_packet(2,2,ready,0,1);handle_frame(74);CHECK(le32(packet+46)==0x82 && le32(packet+66)==0 && packet_size<=1514);
    request_packet(3,3,ready,0,0);handle_frame(86);CHECK(!pending);
    request_packet(3,3,ready,0,1);handle_frame(86);struct remote_pointer event;CHECK(remote_display_input(&event));CHECK(event.epoch==42 && event.x==900 && event.y==556);remote_display_ack(16,42);CHECK(!pending && last_sequence==3 && remote_diag[8]==1);
    request_packet(3,3,ready,0,1);handle_frame(86);CHECK(!remote_display_input(&event) && remote_diag[8]==1);
    request_packet(3,4,ready,0,1);b16(rx+74,1024);handle_frame(86);CHECK(!pending);
    request_packet(2,5,ready,1,1);handle_frame(74);CHECK(le32(packet+46)==0x81);
    request_packet(1,6,ready,0,1);rx[24]^=1;uint32_t before=sent;handle_frame(74);CHECK(before==sent);
    request_packet(4,7,0,0,0);handle_frame(74);CHECK(!history_pending);
    request_packet(4,7,0,0,1);handle_frame(74);CHECK(remote_display_history_requested());
    uint8_t record[HISTORY_STRIDE];memset(record,0x23,sizeof(record));
    remote_display_history_publish(record,1,sizeof(record),3,42,4);CHECK(history_bytes==2104 && history_count==1 && history_id==1 && !history_pending);
    CHECK(le32(history)==0x31545348 && le32(history+12)==3 && !memcmp(history+32,record,sizeof(record)));
    request_packet(4,7,0,0,1);handle_frame(74);CHECK(!history_pending && le32(packet+46)==0x84 && history_id==1);
    request_packet(5,8,1,1400,1);handle_frame(74);CHECK(le32(packet+46)==0x85 && le32(packet+66)==1400 && !memcmp(packet+74,history+1400,704));
    before=sent;request_packet(5,9,2,0,1);handle_frame(74);CHECK(before==sent);
    request_packet(4,10,0,0,1);handle_frame(74);CHECK(remote_display_history_requested());remote_display_history_publish(record,0,sizeof(record),0,43,0);CHECK(history_bytes==32 && history_count==0 && history_id==2);
    request_packet(6,11,0,0,1);handle_frame(74);CHECK(le32(packet+46)==0x86 && le32(packet+62)==1 && le32(packet+66)==4 && le32(packet+70)==1 && !memcmp(packet+74,"CYEA",4));
    mock_wifi=0;request_packet(6,12,0,0,1);handle_frame(74);CHECK(le32(packet+62)==0 && le32(packet+66)==0 && packet_size==74);
    printf("PASS_REMOTE_DISPLAY checks=%u full RGB exact, long runs, packet bounds, IP checksum, cookie and duplicate input\n",checks);return 0;
}
