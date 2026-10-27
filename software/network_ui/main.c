#include "language_profile.h"
/* The accepted UI driver remains shared; network events replace its demo timer. */
#include "network.h"
#include "stream_pilot.h"
#include "cloud_live.h"
#include "cloud_client.h"
#include "speech_playback.h"
#ifdef Z7_TINYQWEN
#include "tiny_board_llm.h"
#include "local_voice.h"
static void llm_idle_tick(void);
static uint32_t llm_restore_enabled;
#define UI_IDLE_TICK llm_idle_tick
#endif
#ifdef Z7_LOCAL_AI
#include "local_ai.h"
#endif
#include "wifi_recover.h"
#ifdef Z7_REMOTE_DISPLAY
#include "remote_display.h"
#endif
#ifdef Z7_LATENCY_PROBE
#include "../ps_bringup/cache.h"
#endif
#ifdef Z7_ARM_TLS
#include "arm_tls.h"
#endif
#ifdef Z7_SD_BOOT
#include "sd_boot_runtime.h"
#ifdef Z7_TINYQWEN
#include "sd_model.h"
#endif
#endif
#include "../subtitle_ui/ui_demo.h"
#include "../subtitle_ui/ui_render.h"
static void render_service(void);
/* Combined audio + ESP platform: audio registers at 0x43c20000, ESP UART at 0x43c30000. */
static uint32_t uart_read(uint32_t offset) {return *(volatile uint32_t *)(0x43c30000u+offset);}
static void uart_write(uint32_t offset,uint32_t value) {*(volatile uint32_t *)(0x43c30000u+offset)=value;}
__attribute__((section(".network_upload_control"),used)) volatile uint32_t network_upload_duration;
static uint32_t service_ms,upload_start,upload_sequence,upload_ready,upload_sent_ms;
static uint32_t gt_ticks(void) {return *(volatile uint32_t *)0xf8f00200u;}
#ifdef Z7_LATENCY_PROBE
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t service_perf[16];
static uint32_t perf_last,perf_clean;
static void service_measure(uint32_t slot,uint32_t start) {
    if(!cloud_client_ready()) return;
    uint32_t duration=gt_ticks()-start;
    service_perf[slot]+=duration;
    if(duration>service_perf[slot+1u]) service_perf[slot+1u]=duration;
}
#endif
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
            if(c=='O') phase=1;
            else if(phase==1u && c=='K') phase=2;
            else if(phase==2u && c=='\r') phase=3;
            else if(phase==3u && c=='\n') return 1;
            else phase=0;
        }
    }
    return 0;
}
/* UART_CUR survives a PS/PL reload. Probe every supported volatile rate so a
   new build can negotiate its selected rate after a diagnostic build. */
static void uart_link_setup(void) {
#ifdef Z7_STREAM_TRANSPORT
    const uint32_t rates[]={WIFI_STREAM_DIVISOR,434u,50u,30u,25u,54u,109u};
    for(uint32_t i=0;i<sizeof(rates)/sizeof(rates[0]);++i) {
        uart_flush();uart_write(8,rates[i]);
        (void)uart_at("AT\r\n",150);
        if(!uart_at("AT\r\n",300)) continue;
        if(rates[i]==WIFI_STREAM_DIVISOR) {uart_flush();return;}
        if(!uart_at(WIFI_STREAM_UART_COMMAND,500)) return;
        for(volatile uint32_t wait=0;wait<4000000u;++wait) {}
        uart_flush();uart_write(8,WIFI_STREAM_DIVISOR);
        /* Failed baud probes can latch framing errors before a verified AT.
           Clear only here, before any session/audio traffic exists. */
        if(uart_at("AT\r\n",300)) {uart_flush();return;}
        uart_flush();uart_write(8,rates[i]);return;
    }
#else
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
#endif
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
#ifdef Z7_ARM_TLS
    arm_tls_init();
#endif
    speech_playback_init();
#ifdef Z7_LOCAL_AI
    local_ai_init(s);
#endif
#ifdef Z7_REMOTE_DISPLAY
    remote_display_init(now);
#endif
#ifdef Z7_TINYQWEN
    tbl_init();
#endif
    service_ms=now-1u;network_upload_duration=upload_sequence=upload_ready=0;
#ifdef Z7_LATENCY_PROBE
    for(uint32_t i=0;i<16u;++i) service_perf[i]=0;
    perf_last=perf_clean=0;
#endif
    ui_render_set_service(render_service);
#ifdef Z7_UART_921600
    uart_accepted=0;
#endif
}
static void network_background_tick(struct ui_state *s,uint32_t now) {
    if(now-service_ms<1u) return;
    service_ms=now;
#ifdef Z7_SD_BOOT
    sd_boot_tick();
#endif
#ifdef Z7_LOCAL_AI
    local_ai_tick(now);
#endif
#ifdef Z7_LATENCY_PROBE
    uint32_t perf_start=gt_ticks(),perf_stage=perf_start;
    uint32_t gap=perf_start-perf_last;perf_last=perf_start;
    if(service_perf[0] && cloud_client_ready() && gap>service_perf[1]) service_perf[1]=gap;
    ++service_perf[0];
#endif
#ifdef Z7_HISTORY_UI
    static const uint32_t speeds[]={80,100,120};
    speech_playback_settings(s->tts_enabled,s->output_muted?0u:s->output_volume,
                             speeds[s->rate_index<3u?s->rate_index:1u]);
#endif
    speech_playback_tick(now);
#ifdef Z7_HISTORY_UI
    struct speech_status voice;speech_playback_status(&voice);
    s->voice_status=voice.state;s->voice_latency_ms=voice.latency_ms;
    s->voice_latency_valid=voice.latency_valid;s->voice_buffer_ms=voice.buffer_ms;
#endif
#ifdef Z7_ARM_TLS
    network_cloud_run(
#ifdef Z7_TINYQWEN
        s->model_mode?local_voice_network_run():
#endif
        cloud_live_capture_enabled());
#endif
#ifdef Z7_UART_921600
    if(wifi_info.phase==2u) uart_accepted=1;
    if(uart_accepted) network_tick(s,now);
#else
    network_tick(s,now);
    /* A network fault (AT timeout/overflow) ends the stream; stop queueing
       audio so a dead link is visible instead of silently dropping frames. */
    if(network_info.phase==6u) cloud_client_stop();
#endif
#ifdef Z7_HISTORY_UI
    uint8_t wifi_name[33];uint32_t wifi_connected=network_wifi_name(wifi_name);
    ui_wifi_identity(s,wifi_connected,wifi_name);
#endif
#ifdef Z7_LOCAL_AI
#ifndef Z7_TINYQWEN
    if(!s->model_mode)
#endif
#endif
    cloud_live_tick(now);
#ifdef Z7_LATENCY_PROBE
    service_measure(2u,perf_stage);perf_stage=gt_ticks();
#endif
#ifdef Z7_ARM_TLS
    int cloud_run=
#ifdef Z7_TINYQWEN
        s->model_mode?local_voice_network_run():
#endif
        cloud_live_capture_enabled();
    if(cloud_run || network_info.phase!=8u) arm_tls_tick(now);
    if(cloud_run && arm_tls_ready()) cloud_client_start(now);
#else
    if(network_info.phase==8u) cloud_client_start(now);
#endif
#ifdef Z7_LATENCY_PROBE
    service_measure(4u,perf_stage);perf_stage=gt_ticks();
#endif
#ifdef Z7_LOCAL_AI
#ifndef Z7_TINYQWEN
    if(!s->model_mode)
#endif
#endif
    cloud_client_tick(now);
#ifdef Z7_LATENCY_PROBE
    service_measure(6u,perf_stage);service_measure(8u,perf_start);
    if(now-perf_clean>=200u) {perf_clean=now;cache_clean_range((const void *)service_perf,sizeof(service_perf));}
#endif
#ifdef Z7_ARM_TLS
    cloud_live_connection(cloud_client_ready());
#endif
    #ifdef Z7_REMOTE_DISPLAY
    remote_display_tick(now);
#ifdef Z7_TINYQWEN
#ifdef Z7_SD_BOOT
    /* An SD boot starts with empty windows; the TF card loader owns them. */
    if(sd_boot_active()) sd_model_tick(now,s->session==UI_RUNNING);
    else
#endif
    if(llm_restore_enabled>=5u)remote_llm_restore_tick();
#endif
#endif
    if(stream_pilot.request || stream_pilot.phase) stream_pilot_tick(now);
    else if(!cloud_live_owns_ui()
#ifdef Z7_ARM_TLS
            && !arm_tls_active()
#endif
    ) upload_tick(now);
}
static void network_demo_tick(struct ui_demo_clock *c,struct ui_state *s,uint32_t now) {
    network_background_tick(s,now);
#if defined(Z7_BILINGUAL) || defined(Z7_SOURCE_EN)
    /* Leave the bilingual display empty until real cloud text arrives. */
    (void)c;
#else
    if(!network_owns_ui() && !cloud_live_owns_ui()) ui_demo_tick(c,s,now);
#endif
}
#ifdef Z7_LATENCY_UI
static uint32_t cloud_latency_read(void) {
    static uint32_t shown;
    volatile uint32_t *audio=(volatile uint32_t *)0x43c20000u;
    if(!(audio[0x2cu/4u]&4u)) {
        uint32_t text=audio[0x88u/4u];
        shown=ui_latency_pack(audio[0x08u/4u],audio[0x04u/4u]&1u,text&0xffffu,text&0x10000u);
    }
    return shown;
}
#define UI_LATENCY_READING cloud_latency_read
#endif
#define ui_demo_init network_demo_init
#define ui_demo_tick network_demo_tick
#define UI_BACKGROUND_TICK network_background_tick
#ifdef Z7_ARM_TLS
#ifdef Z7_LOCAL_AI
#define UI_CONTROL_ACCEPTED local_ai_control
#else
#define UI_CONTROL_ACCEPTED cloud_live_control
#endif
#endif
#include "../subtitle_ui/main.c"
static void render_service(void) {network_background_tick(&state,now_ms());}
#ifdef Z7_TINYQWEN
static void llm_idle_tick(void) {
    if(llm_restore_enabled<5u)++llm_restore_enabled;
    uint32_t flags=remote_llm_asset_flags();
    tbl_set_assets_ready((flags&1u)!=0u,(flags&2u)!=0u);tbl_tick();
}
void tbl_pump(void) {
    static uint32_t pumping,last_sequence,last_draw;
    if(pumping)return;
    pumping=1;uint32_t now=now_ms();
    check_video();process_controls();network_background_tick(&state,now);
    if(state.change_sequence!=last_sequence && now-last_draw>=200u) {
        uint32_t active=(rd(VDMA_BASE+0x28u)>>16)&1u,inactive=active^1u;
        draw(inactive,now,0);wr(VDMA_BASE+0x28u,inactive);barrier();
        wait_value(VDMA_BASE+0x28u,0x1f0000u,inactive<<16,1000u,6);
        publish_ui(inactive);begin_report();report.current_buffer=inactive;++report.flips;update_stats();end_report();
        last_sequence=state.change_sequence;last_draw=now_ms();
    }
    pumping=0;
}
#endif
