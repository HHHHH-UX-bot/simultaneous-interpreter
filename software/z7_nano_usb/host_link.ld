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
    ASSERT(__bss_end <= 0xD000, "USB Host code/data overlaps snapshot/stack")
    .touch_link 0xD000 (NOLOAD) : { KEEP(*(.touch_link)) } > OCM
    ASSERT(SIZEOF(.touch_link) == 0x100, "Touch link ABI changed")
    .mailbox 0xE000 (NOLOAD) : { KEEP(*(.mailbox)) } > OCM
    ASSERT(SIZEOF(.mailbox) == 0x40, "Host report ABI changed")
    .snapshot 0xE100 (NOLOAD) : { KEEP(*(.snapshot)) } > OCM
    ASSERT(SIZEOF(.snapshot) <= 0xF00, "Host snapshot overlaps stack")
    __stack_top = 0xFFF0;
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
