# Reuse the private submission algorithm with this board's explicit cable selector.
set serial [lindex $argv 0]
proc select_ps_core {core} {
    after 1000
    set filter [format {name =~ "*Cortex-A9*#%d"} $core]
    set cpus [targets -target-properties -filter $filter]
    if {[llength $cpus]!=1 || [dict get [lindex $cpus 0] jtag_cable_serial] ne $::serial} {
        error "Unexpected new-board CPU target"
    }
    targets -set -filter $filter
}
set root [file normalize [file join [file dirname [info script]] ..]]
set f [open [file join $root scripts ps_network_config.tcl] r]
set body [read $f]
close $f
set anchor {source [file join $root scripts ps_debug_common.tcl]}
if {[string first $anchor $body]<0 || [string first $anchor $body [expr {[string first $anchor $body]+[string length $anchor]}]]>=0} {
    puts stderr PRIVATE_NETWORK_CONFIG_FAILED
    exit 1
}
eval [string map [list $anchor {}] $body]
