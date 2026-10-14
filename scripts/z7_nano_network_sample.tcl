# Public OCM only; caller has validated the current Z7N3 network build/run and display.
set serial [lindex $argv 0]
set result [lindex $argv 1]
connect -url TCP:127.0.0.1:3121
after 1000
set cpu [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpu]!=1 || [dict get [lindex $cpu 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0 target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
proc public_report {address magic} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value [expr {$address+4}]]
        set words [mrd -force -value $address 16]
        set after [mrd -force -value [expr {$address+4}]]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after && [llength $words]==16} {break}
        if {[clock milliseconds]>=$deadline} {error "No coherent public report"}
        after 10
    }
    if {[lindex $words 0]!=$magic} {error "Wrong public application ABI"}
    return $words
}
set first [public_report 0x1e140 0x3154454e]
set deadline [expr {[clock milliseconds]+3500}]
while {1} {
    after 50
    set net [public_report 0x1e140 0x3154454e]
    set ui [public_report 0x1e080 0x55493131]
    if {[lindex $net 1]!=[lindex $first 1] && [lindex $net 15]==[lindex $ui 10]} {break}
    if {[clock milliseconds]>=$deadline} {error "Network/UI progress or epoch mismatch"}
}
set wifi [public_report 0x1e100 0x57505331]
if {[state] ne "Running"} {error "CPU0 is not running"}
set f [open $result w]
puts $f "{\"status\":\"passed\",\"network\":\[[join $net ,]\],\"ui\":\[[join $ui ,]\],\"wifi\":\[[join $wifi ,]\]}"
close $f
disconnect
puts PASS_Z7_NANO_NETWORK_SAMPLE
