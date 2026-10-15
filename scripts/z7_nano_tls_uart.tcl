# Fixed public queries while the PS network task is idle. No credentials or Flash writes.
set serial [lindex $argv 0]
proc pilot_sample {} {
    set deadline [expr {[clock milliseconds]+3000}]
    while {[clock milliseconds]<$deadline} {
        set before [mrd -force -value 0x1e284]
        set words [mrd -force -value 0x1e280 16]
        set after [mrd -force -value 0x1e284]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after &&
            ([lindex $words 2]!=3 || [mrd -force -value 0x1e148]==0)} {return $words}
    }
    error "No coherent stream pilot report"
}
if {[file exists [file join [file dirname [info script]] .. output z7_nano tls uart_uncertain.json]]} {
    error "UART state unconfirmed; public recovery required before normal AT"
}
connect -url TCP:127.0.0.1:3121
after 1000
set cpus [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpus]!=1 || [dict get [lindex $cpus 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Running CPU0 required"}
set pilot_state [pilot_sample]
if {[mrd -force -value 0x1e140]!=0x3154454e ||
    [mrd -force -value 0x1e148]!=0 || [mrd -force -value 0x1e150]!=0 ||
    [mrd -force -value 0x1e184]!=0 || [mrd -force -value 0x1e108]!=2 ||
    [mrd -force -value 0x1e240]!=0 || [mrd -force -value 0x43c20000]!=0x57494631 ||
    [mrd -force -value 0x43c20008]!=54 || [lindex $pilot_state 0]!=0x31505453 ||
    [lindex $pilot_state 2] ni {0 3} ||
    [lindex $pilot_state 4]!=[lindex $pilot_state 5]} {error "Idle accepted UART required"}
proc take_rx {} {
    set data "";set n [mrd -force -value 0x43c20018]
    if {$n>2048} {error "RX level exceeds FIFO"}
    for {set i 0} {$i<$n} {incr i} {
        set value [mrd -force -value 0x43c20014]
        if {!($value&0x80000000)} {error "RX ownership changed"}
        append data [format %c [expr {$value&255}]]
    }
    return $data
}
proc transact {name text} {
    if {[mrd -force -value 0x1e148]!=0 || [mrd -force -value 0x1e150]!=0 ||
        [mrd -force -value 0x1e184]!=0 || [mrd -force -value 0x43c20004]&4} {error "UART owner changed or TX busy"}
    take_rx
    foreach char [split $text ""] {
        if {!([mrd -force -value 0x43c20004]&2)} {error "TX FIFO full"}
        scan $char %c code;mwr -force 0x43c20010 $code
    }
    set binary_size 0
    regexp {^AT\+SYSFLASH=2,"client_ca",[0-9]+,([0-9]+)\r\n$} $text -> binary_size
    set limit [expr {[clock milliseconds]+25000}];set reply "";set result timeout
    while {[clock milliseconds]<$limit} {
        append reply [take_rx]
        if {[string length $reply]>2048} {error "Response exceeds bound"}
        set start [string first "+SYSFLASH:$binary_size," $reply]
        if {$binary_size>0 && $start>=0} {
            set end [expr {$start+[string length "+SYSFLASH:$binary_size,"]+$binary_size}]
            if {[string range $reply $end [expr {$end+5}]] eq "\r\nOK\r\n"} {set result OK;break}
        } else {
            if {[string first "\r\nOK\r\n" $reply]>=0} {set result OK;break}
            if {[string first "\r\nERROR\r\n" $reply]>=0 || [string first "\r\nFAIL\r\n" $reply]>=0} {set result ERROR;break}
        }
        after 10
    }
    # Captured by the parent in RAM; only whitelisted fields enter evidence.
    puts "TLS_INVENTORY $name $result [binary encode hex $reply]";flush stdout
    if {$result eq "timeout"} {error "Query timed out"}
    return $reply
}
