ENTRY(_start)
MEMORY { STUB (rwx) : ORIGIN = 0x1F800000, LENGTH = 0x100000 }
SECTIONS {
    .text : { *(.text.start) *(.text*) *(.rodata*) } > STUB
    .data : { *(.data*) *(.bss*) *(COMMON) } > STUB
    ASSERT(SIZEOF(.data) == 0, "Stub must not rely on initialized or zeroed globals")
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.attributes) }
}
