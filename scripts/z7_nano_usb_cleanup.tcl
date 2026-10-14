set serial [lindex $argv 0]
set result [lindex $argv 1]
source [file join [file dirname [info script]] z7_nano_usb_stop.tcl]
connect -url TCP:127.0.0.1:3121
after 1000
foreach core {0 1} {
    set matches [targets -target-properties -filter "name =~ \"*Cortex-A9*#$core\""]
    if {[llength $matches]!=1 || [dict get [lindex $matches 0] jtag_cable_serial] ne $serial} {error "Unexpected cleanup target"}
}
targets -set -filter {name =~ "*Cortex-A9*#1"}
if {[state] eq "Running"} {stop}
if {![string match "Stopped:*" [state]]} {error "Cleanup CPU1 stop unconfirmed"}
usb0_stop
targets -set -filter {name =~ "*Cortex-A9*#0"}
set display_state [state]
set f [open $result w]
puts $f "{\"status\":\"passed\",\"host_cpu\":\"Stopped\",\"usb_dma\":\"Halted\",\"display_cpu\":\"$display_state\"}"
close $f
disconnect
puts PASS_Z7_NANO_USB_CLEANUP
