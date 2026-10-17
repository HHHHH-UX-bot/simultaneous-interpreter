/* The accepted UI driver remains shared; network events replace its demo timer. */
#include "network.h"
#include "stream_pilot.h"
#include "cloud_live.h"
#include "cloud_client.h"
#include "../subtitle_ui/ui_demo.h"
#include "../subtitle_ui/ui_render.h"
static void render_service(void);
/* Combined audio + ESP platform: audio registers at 0x43c20000, ESP UART at 0x43c30000. */
static uint32_t uart_read(uint32_t offset) {return *(volatile uint32_t *)(0x43c30000u+offset);}
static void uart_write(uint32_t offset,uint32_t value) {*(volatile uint32_t *)(0x43c30000u+offset)=value;}
__attribute__((section(".network_upload_control"),used)) volatile uint32_t network_upload_duration;
static uint32_t service_ms,upload_start,upload_sequence,upload_ready,upload_sent_ms;
static uint32_t gt_ticks(void) {return *(volatile uint32_t *)0xf8f00200u;}
/* The ESP AT firmware corrupts the first byte it receives after a long idle, so
   the link is warmed once with a disposable AT. */
static void uart_flush(void) {uart_write(12,15);}
static int uart_at(const char *text,uint32_t window_ms) {
    while(uart_read(4)&1u) (void)uart_read(20);
    for(uint32_t i=0;text[i];++i) {
        for(uint32_t budget=0;budget<200000u && !(uart_read(4)&2u);++budget) {}
        uart_write(16,(uint8_t)text[i]);
    }
    uint32_t start=gt_ticks(),limit=window_ms*333333u,phase=0;
    while((uint32_t)(gt_ticks()-start)<limit) {
        if(uart_read(4)&1u) {
            char c=(char)(uart_read(20)&0xffu);
            if(c=='O') phase=1;else if(phase==1u && c=='K') return 1;else phase=0;
        }
    }
    return 0;
}
/* The cloud audio path needs far more than 115200 baud: 16 kHz 16-bit PCM in
   base64 is about 46 kB/s. Negotiate the module up to 921600 (divider 54) and
   fall back to the previous rate if the module does not follow. */
static void uart_link_setup(void) {
    uart_flush();
    (void)uart_at("AT\r\n",150);                 // warm, ignore the answer
    if(uart_at("AT\r\n",300)) {
        if(!uart_at("AT+UART_CUR=921600,8,1,0,0\r\n",500)) return;   // stay at 115200
        for(volatile uint32_t i=0;i<4000000u;++i) {}
        uart_flush();uart_write(8,54);
        for(volatile uint32_t i=0;i<400000u;++i) {}
        uart_flush();
        if(uart_at("AT\r\n",300)) return;        // 921600 verified
        uart_flush();uart_write(8,434);          // module did not follow: back off
        for(volatile uint32_t i=0;i<400000u;++i) {}
        uart_flush();(void)uart_at("AT\r\n",300);
        return;
    }
    /* A previous run may have left the module at 921600. */
    uart_flush();uart_write(8,54);
    for(volatile uint32_t i=0;i<400000u;++i) {}
    uart_flush();
    if(uart_at("AT\r\n",300)) return;
    uart_flush();uart_write(8,434);
    for(volatile uint32_t i=0;i<400000u;++i) {}
    uart_flush();(void)uart_at("AT\r\n",300);
}
static uint8_t upload[1536];
static void put_word(uint32_t at,uint32_t value) {
    for(uint32_t i=0;i<4;++i) upload[at+i]=(uint8_t)(value>>(8*i));
}
static void upload_tick(uint32_t now) {
    if(!network_upload_duration || network_info.phase!=4) {
        if(!network_upload_duration && network_info.phase==4) network_stop();
        if(upload_sequence && network_info.phase!=4) network_upload_duration=0;
        upload_start=now;upload_sequence=upload_ready=0;return;
    }
    if(now-upload_start>=network_upload_duration) {network_upload_duration=0;network_stop();return;}
    /* Low-rate fault isolation on the four-wire link: 25.6 kB/s. */
    if(network_config.reserved && upload_sequence && now-upload_sent_ms<60u) return;
    if(!upload_ready) {
        put_word(0,0x31444e42u);put_word(4,upload_sequence);put_word(8,sizeof(upload));put_word(12,now);
        for(uint32_t i=16;i<sizeof(upload);++i) upload[i]=(uint8_t)(upload_sequence*17u+i*13u);
        upload_ready=1;
    }
    if(network_send(upload,sizeof(upload))) {++upload_sequence;upload_ready=0;upload_sent_ms=now;}
}
#ifdef Z7_UART_921600
static uint32_t uart_accepted;
#endif
static void network_demo_init(struct ui_demo_clock *c,struct ui_state *s,uint32_t now) {
    ui_demo_init(c,s,now);
    /* The network channel owns the AT link from the first byte, so skip the
       boot AT probe entirely: its warm-up byte and reply would otherwise race
       the first cloud setup exchange. The status window still advertises the
       link as available for network_config. */
    volatile uint32_t *w=(volatile uint32_t *)&wifi_info;
    for(uint32_t i=0;i<16u;++i) w[i]=0;
    wifi_info.magic=0x57505331u;wifi_info.sequence=1;wifi_info.phase=2;
    network_init(uart_read,uart_write);
    uart_link_setup();
    stream_pilot_init((const uint8_t *)STREAM_PILOT_TX_ADDRESS,(uint8_t *)STREAM_PILOT_RX_ADDRESS);
    cloud_live_init(s);
    cloud_client_init();
    service_ms=now-1u;network_upload_duration=upload_sequence=upload_ready=0;
    ui_render_set_service(render_service);
#ifdef Z7_UART_921600
    uart_accepted=0;
#endif
}
static void network_background_tick(struct ui_state *s,uint32_t now) {
    if(now-service_ms<1u) return;
    service_ms=now;
#ifdef Z7_UART_921600
    if(wifi_info.phase==2u) uart_accepted=1;
    if(uart_accepted) network_tick(s,now);
#else
    network_tick(s,now);
#endif
    cloud_live_tick(now);
    if(network_info.phase==8u) cloud_client_start(now);
    cloud_client_tick(now);
    if(stream_pilot.request || stream_pilot.phase) stream_pilot_tick(now);else upload_tick(now);
}
static void network_demo_tick(struct ui_demo_clock *c,struct ui_state *s,uint32_t now) {
    network_background_tick(s,now);
    if(!network_owns_ui() && !cloud_live_owns_ui()) ui_demo_tick(c,s,now);
}
#define ui_demo_init network_demo_init
#define ui_demo_tick network_demo_tick
#define UI_BACKGROUND_TICK network_background_tick
#include "../subtitle_ui/main.c"
static void render_service(void) {network_background_tick(&state,now_ms());}
