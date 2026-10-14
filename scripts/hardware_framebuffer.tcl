set root [file normalize [file join [file dirname [info script]] ..]]
set bit_path [lindex $argv 0]
set connected 0
set target_open 0
set manager_open 0
set exit_code 0
try {
    if {![file isfile $bit_path]} {error "Missing attempt bitstream"}
    open_hw_manager
    set manager_open 1
    connect_hw_server -url localhost:3121
    set connected 1
    set targets [get_hw_targets]
    if {[llength $targets]!=1 || ![string match *210251A08870* [lindex $targets 0]]} {error "Unexpected JTAG target"}
    current_hw_target [lindex $targets 0]
    open_hw_target
    set target_open 1
    set devices [get_hw_devices -filter {PART =~ "xc7z010*"}]
    if {[llength $devices]!=1} {error "Expected one xc7z010"}
    set device [lindex $devices 0]
    current_hw_device $device
    set_property PROGRAM.FILE $bit_path $device
    set_property PROBES.FILE {} $device
    set_property FULL_PROBES.FILE {} $device
    program_hw_devices $device
    refresh_hw_device $device
    set done [get_property REGISTER.IR.BIT5_DONE $device]
    set eos [get_property {REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS} $device]
    set crc [get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $device]
    set raw [get_property REGISTER.USR_ACCESS $device]
    if {[regexp {^(?:0x)?([0-9a-fA-F]{8})$} $raw -> value]} {
        scan $value %x identity
    } elseif {[string is entier -strict $raw]} {
        set identity [expr {$raw}]
    } else {error "Cannot decode USR_ACCESS: $raw"}
    puts "FPGA_RESULT $done $eos $crc $identity"
    if {$done ne "1" || $eos ne "1" || $crc ne "0" || $identity!=0x46564231} {error "FPGA verification failed"}
    puts PASS_FRAMEBUFFER_PROGRAM
} on error {message options} {
    puts stderr "ERROR_FRAMEBUFFER: $message"
    set exit_code 1
} finally {
    if {$target_open} {catch {close_hw_target}}
    if {$connected} {catch {disconnect_hw_server}}
    if {$manager_open} {catch {close_hw_manager}}
}
exit $exit_code
