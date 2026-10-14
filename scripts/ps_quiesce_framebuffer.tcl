set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts ps_debug_common.tcl]
set connected 0
set exit_code 0
proc bounded_value {address mask expected} {
    set deadline [expr {[clock milliseconds]+3000}]
    while {([mrd -force -value $address]&$mask)!=$expected} {
        if {[clock milliseconds]>=$deadline} {error "DMA quiesce timed out at $address"}
        after 10
    }
}
try {
    connect -url TCP:127.0.0.1:3121
    set connected 1
    halt_ps_core 1
    halt_ps_core 0
    if {[mrd -force -value 0x43c10000]!=0x46564231} {error "Expected known framebuffer peripheral before quiesce"}
    mwr -force 0x43c10004 0
    bounded_value 0x43c10004 2 0
    mwr -force 0x43c00000 0
    bounded_value 0x43c00004 1 1
    # PG020: soft reset completes/flushes pending commands/transfers before
    # its reset bit clears. No DDR initialization or PL programming here.
    mwr -force 0x43c00000 4
    bounded_value 0x43c00000 4 0
    bounded_value 0x43c00004 1 1
    puts "QUIESCE_RESULT [mrd -force -value 0x43c10004] [mrd -force -value 0x43c00004]"
    puts PASS_FRAMEBUFFER_QUIESCE
} on error {message options} {
    puts stderr "ERROR_PS_BRINGUP: $message"
    set exit_code 1
} finally {
    if {$connected} {catch {disconnect}}
}
exit $exit_code
