/* SD-card boot only (SLCR BOOT_MODE = SD). Takes over the two debugger duties
   of the JTAG flow: start the CPU1 touch host the stub placed in OCM, and apply
   the runtime configuration the FSBL hook copied from z7cfg.bin. A JTAG boot
   reads BOOT_MODE = JTAG and none of this runs. */
#include <stdint.h>
#include "network.h"
#include "cloud_client.h"
#include "sd_boot_runtime.h"
#include "framebuffer_hw.h"
#include "../sd_boot/sd_boot.h"
#include "mbedtls/sha256.h"

struct tls_seed {uint32_t magic,epoch;uint8_t bytes[64];};
extern volatile struct tls_seed arm_tls_seed;
volatile uint32_t sd_boot_diag[8];   /* magic, host started, config state, config bytes, noise samples */

static uint32_t host_started,config_state;

static uint32_t rd(uint32_t address) {return *(volatile uint32_t *)address;}
static void barrier(void) {__asm__ volatile("dsb sy":::"memory");}

int sd_boot_active(void) {return (rd(0xf800025cu)&7u)==5u;}

static void wr(uint32_t address,uint32_t value) {*(volatile uint32_t *)address=value;}
static void delay_ms(uint32_t ms) {
    uint32_t start=rd(0xf8f00200u);
    while(rd(0xf8f00200u)-start<ms*TIMER_TICKS_PER_MS) {}
}
/* The JTAG loader's usb_port_power (scripts/z7_nano_audio.tcl), done before
   the CPU1 host starts: one PHY reset on MIO46, controller reset, host mode
   and port power. Without this first reset the host's own reset after power-on
   leaves the USB3320 ULPI viewport unanswered (host error 6). */
static void usb_preconditions(void) {
    const uint32_t pin=1u<<(46u-32u);
    wr(0xf8000008u,0xdf0du);
    wr(0xf80007b8u,0x00001200u);
    wr(0xf800012cu,rd(0xf800012cu)|4u);
    wr(0xf8000004u,0x767bu);
    wr(0xe000a008u,(~pin&0xffffu)<<16);
    wr(0xe000a244u,rd(0xe000a244u)|pin);
    wr(0xe000a248u,rd(0xe000a248u)|pin);
    delay_ms(10);
    wr(0xe000a008u,((~pin&0xffffu)<<16)|pin);
    delay_ms(20);
    wr(0xe0002140u,2);
    delay_ms(5);
    wr(0xe00021a8u,3);
    wr(0xe0002184u,(rd(0xe0002184u)&~0x2au)|0x1000u);
    delay_ms(20);
}

static uint64_t global_ticks(void) {
    uint32_t high,low;
    do {high=rd(0xf8f00204u);low=rd(0xf8f00200u);} while(high!=rd(0xf8f00204u));
    return ((uint64_t)high<<32)|low;
}
static uint64_t screen_powered_at;
static uint32_t screen_powered;

/* Called before the display starts: the JTAG flow also powers the screen
   (USB VBUS) before any HDMI output, and its touch controller only reports
   when it has been powered for seconds before the host enumerates it. */
void sd_boot_power_screen(void) {
    if(screen_powered || !sd_boot_active() || rd(0xe0002100u)!=0x01000040u) return;
    usb_preconditions();
    screen_powered_at=global_ticks();screen_powered=1;
}

void sd_boot_display_ready(void) {
    if(host_started || !sd_boot_active()) return;
    if(!screen_powered) {sd_boot_diag[1]=3;host_started=1;return;}
    if(global_ticks()-screen_powered_at<5000ull*TIMER_TICKS_PER_MS) return;
    host_started=1;sd_boot_diag[0]=0x44425353u;
    const volatile struct sd_boot_info *info=(const volatile struct sd_boot_info *)SD_BOOT_INFO_ADDRESS;
    if(info->magic!=SD_BOOT_INFO_MAGIC || !info->cpu1_entry) {sd_boot_diag[1]=2;return;}
    /* CPU1 waits in the BootROM WFE loop for a non-zero 0xFFFFFFF0 (Device memory). */
    *(volatile uint32_t *)0xfffffff0u=info->cpu1_entry;
    barrier();__asm__ volatile("sev");
    sd_boot_diag[1]=1;
}

static uint32_t crc32(const uint8_t *p,uint32_t n) {
    uint32_t crc=0xffffffffu;
    while(n--) {crc^=*p++;for(uint32_t k=0;k<8;++k) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return ~crc;
}

/* The file's seed is the same on every boot, so mix in this boot's microphone
   noise and timer jitter: 2048 live 48 kHz samples from the audio sample tap. */
static void fresh_seed(const uint8_t stored[64],uint8_t out[64]) {
    mbedtls_sha256_context a,b;
    mbedtls_sha256_init(&a);mbedtls_sha256_init(&b);
    mbedtls_sha256_starts(&a,0);mbedtls_sha256_starts(&b,0);
    static const uint8_t label_a[]="z7-sd-seed-a",label_b[]="z7-sd-seed-b";
    mbedtls_sha256_update(&a,label_a,sizeof(label_a));mbedtls_sha256_update(&b,label_b,sizeof(label_b));
    mbedtls_sha256_update(&a,stored,64);mbedtls_sha256_update(&b,stored,64);
    uint32_t last=rd(0x43c20080u)&0x10000u,taken=0;
    for(uint32_t spins=0;taken<2048u && spins<20000000u;++spins) {
        uint32_t sample=rd(0x43c20080u);
        if((sample&0x10000u)==last) continue;
        last=sample&0x10000u;++taken;
        uint32_t words[2]={sample,rd(0xf8f00200u)};
        mbedtls_sha256_update(&a,(const uint8_t *)words,sizeof(words));
        mbedtls_sha256_update(&b,(const uint8_t *)words,sizeof(words));
    }
    mbedtls_sha256_finish(&a,out);mbedtls_sha256_finish(&b,out+32);
    mbedtls_sha256_free(&a);mbedtls_sha256_free(&b);
    sd_boot_diag[4]=taken;   /* 2048 when the codec clock was live */
}

void sd_boot_tick(void) {
    if(config_state || !host_started) return;
    if(network_info.phase==1u) return;   /* same idle wait as configure_cloud_runtime.py */
    volatile struct sd_boot_config *file=(volatile struct sd_boot_config *)SD_BOOT_CONFIG_ADDRESS;
    struct sd_boot_config cfg;
    uint8_t *bytes=(uint8_t *)&cfg;
    for(uint32_t i=0;i<sizeof(cfg);++i) bytes[i]=((volatile uint8_t *)file)[i];
    for(uint32_t i=0;i<SD_BOOT_CONFIG_BYTES/4u;++i) ((volatile uint32_t *)file)[i]=0;
    config_state=1;sd_boot_diag[3]=cfg.bytes;
    if(cfg.magic!=SD_BOOT_CONFIG_MAGIC || cfg.version!=1u || cfg.bytes!=sizeof(cfg) ||
       crc32((const uint8_t *)cfg.net,sizeof(cfg)-16u)!=cfg.crc32) {
        sd_boot_diag[2]=cfg.magic==SD_BOOT_CONFIG_MAGIC?3u:2u;
        for(uint32_t i=0;i<sizeof(cfg);++i) ((volatile uint8_t *)bytes)[i]=0;
        return;
    }
    uint8_t seed[64];fresh_seed(cfg.seed,seed);
    volatile uint32_t *net=(volatile uint32_t *)&network_config;
    for(uint32_t i=0;i<32u;++i) if(i!=1u) net[i]=cfg.net[i];
    volatile uint32_t *cloud=(volatile uint32_t *)&cloud_client_config;
    for(uint32_t i=0;i<85u;++i) cloud[i]=cfg.cloud[i];
    arm_tls_seed.epoch=cfg.epoch;
    for(uint32_t i=0;i<64u;++i) arm_tls_seed.bytes[i]=seed[i];
    barrier();arm_tls_seed.magic=0x31444553u;
    barrier();network_config.request_sequence=network_config.request_sequence+1u;barrier();
    for(uint32_t i=0;i<sizeof(cfg);++i) ((volatile uint8_t *)bytes)[i]=0;
    for(uint32_t i=0;i<64u;++i) ((volatile uint8_t *)seed)[i]=0;
    sd_boot_diag[2]=1;
}
