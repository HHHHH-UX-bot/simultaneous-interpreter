# Caller binds the current program and ELF; only the diagnostic duration is written.
set serial [lindex $argv 0]
set action [lindex $argv 1]
set duration [lindex $argv 2]
set output [lindex $argv 3]
connect -url TCP:127.0.0.1:3121
after 1000
set cpus [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpus]!=1 || [dict get [lindex $cpus 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Upload application is not running"}
if {[mrd -force -value 0x1e140]!=0x3154454e} {error "Wrong network application"}
if {$action eq "stop"} {
    mwr -force 0x1e240 0
    disconnect
    puts PASS_Z7_UPLOAD_STOP
    exit
}
proc public_words {address} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value [expr {$address+4}]]
        set words [mrd -force -value $address 16]
        set after [mrd -force -value [expr {$address+4}]]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after} {return $words}
        if {[clock milliseconds]>=$deadline} {error "Public upload status is not coherent"}
        after 1
    }
}
set network [public_words 0x1e140]
set wifi [public_words 0x1e100]
if {[lindex $network 0]!=0x3154454e || [lindex $wifi 0]!=0x57505331} {error "Wrong upload ABI"}
if {$action eq "enable" || $action eq "arm"} {
    set expected_phase [expr {$action eq "arm" ? 0 : 3}]
    if {[lindex $network 2]!=$expected_phase || [lindex $network 3]!=0 || [lindex $network 6]!=0xffffffff ||
        [mrd -force -value 0x1e240]!=0 || $duration<1 || $duration>3600000} {error "Idle connected upload application required"}
    mwr -force 0x1e240 $duration
} elseif {$action ne "sample"} {error "Unknown upload operation"}
set latch [mrd -force -value 0x1e240]
set f [open $output w]
puts $f "{\"network\":\[[join $network ,]\],\"wifi\":\[[join $wifi ,]\],\"duration_ms\":$latch}"
close $f
disconnect
puts PASS_Z7_UPLOAD_CONTROL
