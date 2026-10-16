# Only writes the private configuration mailbox; ARM alone issues AT commands.
set serial [lindex $argv 0]
proc tls_public {} {
    set deadline [expr {[clock milliseconds]+3000}]
    while {[clock milliseconds]<$deadline} {
        set before [mrd -force -value 0x1e144]
        set words [mrd -force -value 0x1e140 16]
        set after [mrd -force -value 0x1e144]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after && [lindex $words 0]==0x3154454e} {return $words}
    }
    error "No coherent network report"
}
proc tls_config {words} {
    set current [tls_public]
    if {[state] ne "Running" || [lindex $current 4]!=[mrd -force -value 0x1e184] ||
        [mrd -force -value 0x1e288]!=0 || [mrd -force -value 0x1e2d0]!=0 ||
        [mrd -force -value 0x1e240]!=0 || [mrd -force -value 0x1e108]!=2} {error "Application owner changed"}
    set request [expr {[lindex $current 4]+1}]
    if {$request>0xffffffff} {error "Request exhausted"}
    for {set i 0} {$i<32} {incr i} {
        if {$i!=1} {mwr -force [expr {0x1e180+4*$i}] [lindex $words $i]}
    }
    mwr -force 0x1e184 $request
    set deadline [expr {[clock milliseconds]+60000}]
    while {[clock milliseconds]<$deadline} {
        after 200
        set current [tls_public]
        if {[lindex $current 4]==$request && [lindex $current 2] in {0 6 8}} {return $current}
    }
    error "TLS transaction timed out"
}
set exit_code 1
set connected 0
set released 0
try {
    set private_line [gets stdin]
    if {![regexp {^[0-9a-f]{8}( [0-9a-f]{8}){31}$} $private_line]} {error "Private input format"}
    set private_words {}
    foreach token [split $private_line] {scan $token %x word;lappend private_words $word}
    unset private_line token word
    connect -url TCP:127.0.0.1:3121;set connected 1
    set cpus [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
    if {[llength $cpus]!=1 || [dict get [lindex $cpus 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0"}
    targets -set -filter {name =~ "*Cortex-A9*#0"}
    set initial [tls_public]
    if {[state] ne "Running" || [lindex $initial 2]!=0 || [lindex $initial 3]!=0} {error "Idle network required"}
    set progressed 0
    for {set i 0} {$i<10} {incr i} {
        after 100;set current [tls_public]
        if {[lindex $current 1]!=[lindex $initial 1]} {set progressed 1;break}
    }
    if {!$progressed} {error "Application not progressing"}
    set released 1
    set close_words [lrepeat 32 0];lset close_words 0 0x3146434e;lset close_words 2 4
    foreach name {positive wrong_identity positive_again} wrong {0 1 0} {
        lset private_words 2 3;lset private_words 30 0;lset private_words 31 $wrong
        set released 0
        set result [tls_config $private_words]
        puts "ARM_TLS_RESULT $name $result";flush stdout
        set phase [lindex $result 2];set err [lindex $result 3]
        set expected [expr {$wrong?($phase==6 && $err==43):($phase==8 && $err==0)}]
        # Close only known terminal transactions. Ambiguous timeout/UART failures
        # remain owned for diagnosis, never certified by a later bare OK.
        if {$phase==8 || ($phase==6 && $err in {43 44 45 47})} {
            set closed [tls_config $close_words]
            puts "ARM_TLS_RESULT ${name}_close $closed";flush stdout
            if {[lindex $closed 2]!=0 || [lindex $closed 3]!=0} {error "Close not confirmed"}
            set released 1
        } else {error "TLS outcome not releasable"}
        if {!$expected} {error "Unexpected TLS result"}
    }
    puts PASS_ARM_TLS_BOOT
    set exit_code 0
} on error {message options} {
    puts ARM_TLS_BOOT_FAILED
} finally {
    puts "ARM_TLS_RELEASED $released"
    catch {unset private_words private_line}
    if {$connected} {catch {disconnect}}
}
exit $exit_code
