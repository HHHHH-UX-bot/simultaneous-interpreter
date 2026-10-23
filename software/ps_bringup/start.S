.syntax unified
.cpu cortex-a9
.arm
#ifdef Z7_TINYQWEN
.fpu neon
#endif

/* Two boot variants share this file:
   - default: the historical OCM-only image used by the display/network/pilot
     build scripts (no MMU, unchanged layout).
   - Z7_MMU_CACHE: cloud_app build. Boot code stays in OCM (Normal,
     non-cacheable); the application, rodata/data and stack live in cached DDR
     (see software/network_ui/link.ld); the L1 section table is built here. */

.section .vectors,"ax"
.global _start
_start:
    b reset_entry
    b exception_hang
    b exception_hang
    b exception_hang
    b exception_hang
    b exception_hang
    b exception_hang
    b exception_hang

#if defined(Z7_MMU_CACHE)
.section .boot,"ax"
reset_entry:
    cpsid if
    cps #0x13
    ldr sp, =__stack_top
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0          @ VBAR: vector table lives in OCM
    isb sy

    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0          @ ICIALLU
    dsb sy
    isb sy

    /* Keep only the L1 caches: the PL VDMA reads DDR through the HP ports,
       which are not coherent with the Zynq L2, so L2 must stay disabled for
       DCCMVAC clean-to-PoC to reach DRAM. The L2 is disabled if ps7_init
       enabled it. */
    ldr r0, =0xF8F02100
    ldr r1, [r0]
    tst r1, #1
    beq 1f
    bic r1, r1, #1
    str r1, [r0]
    ldr r0, =0xF8F02730
    mov r1, #0
    str r1, [r0]
1:

    /* Invalidate D-cache lines for every DDR region the app touches
       (0x00F00000 guard bytes through 0x01E00000 page table/stack). */
    ldr r0, =0x00F00000
#ifdef Z7_TINYQWEN
    ldr r1, =0x1f800000  /* Weights, tokenizer, inference arena and bounded heap. */
#elif defined(Z7_ARM_TLS)
    ldr r1, =0x02800000  /* Include the opt-in TLS code and static arena. */
#elif defined(Z7_TRANSLATE_AUDIO)
    ldr r1, =0x02100000  /* Include the dedicated translated-audio ring. */
#else
    ldr r1, =0x01E00000
#endif
2:  cmp r0, r1
    bhs 3f
    mcr p15, 0, r0, c7, c6, 1          @ DCIMVAC
    add r0, r0, #32
    b 2b
3:  dsb sy
    isb sy

    /* Level-1 section table at 0x01D00000:
         0x000        OCM/boot: Normal non-cacheable, executable
         0x001..0x1FF DDR:      Normal write-back write-allocate
         0x400..0x7FF Device (PL AXI-Lite / PS peripherals), XN
         0xE00..0xEFF Device (GIC), XN
         0xF80..0xFFF Device (SLCR / global timer), XN
       Everything else stays a translation fault. */
    ldr r2, =0x01D00000
    mov r3, #0
    ldr r4, =0x1000
4:  cmp r3, r4
    bhs 9f
    mov r6, #0
    cmp r3, #0
    bne 5f
    ldr r6, =0x00001C02
    b 8f
5:  ldr r7, =0x200
    cmp r3, r7
    bhs 6f
    mov r5, r3, lsl #20
    ldr r7, =0x00001C0E
    orr r6, r5, r7
    b 8f
6:  ldr r7, =0x400
    cmp r3, r7
    blo 8f
    ldr r7, =0x800
    cmp r3, r7
    blo 7f
    ldr r7, =0xE00
    cmp r3, r7
    blo 8f
    ldr r7, =0xF00
    cmp r3, r7
    blo 7f
    ldr r7, =0xF80
    cmp r3, r7
    blo 8f
7:  mov r5, r3, lsl #20
    ldr r7, =0x00000C12
    orr r6, r5, r7
8:  str r6, [r2, r3, lsl #2]
    add r3, r3, #1
    b 4b
9:
    ldr r0, =0x01D00000
    mcr p15, 0, r0, c2, c0, 0          @ TTBR0
    mov r0, #0
    mcr p15, 0, r0, c2, c0, 2          @ TTBCR
    ldr r0, =0x55555555
    mcr p15, 0, r0, c3, c0, 0          @ DACR: all client
    mov r0, #0
    mcr p15, 0, r0, c8, c7, 0          @ TLBIALL: drop any stale TLB entries
    dsb sy
    isb sy

    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #0x00000001            @ M
    bic r0, r0, #0x00000004            @ C
    bic r0, r0, #0x00001000            @ I
    bic r0, r0, #0x00000800            @ Z
    bic r0, r0, #0x00002000            @ VE
    orr r0, r0, #0x00000001
    orr r0, r0, #0x00000004
    orr r0, r0, #0x00001000
    orr r0, r0, #0x00000800
    mcr p15, 0, r0, c1, c0, 0
    dsb sy
    isb sy

    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
10: cmp r0, r1
    strlo r2, [r0], #4
    blo 10b
    dsb sy
    isb sy
#ifdef Z7_TINYQWEN
    /* Softfp keeps the existing integer calling convention while allowing
       Cortex-A9 VFP/NEON arithmetic inside the local inference engine. */
    mrc p15, 0, r0, c1, c0, 2          @ CPACR
    orr r0, r0, #0x00f00000           @ Full access to CP10 and CP11
    mcr p15, 0, r0, c1, c0, 2
    isb sy
    mov r0, #0x40000000
    vmsr fpexc, r0
    mov r0, #0
    vmsr fpscr, r0
#endif
    bl main

exception_hang:
    ldr r0, =0x0001E3F0
    str lr, [r0]                       @ abort/prefetch LR (fault address + 4)
    mrs r1, spsr
    str r1, [r0, #4]
    b exception_hang
#else
reset_entry:
    cpsid if
    cps #0x13
    ldr sp, =__stack_top
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0          @ VBAR: vector table lives in OCM
    isb sy
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #1
    bic r0, r0, #4
    bic r0, r0, #0x1000
    bic r0, r0, #0x800
    bic r0, r0, #0x2000
    mcr p15, 0, r0, c1, c0, 0
    dsb sy
    isb sy
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0
    isb sy
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl main

exception_hang:
    b exception_hang
#endif
