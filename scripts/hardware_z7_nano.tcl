set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root output z7_nano config board.tcl]
set action [lindex $argv 0]
set record [lindex $argv 1]
if {$action ni {probe program-static program-display program-wifi}} {error "Unsupported hardware action"}
open_hw_manager
connect_hw_server -url localhost:3121
set targets [get_hw_targets]
if {[llength $targets] != 1} {error "Expected one JTAG target"}
set target [lindex $targets 0]
if {![string match "*/$board_serial" $target]} {error "Unexpected JTAG cable: $target"}
current_hw_target $target
open_hw_target
set devices [get_hw_devices -filter {PART != "arm_dap"}]
if {[llength $devices] != 1 || [get_property PART [lindex $devices 0]] ne "xc7z010"} {
    error "Expected one xc7z010"
}
set device [lindex $devices 0]
current_hw_device $device
refresh_hw_device -update_hw_probes false $device
scan [get_property REGISTER.USR_ACCESS $device] %x identity
if {$action ne "probe"} {
    set_property PROGRAM.FILE [lindex $argv 2] $device
    set_property PROBES.FILE {} $device
    set_property FULL_PROBES.FILE {} $device
    program_hw_devices $device
    refresh_hw_device -update_hw_probes false $device
    if {[get_property REGISTER.IR.BIT5_DONE $device] ne "1" ||
        [get_property {REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS} $device] ne "1" ||
        [get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $device] ne "0"} {error "DONE/EOS/CRC check failed"}
    scan [get_property REGISTER.USR_ACCESS $device] %x identity
    set expected [dict get {program-static 0x5a374e31 program-display 0x5a374e32 program-wifi 0x5a374e33} $action]
    if {$identity != $expected} {error "Configuration identity mismatch"}
}
set done [get_property REGISTER.IR.BIT5_DONE $device]
set eos [get_property {REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS} $device]
set crc [get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $device]
set f [open $record w]
puts $f "{\"status\":\"passed\",\"part\":\"xc7z010\",\"cable_serial\":\"$board_serial\",\"usr_access\":$identity,\"done\":$done,\"eos\":$eos,\"crc_error\":$crc,\"action\":\"$action\",\"screen_acceptance\":\"pending\"}"
close $f
puts PASS_Z7_NANO_HARDWARE
close_hw_target
disconnect_hw_server
close_hw_manager
