set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output wifi]
set action [lindex $argv 0]
set attempt [lindex $argv 1]
set bit_hash [lindex $argv 2]
if {$action ni {probe program}} {error "Use run_wifi.py probe/program"}
proc json_string {v} {return "\"[string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r"] $v]\""}
open_hw_manager
connect_hw_server -url localhost:3121
set targets [get_hw_targets]
if {[llength $targets] != 1} {error "Expected exactly one JTAG target"}
set target [lindex $targets 0]
current_hw_target $target
open_hw_target
set devices [get_hw_devices -filter {PART =~ "xc7z010*"}]
if {[llength $devices] != 1} {error "Expected one xc7z010"}
set device [lindex $devices 0]
current_hw_device $device
if {$action eq "program"} {
    set_property PROGRAM.FILE [file join $out wifi_ui_top.bit] $device
    set_property PROBES.FILE {} $device
    set_property FULL_PROBES.FILE {} $device
    program_hw_devices $device
}
refresh_hw_device $device
set done [get_property REGISTER.IR.BIT5_DONE $device]
set eos [get_property {REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS} $device]
set crc [get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $device]
if {$action eq "program" && ($done ne "1" || $eos ne "1" || $crc ne "0")} {error "DONE/EOS/CRC check failed"}
set axes [get_hw_axis -quiet]
if {$action eq "program" && [llength $axes] != 1} {error "Expected one JTAG AXI master"}
file mkdir [file join $out hardware]
set f [open [file join $out hardware ${action}_result.json] w]
puts $f "{\"Status\":\"verified\",\"AttemptId\":[json_string $attempt],\"Target\":[json_string $target],\"Device\":[json_string [get_property NAME $device]],\"BitstreamSHA256\":[json_string $bit_hash],\"DONE\":$done,\"EOS\":$eos,\"CRC_ERROR\":$crc,\"AXIMasters\":[llength $axes],\"Volatile\":true}"
close $f
puts "PASS_WIFI_[string toupper $action]"
close_hw_target
disconnect_hw_server
close_hw_manager
