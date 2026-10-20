# Dedicated UART test only. Caller verifies bit/ELF hashes and live PL first.
set action [lindex $argv 0]
set serial [lindex $argv 1]
set output [lindex $argv 2]
if {$action ni {load prepare start result}} {error "Unknown UART test action"}
connect -url TCP:127.0.0.1:3121
after 1000
foreach filter {{name == "xc7z010"} {name =~ "*Cortex-A9*#0"} {name =~ "*Cortex-A9*#1"}} {
    set target [targets -target-properties -filter $filter]
    if {[llength $target]!=1 || ![dict exists [lindex $target 0] jtag_cable_serial] || [dict get [lindex $target 0] jtag_cable_serial] ne $serial} {error "Wrong UART test target"}
}
if {$action eq "load"} {
    targets -set -filter {name =~ "APU*"}
    rst -system
    after 1000
    foreach core {1 0} {
        targets -set -filter "name =~ \"*Cortex-A9*#$core\""
        if {[state] eq "Running"} {stop}
        if {![string match "Stopped:*" [state]]} {error "Core did not stop"}
    }
    targets -set -filter {name == "xc7z010"}
    fpga -file [lindex $argv 3]
    targets -set -filter {name =~ "*Cortex-A9*#0"}
    source [lindex $argv 4]
    ps7_init
    ps7_post_config
    if {[mrd -force -value 0x43c30000]!=0x57494631} {error "Wrong UART peripheral"}
    rst -processor
    dow [lindex $argv 5]
    con
    after 100
}
targets -set -filter {name =~ "*Cortex-A9*#0"}
set words [mrd -force -value 0x1e000 32]
if {[lindex $words 0]!=0x31545755} {error "UART test application not loaded"}
if {$action eq "load" && [lindex $words 1]!=1} {error "UART test not ready"}
if {$action eq "prepare"} {
    if {[lindex $words 1] ni {0x00000001 0x00000003 1 3}} {error "Previous UART test not idle"}
    dow -data [lindex $argv 3] 0x01000000
    mwr -force 0x1e010 [list [lindex $argv 4] 50 [lindex $argv 5]]
    mwr -force 0x1e004 1
}
if {$action eq "start"} {
    if {[lindex $words 1]!=1} {error "UART test not prepared"}
    mwr -force 0x1e008 1
}
set numbers {}
foreach word [mrd -force -value 0x1e000 32] {lappend numbers [expr {$word+0}]}
set uart {}
foreach word [mrd -force -value 0x43c30018 6] {lappend uart [expr {$word+0}]}
set f [open $output w]
puts $f "{\"report\":\[[join $numbers ,]\],\"uart\":\[[join $uart ,]\]}"
close $f
disconnect
puts "PASS_UART_WIRE_[string toupper $action]"
