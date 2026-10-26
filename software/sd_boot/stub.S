.syntax unified
.cpu cortex-a9
.arm

/* FSBL hands off here with the MMU and caches off. The stub runs from DDR so
   it may overwrite the OCM the FSBL occupied. */
.section .text.start,"ax"
.global _start
_start:
    cpsid if
    ldr sp, =0x1F8FFFF0
    bl stub_main
1:  wfe
    b 1b
