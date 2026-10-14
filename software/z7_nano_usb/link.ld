ENTRY(_start)
MEMORY { OCM (rwx) : ORIGIN = 0x00001000, LENGTH = 0xF000 }
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > OCM
    .text : { *(.text*) *(.rodata*) } > OCM
    .data : { *(.data*) } > OCM
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > OCM
    ASSERT(__bss_end <= 0xD000, "Core1 OCM code/data overlaps report/stack")
    .mailbox 0xE000 (NOLOAD) : { KEEP(*(.mailbox)) } > OCM
    ASSERT(SIZEOF(.mailbox) == 0x40, "USB probe report ABI changed")
    __stack_top = 0xFFF0;
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
