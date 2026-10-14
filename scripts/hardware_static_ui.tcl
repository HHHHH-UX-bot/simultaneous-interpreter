set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output fpga_static]
file mkdir [file join $out hardware]
set action [lindex $argv 0]
if {$action ni {probe program}} {error "Use probe or program"}
proc json_string {value} {
    return "\"[string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r" "\t" "\\t"] $value]\""
}
proc utc_now {} {return [clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]}
set target_name ""
set device_name ""
set summary_path [file join $out hardware program_result.json]
if {$action eq "program"} {
    set attempt [lindex $argv 1]
    set bit_hash [lindex $argv 2]
    if {![regexp {^[0-9a-fA-F-]{36}$} $attempt] || ![regexp {^[0-9a-fA-F]{64}$} $bit_hash]} {
        error "Use run_static_ui.ps1: verified attempt ID and bitstream SHA256 are required"
    }
    set summary [open $summary_path w]
    puts $summary "{\"Status\":\"pending\",\"AttemptId\":[json_string $attempt],\"BitstreamSHA256\":[json_string $bit_hash],\"StartedAtUTC\":[json_string [utc_now]]}"
    close $summary
}
set failed [catch {
    open_hw_manager
    connect_hw_server -url localhost:3121
    set targets [get_hw_targets]
    if {[llength $targets] != 1} {error "Expected one JTAG target, found $targets"}
    set target_name [lindex $targets 0]
    current_hw_target $target_name
    open_hw_target
    set devices [get_hw_devices -filter {PART =~ "xc7z010*"}]
    if {[llength $devices] != 1} {error "Expected one xc7z010, found [get_hw_devices]"}
    set device [lindex $devices 0]
    set device_name [get_property NAME $device]
    current_hw_device $device
    refresh_hw_device -update_hw_probes false $device
    puts "PASS_STATIC_HW_TARGET: name=$device_name part=[get_property PART $device]"
    if {$action eq "program"} {
        set_property PROGRAM.FILE [file join $out static_ui_top.bit] $device
        set_property PROBES.FILE {} $device
        set_property FULL_PROBES.FILE {} $device
        program_hw_devices $device
        refresh_hw_device -update_hw_probes false $device
        if {[get_property REGISTER.IR.BIT5_DONE $device] ne "1" ||
            [get_property {REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS} $device] ne "1" ||
            [get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $device] ne "0"} {
            error "Configuration DONE/EOS/CRC verification failed"
        }
    }
    set report [open [file join $out hardware ${action}_properties.txt] w]
    foreach property [lsort [list_property $device]] {
        if {[regexp {^(PART|NAME|IS_|PROGRAM\.|REGISTER\.(CONFIG_STATUS|IR|BOOT_STATUS|USR_ACCESS))} $property]} {
            puts $report "$property=[get_property $property $device]"
        }
    }
    close $report
    set part [get_property PART $device]
    if {$action eq "program"} {
        set summary [open $summary_path w]
        puts $summary "{\"Status\":\"configured\",\"AttemptId\":[json_string $attempt],\"BitstreamSHA256\":[json_string $bit_hash],\"Target\":[json_string $target_name],\"Device\":[json_string $device_name],\"Part\":[json_string $part],\"DONE\":1,\"EOS\":1,\"CRC_ERROR\":0,\"ConfiguredAtUTC\":[json_string [utc_now]],\"Volatile\":true,\"ScreenAcceptance\":\"pending_user_confirmation\"}"
        close $summary
        puts "PASS_STATIC_PROGRAM: JTAG configured; DONE=1 EOS=1 CRC_ERROR=0"
    } else {
        set summary [open [file join $out hardware probe_result.json] w]
        puts $summary "{\"Status\":\"passed\",\"Target\":[json_string $target_name],\"Device\":[json_string $device_name],\"Part\":[json_string $part],\"VerifiedAtUTC\":[json_string [utc_now]]}"
        close $summary
    }
} error_text error_options]
catch {close_hw_target}
catch {disconnect_hw_server}
catch {close_hw_manager}
if {$failed} {
    if {$action eq "program"} {
        set summary [open $summary_path w]
        puts $summary "{\"Status\":\"failed\",\"AttemptId\":[json_string $attempt],\"BitstreamSHA256\":[json_string $bit_hash],\"Target\":[json_string $target_name],\"Device\":[json_string $device_name],\"FailedAtUTC\":[json_string [utc_now]],\"ErrorMessage\":[json_string $error_text],\"ScreenAcceptance\":\"unverified\"}"
        close $summary
    }
    return -options $error_options $error_text
}
