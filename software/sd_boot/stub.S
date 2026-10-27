.syntax unified
.cpu cortex-a9
.arm

/* FSBL hands off here with the MMU and caches off. The stub runs from DDR so
   it may overwrite the OCM the FSBL occupied. Data accesses stay uncached,
   but with the I-cache off every instruction of the CRC loop is a DDR fetch,
   which cost seconds; stub_main turns it off again before entering CPU0. */
.section .text.start,"ax"
.global _start
_start:
    cpsid if
    ldr sp, =0x1F8FFFF0
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0          @ ICIALLU
    mcr p15, 0, r0, c7, c5, 6          @ BPIALL
    dsb sy
    isb sy
    mrc p15, 0, r0, c1, c0, 0
    orr r0, r0, #0x1000                @ I
    orr r0, r0, #0x0800                @ Z
    mcr p15, 0, r0, c1, c0, 0
    isb sy
    bl stub_main
1:  wfe
    b 1b

/* Leave the core as the FSBL handed it over: I-cache and branch prediction
   off and invalidated, then jump to the CPU0 entry in r0. */
.global stub_enter
stub_enter:
    mrc p15, 0, r1, c1, c0, 0
    bic r1, r1, #0x1000
    bic r1, r1, #0x0800
    mcr p15, 0, r1, c1, c0, 0
    isb sy
    mov r1, #0
    mcr p15, 0, r1, c7, c5, 0          @ ICIALLU
    mcr p15, 0, r1, c7, c5, 6          @ BPIALL
    dsb sy
    isb sy
    bx r0
