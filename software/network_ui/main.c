/* The accepted UI driver remains shared; network events replace its demo timer. */
#include "network.h"
#include "../subtitle_ui/ui_demo.h"
static uint32_t uart_read(uint32_t offset) {return *(volatile uint32_t *)(0x43c20000u+offset);}
static void uart_write(uint32_t offset,uint32_t value) {*(volatile uint32_t *)(0x43c20000u+offset)=value;}
#ifdef Z7_UART_921600
static uint32_t uart_accepted;
#endif
static void network_demo_init(struct ui_demo_clock *c,struct ui_state *s,uint32_t now) {
    ui_demo_init(c,s,now);wifi_probe_init(uart_read,uart_write,now);network_init(uart_read,uart_write);
#ifdef Z7_UART_921600
    uart_accepted=0;
#endif
}
static void network_demo_tick(struct ui_demo_clock *c,struct ui_state *s,uint32_t now) {
    wifi_probe_tick(now);
#ifdef Z7_UART_921600
    if(wifi_info.phase==2u) uart_accepted=1;
    if(uart_accepted) network_tick(s,now);
#else
    network_tick(s,now);
#endif
    if(!network_owns_ui()) ui_demo_tick(c,s,now);
}
#define ui_demo_init network_demo_init
#define ui_demo_tick network_demo_tick
#include "../subtitle_ui/main.c"
