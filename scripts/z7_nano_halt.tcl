# Stop only the two cores on the verified new-board cable; no DDR/PL register writes.
set serial [lindex $argv 0]
connect -url TCP:127.0.0.1:3121
after 1000
set fpga [targets -target-properties -filter {name == "xc7z010"}]
if {[llength $fpga] != 1 || [dict get [lindex $fpga 0] jtag_cable_serial] ne $serial} {
    error "Expected one xc7z010 on the selected cable before halting CPUs"
}
foreach core {1 0} {
    set filter "name =~ \"*Cortex-A9*#$core\""
    set matches [targets -target-properties -filter $filter]
    if {[llength $matches] != 1 || [dict get [lindex $matches 0] jtag_cable_serial] ne $serial} {
        error "Unexpected CPU$core target"
    }
    targets -set -filter $filter
    if {[state] eq "Running"} {stop}
    if {![string match "Stopped:*" [state]]} {error "CPU$core did not halt"}
    puts "CPU${core}_HALTED [state]"
}
disconnect
puts PASS_Z7_NANO_HALT
