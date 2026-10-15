/* The accepted UI driver remains shared; network events replace its demo timer. */
#include "network.h"
#include "stream_pilot.h"
#include "cloud_live.h"
#include "../subtitle_ui/ui_demo.h"
#include "../subtitle_ui/ui_render.h"
static void render_service(void);
static uint32_t uart_read(uint32_t offset) {return *(volatile uint32_t *)(0x43c20000u+offset);}
static void uart_write(uint32_t offset,uint32_t value) {*(volatile uint32_t *)(0x43c20000u+offset)=value;}
__attribute__((section(".network_upload_control"),used)) volatile uint32_t network_upload_duration;
static uint32_t service_ms,wifi_service_ms,upload_start,upload_sequence,upload_ready,upload_sent_ms;
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
    ui_demo_init(c,s,now);wifi_probe_init(uart_read,uart_write,now);network_init(uart_read,uart_write);
    stream_pilot_init((const uint8_t *)STREAM_PILOT_TX_ADDRESS,(uint8_t *)STREAM_PILOT_RX_ADDRESS);
    cloud_live_init(s);
    service_ms=now-1u;wifi_service_ms=now-200u;network_upload_duration=upload_sequence=upload_ready=0;
    ui_render_set_service(render_service);
#ifdef Z7_UART_921600
    uart_accepted=0;
#endif
}
static void network_background_tick(struct ui_state *s,uint32_t now) {
    if(now-service_ms<1u) return;
    service_ms=now;
    if(wifi_info.phase!=2u || now-wifi_service_ms>=200u) {wifi_probe_tick(now);wifi_service_ms=now;}
#ifdef Z7_UART_921600
    if(wifi_info.phase==2u) uart_accepted=1;
    if(uart_accepted) network_tick(s,now);
#else
    network_tick(s,now);
#endif
    cloud_live_tick(now);
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
