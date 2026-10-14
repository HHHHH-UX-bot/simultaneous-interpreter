ENTRY(_start)
MEMORY { OCM (rwx) : ORIGIN = 0x00010000, LENGTH = 0x10000 }
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > OCM
    .text : { *(.text*) *(.rodata*) } > OCM
    .data : { *(.data*) } > OCM
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > OCM
    ASSERT(__bss_end <= 0x0001D000, "OCM code/data overlaps result mailbox")
    .mailbox 0x0001E000 (NOLOAD) : { KEEP(*(.mailbox)) } > OCM
    ASSERT(SIZEOF(.mailbox) <= 0x100, "Result mailbox exceeds reserved space")
    __stack_top = 0x0001FFF0;
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
