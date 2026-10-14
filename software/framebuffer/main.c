#include <stdint.h>
#include "framebuffer_hw.h"

struct display_result {
    uint32_t magic,state,sequence,phase,flips,current_buffer;
    uint32_t good_frames,underflows,format_errors,vdma_status,last_frame_pixels;
    uint32_t pixels_low,pixels_high,raw_frames,cpu_sctlr,error_code;
};
__attribute__((section(".mailbox"),used)) volatile struct display_result report;

static inline uint32_t rd(uint32_t addr) {return *(volatile uint32_t *)addr;}
static inline void wr(uint32_t addr,uint32_t value) {*(volatile uint32_t *)addr=value;}
static inline void barrier(void) {__asm__ volatile("dsb sy" ::: "memory");}
static uint32_t ticks(void) {return rd(0xf8f00200u);}

static void begin_report(void) {report.sequence++; barrier();}
static void end_report(void) {barrier(); report.sequence++; barrier();}
static void update_stats(void) {
    report.good_frames=rd(VIDEO_BASE+0x0cu);
    report.underflows=rd(VIDEO_BASE+0x10u);
    report.format_errors=rd(VIDEO_BASE+0x14u);
    report.vdma_status=rd(VDMA_BASE+0x04u);
    report.last_frame_pixels=rd(VIDEO_BASE+0x20u);
    uint32_t high,low;
    do {high=rd(VIDEO_BASE+0x1cu); low=rd(VIDEO_BASE+0x18u);} while (high!=rd(VIDEO_BASE+0x1cu));
    report.pixels_low=low; report.pixels_high=high;
    report.raw_frames=rd(VIDEO_BASE+0x08u);
}
static void snapshot(void) {begin_report(); update_stats(); end_report();}
__attribute__((noreturn)) static void fail(uint32_t code) {
    begin_report(); report.error_code=code; report.state=0xbad0bad0u; end_report();
    for (;;) __asm__ volatile("wfe");
}
static void wait_value(uint32_t addr,uint32_t mask,uint32_t expected,uint32_t timeout_ms,uint32_t code) {
    uint32_t start=ticks();
    for (uint32_t budget=0; budget<2000000u; ++budget) {
        if ((rd(addr)&mask)==expected) return;
        if ((uint32_t)(ticks()-start)>timeout_ms*TIMER_TICKS_PER_MS) fail(code);
    }
    fail(code);
}
static void check_video(void) {
    if (rd(VDMA_BASE+0x04u)&0xf0u) {snapshot(); fail(20);}
    if (rd(VIDEO_BASE+0x10u) || rd(VIDEO_BASE+0x14u)) {snapshot(); fail(21);}
}
static void draw(uint32_t base,uint32_t box_x) {
    static const uint32_t bars[8]={0xffffffu,0xffff00u,0x00ffffu,0x00ff00u,
                                   0xff00ffu,0xff0000u,0x0000ffu,0x000000u};
    for (uint32_t y=0;y<FB_HEIGHT;++y) {
        volatile uint32_t *line=(volatile uint32_t *)(base+y*FB_STRIDE);
        for (uint32_t x=0;x<FB_WIDTH;++x) {
            uint32_t color=(y<300u)?bars[x>>7u]:((((x>>5u)+(y>>5u))&1u)?0xffffffu:0x202020u);
            if (x<3u || x>=FB_WIDTH-3u || y<3u || y>=FB_HEIGHT-3u) color=0xffffffu;
            if (x>=box_x && x<box_x+64u && y>=420u && y<484u) color=0xff7d32u;
            line[x]=color;
        }
    }
    barrier();
}

int main(void) {
    volatile uint32_t *words=(volatile uint32_t *)&report;
    for (uint32_t i=0;i<sizeof(report)/4u;++i) words[i]=0;
    begin_report(); report.magic=0x46504231u; report.state=1; report.phase=1;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(report.cpu_sctlr));
    end_report();
    if (report.cpu_sctlr&0x3805u) fail(1);
    wr(0xf8f00208u,1u); barrier();
    uint32_t timer_start=ticks();
    for (volatile uint32_t i=0;i<1000u;++i) {}
    if (ticks()==timer_start) fail(2);
    if (rd(VIDEO_BASE)!=0x46564231u || rd(VIDEO_BASE+0x24u)!=FB_HEIGHT*65536u+FB_WIDTH) fail(3);
    wr(VIDEO_BASE+0x04u,0u);
    wait_value(VIDEO_BASE+0x04u,2u,0u,1000u,8);
    wr(VDMA_BASE+0x00u,0u);
    wait_value(VDMA_BASE+0x04u,1u,1u,1000u,9);
    wr(VDMA_BASE+0x00u,4u);
    wait_value(VDMA_BASE,4u,0u,1000u,4);
    draw(FB_BASE0,48u); draw(FB_BASE1,80u);
    wr(VDMA_BASE+0x00u,1u); // Run, park mode; no internal genlock.
    wr(VDMA_BASE+0x5cu,FB_BASE0); wr(VDMA_BASE+0x60u,FB_BASE1);
    wr(VDMA_BASE+0x28u,0u);
    wr(VDMA_BASE+0x58u,FB_STRIDE);
    wr(VDMA_BASE+0x54u,FB_WIDTH*4u);
    wr(VDMA_BASE+0x50u,FB_HEIGHT); // VSIZE commits parameters last (PG020).
    barrier(); wr(VIDEO_BASE+0x04u,1u); barrier();
    uint32_t start=ticks();
    while (rd(VIDEO_BASE+0x0cu)<2u) {
        check_video();
        if ((uint32_t)(ticks()-start)>1500u*TIMER_TICKS_PER_MS) fail(5);
    }
    begin_report(); report.phase=2; report.current_buffer=0; end_report();
    uint32_t active=0,box_x=80u;
    for (;;) {
        check_video();
        uint32_t inactive=active^1u;
        draw(inactive?FB_BASE1:FB_BASE0,box_x);
        // Only request after all writes reach DDR. VDMA selects on external
        // frame sync; confirm it has started reading the new frame store.
        wr(VDMA_BASE+0x28u,inactive); barrier();
        wait_value(VDMA_BASE+0x28u,0x1f0000u,inactive<<16u,1000u,6);
        active=inactive;
        begin_report(); report.current_buffer=active; report.flips++; update_stats();
        if (report.flips>=4u) {report.phase=3; report.state=0x600d600du;}
        end_report(); check_video();
        if (report.last_frame_pixels!=FB_WIDTH*FB_HEIGHT) fail(7);
        box_x+=32u; if (box_x>FB_WIDTH-96u) box_x=32u;
        uint32_t pause_start=ticks();
        while ((uint32_t)(ticks()-pause_start)<500u*TIMER_TICKS_PER_MS) check_video();
    }
}
