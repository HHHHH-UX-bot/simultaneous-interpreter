# Private credentials arrive via stdin and are never printed or read back.
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts ps_debug_common.tcl]
set connected 0;set selected 0;set exit_code 0;set public_stage input;set public_reason unavailable
proc config_public {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value 0x1e144]
        set values [mrd -force -value 0x1e140 16]
        set after [mrd -force -value 0x1e144]
        if {$before==$after && ($after&1)==0 && [lindex $values 1]==$after} {break}
        if {[clock milliseconds]>=$deadline} {set ::public_reason incoherent_public;error "Public status unavailable"}
        after 20
    }
    if {[llength $values]!=16 || [lindex $values 0]!=0x3154454e} {set ::public_reason wrong_application;error "Wrong application"}
    return $values
}
try {
    set private_line [gets stdin]
    if {![regexp {^[0-9a-f]{8}( [0-9a-f]{8}){31}$} $private_line]} {error "Invalid private input"}
    set private_words {}
    foreach token [split $private_line] {scan $token %x word;lappend private_words $word}
    unset private_line token
    set public_stage connection
    connect -url TCP:127.0.0.1:3121;set connected 1;select_ps_core 0;set selected 1
    set public_stage cpu
    if {[state] ne "Running"} {error "CPU unavailable"}
    set public_stage public_status
    set before [config_public];set deadline [expr {[clock milliseconds]+3500}]
    while {1} {
        set current [config_public]
        if {[lindex $current 1]!=[lindex $before 1]} {break}
        if {[clock milliseconds]>=$deadline} {error "Application not progressing"}
        after 50
    }
    if {[lindex $current 2]!=0 || [lindex $current 6]!=0xffffffff} {error "Fresh network task required"}
    set request [mrd -force -value 0x1e184]
    if {$request>=0xffffffff} {error "Configuration sequence exhausted"}
    incr request
    set public_stage content_write
    # Preserve the old request word until all other fields are present.
    for {set i 0} {$i<32} {incr i} {
        if {$i!=1} {mwr -force [expr {0x1e180+4*$i}] [lindex $private_words $i]}
    }
    unset private_words
    set public_stage commit
    mwr -force 0x1e184 $request
    set public_stage consumption
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set values [config_public]
        if {[lindex $values 4]==$request} {break}
        if {[clock milliseconds]>=$deadline} {set public_reason no_ack;error "No configuration acknowledgement"}
        after 50
    }
    if {[state] ne "Running"} {error "CPU state unavailable"}
    puts "NETWORK_CONFIG_RESULT $values"
    puts PASS_PRIVATE_NETWORK_CONFIG
} on error {message options} {
    # Never echo Tcl error details or a private word value.
    puts "PRIVATE_CONFIG_FAILURE_STAGE $public_stage"
    set public_cpu unavailable
    if {$selected && ![catch {state} cpu_state]} {
        if {$cpu_state eq "Running"} {set public_cpu running}
        if {[string match "Stopped:*" $cpu_state]} {set public_cpu stopped}
    }
    puts "PRIVATE_CONFIG_FAILURE_CPU $public_cpu"
    puts "PRIVATE_CONFIG_FAILURE_REASON $public_reason"
    puts stderr PRIVATE_NETWORK_CONFIG_FAILED
    set exit_code 1
} finally {
    catch {unset private_words private_line}
    if {$connected} {catch {disconnect}}
}
exit $exit_code
