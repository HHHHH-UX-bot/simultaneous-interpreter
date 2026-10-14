#include <stdint.h>

#define TEST_BASE 0x01000000u
#define TEST_WORDS (0x00400000u / 4u)
#define MAGIC 0x50534431u
#define PASS 0x600d600du
#define FAIL 0xbad0bad0u

struct result {
    uint32_t magic, state, phase, words_checked;
    uint32_t fail_address, expected, actual, checksum;
    uint32_t test_base, test_bytes, ddr_control, ddr_status, cpu_sctlr;
};
__attribute__((section(".mailbox"), used)) volatile struct result report;

static inline void sync_memory(void) { __asm__ volatile("dsb sy" ::: "memory"); }

__attribute__((noinline, noreturn)) void test_done(void) {
    sync_memory();
    for (;;) __asm__ volatile("wfe");
}

static void check(volatile uint32_t *address, uint32_t expected) {
    uint32_t actual = *address;
    report.words_checked++;
    report.checksum ^= actual;
    if (actual != expected) {
        report.fail_address = (uint32_t)address;
        report.expected = expected;
        report.actual = actual;
        sync_memory();
        report.state = FAIL;
        sync_memory();
        test_done();
    }
}

int main(void) {
    volatile uint32_t *r = (volatile uint32_t *)&report;
    for (uint32_t i = 0; i < sizeof(report) / 4u; ++i) r[i] = 0;
    report.magic = MAGIC;
    report.state = 1;
    report.test_base = TEST_BASE;
    report.test_bytes = TEST_WORDS * 4u;
    report.ddr_control = *(volatile uint32_t *)0xf8006000u;
    report.ddr_status = *(volatile uint32_t *)0xf8006054u;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(report.cpu_sctlr));
    volatile uint32_t *mem = (volatile uint32_t *)TEST_BASE;

    report.phase = 1;
    for (uint32_t bit = 0; bit < 32; ++bit) {
        uint32_t value = 1u << bit;
        *mem = value; sync_memory(); check(mem, value);
        *mem = ~value; sync_memory(); check(mem, ~value);
    }

    static const uint32_t patterns[] = {0u, 0xffffffffu, 0xaaaaaaaau, 0x55555555u};
    for (uint32_t p = 0; p < 4; ++p) {
        report.phase = 2u + p;
        for (uint32_t i = 0; i < TEST_WORDS; ++i) mem[i] = patterns[p];
        sync_memory();
        for (uint32_t i = 0; i < TEST_WORDS; ++i) check(mem + i, patterns[p]);
    }
    report.phase = 6;
    for (uint32_t i = 0; i < TEST_WORDS; ++i) mem[i] = (TEST_BASE + i * 4u) ^ 0x55aa55aau;
    sync_memory();
    for (uint32_t i = 0; i < TEST_WORDS; ++i) check(mem + i, (TEST_BASE + i * 4u) ^ 0x55aa55aau);

    // Sparse high-address probes check distinct regions, not every DDR byte.
    static const uint32_t probes[] = {0x00100000u, 0x0ff00000u, 0x10000000u, 0x1fff0000u};
    report.phase = 7;
    for (uint32_t i = 0; i < 4; ++i) *(volatile uint32_t *)probes[i] = 0x12345670u + i;
    sync_memory();
    for (uint32_t i = 0; i < 4; ++i) check((volatile uint32_t *)probes[i], 0x12345670u + i);
    report.phase = 8;
    sync_memory();
    report.state = PASS;
    sync_memory();
    test_done();
}
