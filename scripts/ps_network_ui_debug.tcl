# Read public UI/display/network reports; never inspect the private config buffer.
set root [file normalize [file join [file dirname [info script]] ..]]
proc network_public {address magic} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [mrd -force -value [expr {$address+4}]]
        set values [mrd -force -value $address 16]
        set after [mrd -force -value [expr {$address+4}]]
        if {$before==$after && ($after&1)==0 && [lindex $values 1]==$after} {break}
        if {[clock milliseconds]>=$deadline} {error "No coherent public report"}
        after 10
    }
    if {[llength $values]!=16 || [lindex $values 0]!=$magic} {error "Unexpected public report"}
    return $values
}
set f [open [file join $root scripts ps_subtitle_ui.tcl] r];set body [read $f];close $f
set needle {    puts "PASS_SUBTITLE_[string toupper $action]"}
set replacement {
    puts "PS_WIFI_RESULT [network_public 0x1e100 0x57505331]"
    set network_before [network_public 0x1e140 0x3154454e]
    set network_deadline [expr {[clock milliseconds]+3500}]
    while {1} {
        set network_after [network_public 0x1e140 0x3154454e]
        if {[lindex $network_after 1]!=[lindex $network_before 1]} {break}
        if {[clock milliseconds]>=$network_deadline} {error "Network task is not progressing"}
        after 50
    }
    set network_sync_deadline [expr {[clock milliseconds]+3500}]
    while {1} {
        set network_after [network_public 0x1e140 0x3154454e]
        set network_ui_current [ui_info]
        if {[lindex $network_after 15]==[lindex $network_ui_current 10]} {break}
        if {[clock milliseconds]>=$network_sync_deadline} {error "Public UI/network epoch did not settle"}
        after 20
    }
    puts "UI_RESULT $network_ui_current"
    puts "NETWORK_RESULT $network_after"
    if {[state] ne "Running"} {error "CPU not running after public network observation"}
    puts "PASS_SUBTITLE_[string toupper $action]"
}
set start [string first $needle $body]
if {$start<0 || [string first $needle $body [expr {$start+[string length $needle]}]]>=0} {error "Public network observation hook ambiguous"}
set ui_needle {    puts "UI_RESULT [ui_info]"}
if {[string first $ui_needle $body]<0} {error "UI report hook missing"}
set body [string map [list $ui_needle {    set network_ui_initial [ui_info]}] $body]
eval [string map [list $needle $replacement] $body]
