# Observe only the public status report; no raw UART access or credential reads.
set root [file normalize [file join [file dirname [info script]] ..]]
set f [open [file join $root scripts ps_subtitle_ui.tcl] r]
set body [read $f]
close $f
set needle {    puts "PASS_SUBTITLE_[string toupper $action]"}
set replacement {
    set wifi_deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set wifi_before [ui_word 0x1e104]
        set wifi_values [mrd -force -value 0x1e100 16]
        set wifi_after [ui_word 0x1e104]
        if {$wifi_before==$wifi_after && ($wifi_after&1)==0 && [lindex $wifi_values 1]==$wifi_after} {break}
        if {[clock milliseconds]>=$wifi_deadline} {error "No coherent public Wi-Fi status"}
        after 10
    }
    if {[llength $wifi_values]!=16 || [lindex $wifi_values 0]!=0x57505331 || [state] ne "Running"} {
        error "Invalid public PS Wi-Fi report"
    }
    puts "PS_WIFI_RESULT $wifi_values"
    puts "PASS_SUBTITLE_[string toupper $action]"
}
set start [string first $needle $body]
if {$start<0 || [string first $needle $body [expr {$start+[string length $needle]}]]>=0} {
    error "Public Wi-Fi observation hook is ambiguous"
}
eval [string map [list $needle $replacement] $body]
