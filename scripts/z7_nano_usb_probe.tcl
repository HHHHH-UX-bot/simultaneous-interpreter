set serial [lindex $argv 0]
set elf [lindex $argv 1]
set result [lindex $argv 2]
source [file join [file dirname [info script]] z7_nano_usb_stop.tcl]
connect -url TCP:127.0.0.1:3121
after 1000
foreach core {0 1} {
    set filter "name =~ \"*Cortex-A9*#$core\""
    set matches [targets -target-properties -filter $filter]
    if {[llength $matches]!=1 || [dict get [lindex $matches 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU$core target"}
}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Display CPU0 must remain running"}
targets -set -filter {name =~ "*Cortex-A9*#1"}
if {[state] eq "Running"} {stop}
if {![string match "Stopped:*" [state]]} {error "Probe CPU1 did not stop"}
if {[mrd -force -value 0xf8000910]&1} {error "Core1 low OCM bank not mapped"}
usb0_stop
dow $elf
mwr -force 0xe000 0 16
con
set deadline [expr {[clock milliseconds]+15000}]
while {1} {
    set status [mrd -force -value 0xe004]
    if {$status==0x600d600d || $status==0xbad0bad0} {break}
    if {[clock milliseconds]>=$deadline} {stop; error "USB controller/PHY probe timeout"}
    after 50
}
stop
if {![string match "Stopped:*" [state]]} {error "Probe CPU1 stop unconfirmed"}
set words [mrd -force -value 0xe000 16]
puts "USB_PROBE_RESULT $words"
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Display CPU0 stopped during USB probe"}
if {[lindex $words 0]!=0x55535031 || [lindex $words 1]!=0x600d600d || [lindex $words 2]!=6 ||
    [lindex $words 3]!=0 || [lindex $words 8]!=0x0424 || [lindex $words 9]!=7 ||
    [lindex $words 10]!=0x5a || ([lindex $words 14]&0x3805)!=0} {error "USB controller/PHY probe failed: $words"}
set f [open $result w]
puts $f "{\"status\":\"passed\",\"phase\":6,\"phy_vid\":[lindex $words 8],\"phy_pid\":[lindex $words 9],\"scratch\":[lindex $words 10],\"portsc\":[lindex $words 11],\"usbmode\":[lindex $words 12],\"display_cpu\":\"Running\",\"probe_cpu\":\"Stopped\",\"report\":\[[join $words ,]\],\"scope\":\"USB0 controller and ULPI PHY; no device descriptor or HID yet\"}"
close $f
disconnect
puts PASS_Z7_NANO_USB_PROBE
