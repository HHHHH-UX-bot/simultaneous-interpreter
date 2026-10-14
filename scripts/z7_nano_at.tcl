# Public status only. The caller has checked Z7N3, current bitstream/app and display progress.
set serial [lindex $argv 0]
set result [lindex $argv 1]
connect -url TCP:127.0.0.1:3121
after 1000
set cpu [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpu]!=1 || [dict get [lindex $cpu 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0 target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
proc wifi_report {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value 0x1e104]
        set words [mrd -force -value 0x1e100 16]
        set after [mrd -force -value 0x1e104]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after} {return $words}
        if {[clock milliseconds]>=$deadline} {error "No coherent public AT status"}
        after 10
    }
}
set initial [wifi_report]
set deadline [expr {[clock milliseconds]+5000}]
while {1} {
    after 100
    set next [wifi_report]
    if {[lindex $next 2]==6 || [lindex $next 12]>[lindex $initial 12]} {break}
    if {[clock milliseconds]>=$deadline} {error "AT task is not progressing: $next"}
}
set passed [expr {[state] eq "Running" && [lindex $next 0]==0x57505331 &&
    [lindex $next 2]==2 && [lindex $next 3]==0 && [lindex $next 4]==1 &&
    [lindex $next 5]==1 && [lindex $next 6]==1 && [lindex $next 8]>0 &&
    [lindex $next 9]==22 && [lindex $next 10]==0 && [lindex $next 11]==0}]
set status [expr {$passed ? "passed" : "failed"}]
set f [open $result w]
puts $f "{\"status\":\"$status\",\"phase\":[lindex $next 2],\"error\":[lindex $next 3],\"at_ok\":[lindex $next 4],\"echo_off\":[lindex $next 5],\"firmware_ok\":[lindex $next 6],\"firmware_hash\":[lindex $next 7],\"rx_received\":[lindex $next 8],\"tx_started\":[lindex $next 9],\"rx_overflow\":[lindex $next 10],\"framing_error\":[lindex $next 11],\"ticks\":[lindex $next 12],\"report\":\[[join $next ,]\]}"
close $f
disconnect
if {!$passed} {error "Autonomous AT did not pass: $next"}
puts PASS_Z7_NANO_AT
