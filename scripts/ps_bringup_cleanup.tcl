set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts ps_debug_common.tcl]
set connected 0
set exit_code 0
try {
    connect -url TCP:127.0.0.1:3121
    set connected 1
    halt_ps_core 1
    halt_ps_core 0
    puts PASS_PS_CLEANUP
} on error {message options} {
    puts stderr "ERROR_PS_BRINGUP: cleanup cannot confirm CPU state: $message"
    set exit_code 1
} finally {
    if {$connected} {catch {disconnect}}
}
exit $exit_code
