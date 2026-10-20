ENTRY(_start)
/* Boot code stays in OCM and runs uncached; the application, its rodata/data
   and the stack live in cached DDR. Mailboxes keep their host-pinned OCM
   addresses and are mapped Normal non-cacheable so JTAG always reads fresh
   values. The L1 section table is built at 0x01D00000 by start.S. */
MEMORY {
    OCM (rwx) : ORIGIN = 0x00010000, LENGTH = 0x10000
    APP_DDR (rwx) : ORIGIN = 0x01C00000, LENGTH = 0x000E0000
    STACK_DDR (rw) : ORIGIN = 0x01CE0000, LENGTH = 0x00020000
    FONT_DDR (r) : ORIGIN = 0x01800000, LENGTH = 0x200000
    SNAPSHOT_DDR (rw) : ORIGIN = 0x01B00000, LENGTH = 0x10000
    CLOUD_DDR (rw) : ORIGIN = 0x01A00000, LENGTH = 0x100000
    PLAYBACK_DDR (rw) : ORIGIN = 0x02000000, LENGTH = 0x100000
}
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > OCM
    .boot : { KEEP(*(.boot)) } > OCM
    .text : { *(.text*) *(.rodata*) } > APP_DDR
    .data : { *(.data*) } > APP_DDR
    .bss 0x01A80000 (NOLOAD) : {
        . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > CLOUD_DDR
    .mailbox 0x0001E000 (NOLOAD) : { KEEP(*(.mailbox)) } > OCM
    ASSERT(SIZEOF(.mailbox) == 0x40, "Display mailbox ABI changed")
    .touch_feedback 0x0001E040 (NOLOAD) : { KEEP(*(.touch_feedback)) } > OCM
    ASSERT(SIZEOF(.touch_feedback) <= 0x40, "Touch feedback exceeds reserve")
    .ui_mailbox 0x0001E080 (NOLOAD) : { KEEP(*(.ui_mailbox)) } > OCM
    ASSERT(SIZEOF(.ui_mailbox) <= 0x40, "UI mailbox exceeds reserve")
    .ui_command 0x0001E0C0 (NOLOAD) : { KEEP(*(.ui_command)) } > OCM
    ASSERT(SIZEOF(.ui_command) <= 0x40, "UI command exceeds reserve")
    .wifi_mailbox 0x0001E100 (NOLOAD) : { KEEP(*(.wifi_mailbox)) } > OCM
    ASSERT(SIZEOF(.wifi_mailbox) == 0x40, "Wi-Fi status ABI changed")
    .network_mailbox 0x0001E140 (NOLOAD) : { KEEP(*(.network_mailbox)) } > OCM
    ASSERT(SIZEOF(.network_mailbox) == 0x40, "Network public ABI changed")
    .network_config 0x0001E180 (NOLOAD) : { KEEP(*(.network_config)) } > OCM
    ASSERT(SIZEOF(.network_config) == 0x80, "Network private mailbox ABI changed")
    .cloud_config 0x0001E200 (NOLOAD) : { KEEP(*(.cloud_config)) } > OCM
    ASSERT(SIZEOF(.cloud_config) <= 0x180, "Cloud config exceeds reserve")
    .stream_pilot 0x0001E380 (NOLOAD) : { KEEP(*(.stream_pilot)) } > OCM
    ASSERT(SIZEOF(.stream_pilot) <= 0x40, "Stream pilot ABI changed")
    .cloud_mailbox 0x0001E3C0 (NOLOAD) : { KEEP(*(.cloud_mailbox)) } > OCM
    ASSERT(SIZEOF(.cloud_mailbox) <= 0x40, "Cloud public ABI changed")
    .ui_assets : { __ui_assets_start = .; KEEP(*(.ui_assets)) __ui_assets_end = .; } > FONT_DDR
    .cloud_runtime (NOLOAD) : { KEEP(*(.cloud_runtime)) } > CLOUD_DDR
    .playback_runtime (NOLOAD) : { KEEP(*(.playback_runtime)) } > PLAYBACK_DDR
    .cloud_final 0x01A60000 (NOLOAD) : { KEEP(*(.cloud_final)) } > CLOUD_DDR
    ASSERT(SIZEOF(.cloud_final) == 2052, "Cloud caption ABI changed")
    .cloud_masks 0x01A40000 (NOLOAD) : { KEEP(*(.cloud_masks)) } > CLOUD_DDR
    .ui_runtime (NOLOAD) : { __ui_runtime_start = .; KEEP(*(.ui_runtime)) __ui_runtime_end = .; } > SNAPSHOT_DDR
    ASSERT(SIZEOF(.ui_assets) <= 0x200000, "Font subset exceeds asset reserve")
    ASSERT(SIZEOF(.ui_runtime) <= 0x10000, "Snapshots exceed dedicated DDR reserve")
    __stack_top = 0x01CFFFF0;
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
