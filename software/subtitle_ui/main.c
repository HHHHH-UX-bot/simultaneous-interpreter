#include <stdint.h>
#include "framebuffer_hw.h"
#include "ui_assets.h"
#include "ui_demo.h"
#include "ui_render.h"
#ifdef Z7_MMU_CACHE
#include "../ps_bringup/cache.h"
#endif
#ifdef Z7_NANO_TOUCH
#include "ui_touch.h"
__attribute__((section(".touch_feedback"),used)) volatile struct touch_feedback touch_feedback;
#endif

struct display_result {
    uint32_t magic,state,sequence,phase,flips,current_buffer;
    uint32_t good_frames,underflows,format_errors,vdma_status,last_frame_pixels;
    uint32_t pixels_low,pixels_high,raw_frames,cpu_sctlr,error_code;
};
struct ui_information {
    uint32_t magic,sequence,snapshot_base,snapshot_stride,model_bytes,active_index;
    uint32_t elapsed_ms,generation,last_render_ms,max_render_ms,epoch,history_count;
    uint32_t final_count,rejected_events,command_ack,asset_checks;
};
struct ui_command_mailbox {
    uint32_t magic,request_sequence,expected_epoch,command,ack_sequence,result,observed_epoch,reserved;
};
struct ui_snapshot {
    uint32_t magic,version,bytes,model_bytes,elapsed_ms,requested_page,generation,reserved;
    struct ui_state state;
    struct ui_render_stats stats;
};
_Static_assert(sizeof(struct display_result)==64,"Display ABI");
_Static_assert(sizeof(struct ui_information)==64,"UI ABI");
#ifdef Z7_HISTORY_UI
#ifdef Z7_TINYQWEN
_Static_assert(sizeof(struct ui_state)==136000u,"Board Qwen model ABI (debug/conversation/glossary)");
_Static_assert(sizeof(struct ui_snapshot)==136064u,"Board Qwen snapshot ABI");
#else
_Static_assert(sizeof(struct ui_state)==135952u,"History/settings model ABI");
_Static_assert(sizeof(struct ui_snapshot)==136016u,"History/settings snapshot ABI");
#endif
#else
_Static_assert(sizeof(struct ui_state)==11392u,"ARM/native model ABI requires 32-bit enums");
_Static_assert(sizeof(struct ui_snapshot)==11456u,"ARM/native snapshot ABI");
#endif
__attribute__((section(".mailbox"),used)) volatile struct display_result report;
__attribute__((section(".ui_mailbox"),used)) volatile struct ui_information ui_info;
__attribute__((section(".ui_command"),used)) volatile struct ui_command_mailbox ui_command;
__attribute__((section(".ui_runtime"),used)) struct ui_snapshot snapshots[2];
static struct ui_state state;
static struct ui_demo_clock demo;
static uint64_t started_ticks;
static uint32_t generation,last_render_ms,max_render_ms;
static uint32_t format_baseline;
static const uint32_t guard_bases[]={FB_BASE0-256u,FB_BASE0+FB_HEIGHT*FB_STRIDE,
                                   FB_BASE1-256u,FB_BASE1+FB_HEIGHT*FB_STRIDE};

static inline uint32_t rd(uint32_t address) {return *(volatile uint32_t *)address;}
static inline void wr(uint32_t address,uint32_t value) {*(volatile uint32_t *)address=value;}
static inline void barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
static uint64_t timer(void) {
    uint32_t high,low;
    do {high=rd(0xf8f00204u);low=rd(0xf8f00200u);} while(high!=rd(0xf8f00204u));
    return ((uint64_t)high<<32)|low;
}
static uint32_t now_ms(void) {return (uint32_t)((timer()-started_ticks)/TIMER_TICKS_PER_MS);}
static void begin_report(void) {++report.sequence;barrier();}
static void end_report(void) {barrier();++report.sequence;barrier();}
static void update_stats(void) {
    report.good_frames=rd(VIDEO_BASE+0x0cu);report.underflows=rd(VIDEO_BASE+0x10u);
    report.format_errors=rd(VIDEO_BASE+0x14u);report.vdma_status=rd(VDMA_BASE+0x04u);
    report.last_frame_pixels=rd(VIDEO_BASE+0x20u);
    uint32_t high,low;
    do {high=rd(VIDEO_BASE+0x1cu);low=rd(VIDEO_BASE+0x18u);} while(high!=rd(VIDEO_BASE+0x1cu));
    report.pixels_low=low;report.pixels_high=high;report.raw_frames=rd(VIDEO_BASE+0x08u);
}
__attribute__((noreturn)) static void fail(uint32_t code) {
    begin_report();report.error_code=code;report.state=0xbad0bad0u;update_stats();end_report();
    for(;;) __asm__ volatile("wfe");
}
static void check_video(void) {
    if(rd(VDMA_BASE+0x04u)&0xf0u) fail(20);
    /* Underflow means the pixel pipeline starved: always fatal. */
    if(rd(VIDEO_BASE+0x10u)) fail(21);
    /* format_ok in framebuffer_video.v requires the AXI-Stream TUSER (SOF)
       marker to land exactly on the first active pixel. On the very first frame
       after the timing enable, the VDMA can still be re-aligning, so that frame
       is counted as one format error and drained until the next SOF. That is a
       start-up race, not a fault: three of four boots ended in fail(21) with a
       correct picture (good_frames climbing, full 614400-pixel frames).
       Latch the counter once the pipeline is warm and only fail on increases
       after that, so mid-stream format errors are still caught. */
    uint32_t format_errors=rd(VIDEO_BASE+0x14u);
    if(rd(VIDEO_BASE+0x0cu)<2u) return;
    if(!format_baseline) format_baseline=format_errors;
    if(format_errors>format_baseline) fail(21);
}
static void initialize_guards(void) {
    for(uint32_t g=0;g<4u;++g) for(uint32_t i=0;i<64u;++i)
        wr(guard_bases[g]+i*4u,0x9e3779b9u^(g*0x10001u)^i);
    barrier();
#ifdef Z7_MMU_CACHE
    for(uint32_t g=0;g<4u;++g) cache_clean_range((const void *)guard_bases[g],256u);
#endif
}
static void check_guards(void) {
    for(uint32_t g=0;g<4u;++g) for(uint32_t i=0;i<64u;++i)
        if(rd(guard_bases[g]+i*4u)!=(0x9e3779b9u^(g*0x10001u)^i)) fail(33);
}
static void wait_value(uint32_t address,uint32_t mask,uint32_t expected,uint32_t ms,uint32_t code) {
    uint32_t start=now_ms();
    for(uint32_t budget=0;budget<2000000u;++budget) {
        if((rd(address)&mask)==expected) return;
        if((uint32_t)(now_ms()-start)>ms) fail(code);
    }
    fail(code);
}
static void process_command(void) {
    uint32_t request=ui_command.request_sequence;
    if(request==ui_command.ack_sequence) return;
    barrier();uint32_t epoch=ui_command.expected_epoch,command=ui_command.command;
    int accepted=0;
    if(epoch==state.epoch && command>=1u && command<=UI_COMMAND_COUNT)
        accepted=ui_control(&state,(enum ui_command)(command-1u));
    if(accepted && state.session==UI_RUNNING && demo.phase==4u && command>=5u)
        demo.marked_ms=now_ms();
#ifdef UI_CONTROL_ACCEPTED
    if(accepted) UI_CONTROL_ACCEPTED((enum ui_command)(command-1u));
#endif
    ui_command.result=accepted?1u:2u;ui_command.observed_epoch=state.epoch;
    barrier();ui_command.ack_sequence=request;barrier();
}
static void process_controls(void) {
    process_command();
#ifdef Z7_REMOTE_DISPLAY
    _Static_assert(sizeof(struct ui_history_entry)==2072u,"Remote finalized history ABI");
    if(remote_display_history_requested())
        remote_display_history_publish(state.history,state.history_count,sizeof(state.history[0]),state.history_discarded,state.epoch,state.final_count);
    struct remote_pointer pointer;
    if(remote_display_input(&pointer)) {
        uint32_t accepted=pointer.epoch==ui_view_context(&state)?ui_touch_remote(&state,pointer.press_x,pointer.press_y,pointer.x,pointer.y):0;
        remote_display_ack(accepted,ui_view_context(&state));
#ifdef UI_CONTROL_ACCEPTED
        if(accepted) UI_CONTROL_ACCEPTED((enum ui_command)(accepted-1u));
#endif
    }
#endif
#ifdef Z7_NANO_TOUCH
    uint32_t command=ui_touch_poll((const volatile struct touch_link *)TOUCH_LINK_ADDRESS,&touch_feedback,&state);
    if(command>=5u && state.session==UI_RUNNING && demo.phase==4u) demo.marked_ms=now_ms();
#ifdef UI_CONTROL_ACCEPTED
    if(command) UI_CONTROL_ACCEPTED((enum ui_command)(command-1u));
#endif
#endif
}
static void draw(uint32_t index,uint32_t elapsed,uint32_t page) {
#ifdef Z7_HISTORY_UI
    ui_history_prepare(&state);
#endif
    struct ui_snapshot *snapshot=&snapshots[index];
    snapshot->magic=0x55495331u;snapshot->version=1;snapshot->bytes=sizeof(*snapshot);
    snapshot->model_bytes=sizeof(state);snapshot->elapsed_ms=elapsed;snapshot->requested_page=page;
    snapshot->generation=++generation;snapshot->reserved=0;snapshot->state=state;
#ifdef UI_LATENCY_READING
    /* Version 2 snapshots carry the L1/T1 cards; they are debug information. */
    int latency_cards=1;
#ifdef Z7_HISTORY_UI
    latency_cards=state.show_debug && !state.conversation;
#endif
    if(latency_cards) {snapshot->version=2;snapshot->reserved=UI_LATENCY_READING();}
#endif
    uint32_t start=now_ms();
    ui_render((uint32_t *)(index?FB_BASE1:FB_BASE0),&snapshot->state,elapsed,page,&snapshot->stats);
#ifdef UI_LATENCY_READING
    if(latency_cards) ui_render_latency((uint32_t *)(index?FB_BASE1:FB_BASE0),snapshot->reserved,&snapshot->stats);
#endif
    last_render_ms=now_ms()-start;if(last_render_ms>max_render_ms) max_render_ms=last_render_ms;
    if(snapshot->stats.missing_glyphs || snapshot->stats.clipped_glyph_pixels) fail(32);
    barrier();check_guards();
#ifdef Z7_MMU_CACHE
    /* Make the frame and its snapshot visible to the VDMA and the JTAG host. */
    cache_clean_range((const void *)(index?FB_BASE1:FB_BASE0),(unsigned)(FB_HEIGHT*FB_STRIDE));
    cache_clean_range(&snapshots[index],(unsigned)sizeof(struct ui_snapshot));
#endif
}
static void publish_ui(uint32_t active) {
#ifdef Z7_REMOTE_DISPLAY
    remote_display_present((const uint32_t *)(active?FB_BASE1:FB_BASE0),snapshots[active].generation,ui_view_context(&snapshots[active].state));
#endif
    const struct ui_state *shown=&snapshots[active].state;
    ++ui_info.sequence;barrier();
    ui_info.magic=0x55493131u;ui_info.snapshot_base=(uint32_t)snapshots;
    ui_info.snapshot_stride=sizeof(struct ui_snapshot);ui_info.model_bytes=sizeof(state);
    ui_info.active_index=active;ui_info.elapsed_ms=snapshots[active].elapsed_ms;
    ui_info.generation=snapshots[active].generation;ui_info.last_render_ms=last_render_ms;
    ui_info.max_render_ms=max_render_ms;ui_info.epoch=shown->epoch;
    ui_info.history_count=shown->history_count;ui_info.final_count=shown->final_count;
    ui_info.rejected_events=shown->rejected_events;ui_info.command_ack=ui_command.ack_sequence;
    ui_info.asset_checks=3;barrier();++ui_info.sequence;barrier();
}
int main(void) {
    volatile uint32_t *words=(volatile uint32_t *)&report;
    for(uint32_t i=0;i<sizeof(report)/4u;++i) words[i]=0;
    words=(volatile uint32_t *)&ui_info;for(uint32_t i=0;i<sizeof(ui_info)/4u;++i) words[i]=0;
    words=(volatile uint32_t *)&ui_command;for(uint32_t i=0;i<sizeof(ui_command)/4u;++i) words[i]=0;
    ui_command.magic=0x55494331u;
#ifdef Z7_NANO_TOUCH
    ui_touch_init((const volatile struct touch_link *)TOUCH_LINK_ADDRESS,&touch_feedback);
#endif
    begin_report();report.magic=0x46504231u;report.state=1;report.phase=1;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(report.cpu_sctlr));end_report();
#ifdef Z7_MMU_CACHE
    if((report.cpu_sctlr&0x1005u)!=0x1005u) fail(1);   /* MMU + I-cache + D-cache must be on */
#else
    if(report.cpu_sctlr&0x3805u) fail(1);
#endif
    wr(0xf8f00208u,1);barrier();started_ticks=timer();
    for(volatile uint32_t i=0;i<1000u;++i) {}
    if(timer()==started_ticks) fail(2);
#ifdef Z7_SD_BOOT
    sd_boot_power_screen();   /* JTAG flow powers the screen before the display starts */
#endif
    if(rd(VIDEO_BASE)!=0x46564231u || rd(VIDEO_BASE+0x24u)!=FB_HEIGHT*65536u+FB_WIDTH) fail(3);
    if(ui_font_count!=5u || !ui_demo_sample_count) fail(31);
    static const uint32_t sizes[]={14,18,28,36,44};
    for(uint32_t i=0;i<5u;++i) if(ui_fonts[i].size!=sizes[i] || !ui_fonts[i].count) fail(31);
    wr(VIDEO_BASE+4u,0);wait_value(VIDEO_BASE+4u,2u,0u,1000u,8);
    wr(VDMA_BASE,0);wait_value(VDMA_BASE+4u,1u,1u,1000u,9);
    wr(VDMA_BASE,4);wait_value(VDMA_BASE,4u,0u,1000u,4);
    initialize_guards();
    ui_demo_init(&demo,&state,now_ms());draw(0,now_ms(),0);draw(1,now_ms(),0);
    /* Accepted bring-up order: start the VDMA, write its frame geometry and
       addresses, then enable the video timing. Reordering this (configure while
       halted, then start) makes the first frames fail format_ok on this board. */
    wr(VDMA_BASE,1);wr(VDMA_BASE+0x5cu,FB_BASE0);wr(VDMA_BASE+0x60u,FB_BASE1);
    wr(VDMA_BASE+0x28u,0);wr(VDMA_BASE+0x58u,FB_STRIDE);wr(VDMA_BASE+0x54u,FB_WIDTH*4u);
    wr(VDMA_BASE+0x50u,FB_HEIGHT);barrier();wr(VIDEO_BASE+4u,1);barrier();
    uint32_t start=now_ms();
    while(rd(VIDEO_BASE+0x0cu)<2u) {check_video();if(now_ms()-start>1500u) fail(5);}
    begin_report();report.phase=2;report.current_buffer=0;end_report();
    uint32_t active=0,page_mark=now_ms(),previous_sequence=state.change_sequence,page=0;
    for(;;) {
#ifdef UI_IDLE_TICK
        UI_IDLE_TICK();active=(rd(VDMA_BASE+0x28u)>>16)&1u;
#endif
        check_video();process_controls();uint32_t elapsed=now_ms();ui_demo_tick(&demo,&state,elapsed);
        if(previous_sequence!=state.change_sequence) {previous_sequence=state.change_sequence;page_mark=elapsed;page=0;}
        if(state.session==UI_RUNNING) page=(elapsed-page_mark)/4000u;
        uint32_t inactive=active^1u;draw(inactive,elapsed,page);
        wr(VDMA_BASE+0x28u,inactive);barrier();
        wait_value(VDMA_BASE+0x28u,0x1f0000u,inactive<<16,1000u,6);active=inactive;
        publish_ui(active);begin_report();report.current_buffer=active;++report.flips;update_stats();
        if(report.flips>=4u) {report.phase=3;report.state=0x600d600du;}end_report();check_video();
#ifdef Z7_SD_BOOT
        if(report.flips>=4u) sd_boot_display_ready();   /* JTAG flow starts CPU1 at this point */
#endif
        if(report.last_frame_pixels!=FB_WIDTH*FB_HEIGHT) fail(7);
        uint32_t pause=now_ms();while(now_ms()-pause<500u) {
            check_video();process_controls();
#ifdef UI_BACKGROUND_TICK
            UI_BACKGROUND_TICK(&state,now_ms());
#endif
#ifdef Z7_NANO_TOUCH
            if(previous_sequence!=state.change_sequence) break;
#endif
        }
    }
}
