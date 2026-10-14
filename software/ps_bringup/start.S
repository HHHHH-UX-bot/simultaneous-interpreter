.syntax unified
.cpu cortex-a9
.arm
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
reset_entry:
    cpsid if
    cps #0x13
    ldr sp, =__stack_top
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
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl main
exception_hang:
    b exception_hang
