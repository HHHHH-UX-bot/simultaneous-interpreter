# Caller binds current Z7N3 program/ELF. UART transactions remain CPU0-owned.
set serial [lindex $argv 0]
set output [lindex $argv 1]
connect -url TCP:127.0.0.1:3121
after 1000
set cpus [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpus]!=1 || [dict get [lindex $cpus 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0 target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
proc capability_public {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value 0x1e144]
        set net [mrd -force -value 0x1e140 16]
        set caps [mrd -force -value 0x1e200 16]
        set after [mrd -force -value 0x1e144]
        if {$before==$after && ($after&1)==0 && [lindex $net 1]==$after} {break}
        if {[clock milliseconds]>=$deadline} {error "Capability report is not coherent"}
        after 10
    }
    if {[llength $net]!=16 || [lindex $net 0]!=0x3154454e || [llength $caps]!=16 || [lindex $caps 0]!=0x43415031} {
        error "Wrong capability application ABI"
    }
    return [list $net $caps]
}
set first [capability_public]
set deadline [expr {[clock milliseconds]+3500}]
while {1} {
    after 50
    lassign [capability_public] net caps
    if {[lindex $net 1]!=[lindex [lindex $first 0] 1]} {break}
    if {[clock milliseconds]>=$deadline} {error "Capability application is not progressing"}
}
if {[state] ne "Running" || [lindex $net 2]!=0 || [lindex $net 3]!=0 || [lindex $net 4]!=0 ||
    [lindex $net 6]!=0xffffffff || [lindex $caps 1]!=0} {error "Fresh idle capability application required"}
if {[mrd -force -value 0x1e108]!=2} {error "Autonomous UART acceptance required"}
# Clear unused configuration content; publish the request last. No credentials.
mwr -force 0x1e188 0 30
mwr -force 0x1e180 0x3146434e
mwr -force 0x1e188 2
mwr -force 0x1e184 1
set deadline [expr {[clock milliseconds]+25000}]
while {1} {
    after 50
    lassign [capability_public] net caps
    if {[lindex $caps 1]==2 || [lindex $caps 1]==6 || [lindex $net 2]==6} {break}
    if {[clock milliseconds]>=$deadline} {error "Capability query observation timed out"}
}
set complete [expr {[state] eq "Running" && [lindex $caps 1]==2 && [lindex $caps 2]==0 &&
    [lindex $caps 4]==31 && [lindex $net 3]==0 && [lindex $net 12]==0 && [lindex $net 13]==0}]
set status [expr {$complete?"observed":"failed"}]
set f [open $output w]
puts $f "{\"status\":\"$status\",\"network\":\[[join $net ,]\],\"capabilities\":\[[join $caps ,]\]}"
close $f
disconnect
if {!$complete} {error "Capability query task failed"}
puts OBSERVED_Z7_CAPABILITIES
