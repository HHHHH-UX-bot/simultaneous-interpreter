set serial [lindex $argv 0]
set elf [lindex $argv 1]
set folder [lindex $argv 2]
set observe_ms [lindex $argv 3]
set power_trace [lindex $argv 4]
if {$power_trace eq ""} {set power_trace 0}
if {$power_trace ni {0 1}} {error "Invalid power trace option"}
if {![string is integer -strict $observe_ms] || $observe_ms<1000 || $observe_ms>300000} {error "Invalid observation interval"}
source [file join [file dirname [info script]] z7_nano_usb_stop.tcl]
connect -url TCP:127.0.0.1:3121
after 1000
foreach core {0 1} {
    set matches [targets -target-properties -filter "name =~ \"*Cortex-A9*#$core\""]
    if {[llength $matches]!=1 || [dict get [lindex $matches 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU$core target"}
}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Display CPU0 must be running"}
targets -set -filter {name =~ "*Cortex-A9*#1"}
if {[state] eq "Running"} {stop}
if {![string match "Stopped:*" [state]] || [mrd -force -value 0xf8000910]&1} {error "Host CPU1/OCM mismatch"}
usb0_stop
proc power_read32 {address} {return [mrd -force -value $address]}
proc power_wait_viewport {mask} {
    set deadline [expr {[clock milliseconds]+500}]
    while {[power_read32 0xe0002170]&$mask} {
        if {[clock milliseconds]>=$deadline} {error "Power trace ULPI read timeout"}
        after 1
    }
}
proc power_ulpi_read {address} {
    if {([power_read32 0xe0002170]&0x08000000)==0} {
        mwr -force 0xe0002170 0x80000000
        power_wait_viewport 0x80000000
    }
    mwr -force 0xe0002170 [expr {0x40000000|($address<<16)}]
    power_wait_viewport 0x40000000
    return [expr {([power_read32 0xe0002170]>>8)&255}]
}
proc trace_power {file read_phy} {
    set row [list [clock milliseconds] [power_read32 0xe038] [power_read32 0xe008] \
        [power_read32 0xe00c] [power_read32 0xe028] [power_read32 0xe0002184] \
        [power_read32 0xe0002140] [power_read32 0xe0002144] [power_read32 0xe00021a4]]
    # CPU1 only accesses the viewport during initialization; read PHY after mounting.
    if {$read_phy} {
        lappend row [power_ulpi_read 4] [power_ulpi_read 10] [power_ulpi_read 19]
    } else {lappend row "" "" ""}
    puts $file [join $row ,]
    flush $file
}
set trace_file ""
set code [catch {
    if {$power_trace} {
        set trace_file [open [file join $folder power_trace.csv] w]
        puts $trace_file "host_ms,heartbeat,phase,error,reports,portsc,usbcmd,usbsts,otgsc,phy_function_control,phy_otg_control,phy_interrupt_status"
        trace_power $trace_file 0
    }
    dow $elf
    mwr -force 0xe000 0 16
    con
    set deadline [expr {[clock milliseconds]+15000}]
    while {[mrd -force -value 0xe008]!=4} {
        if {$power_trace} {trace_power $trace_file 0}
        if {[mrd -force -value 0xe004]==0xbad0bad0} {error "Host initialization failed"}
        if {[clock milliseconds]>=$deadline} {error "Device enumeration timeout"}
        after 50
    }
    puts "USB_ENUM_MOUNTED [mrd -force -value 0xe000 16]"
    if {$power_trace} {trace_power $trace_file 1}
    puts USB_CAPTURE_READY
    flush stdout
    set deadline [expr {[clock milliseconds]+$observe_ms}]
    while {[clock milliseconds]<$deadline} {
        if {$power_trace} {trace_power $trace_file 1}
        if {[mrd -force -value 0xe004]==0xbad0bad0} {error "Host failed during observation"}
        after [expr {min(1000,max(0,$deadline-[clock milliseconds]))}]
    }
    puts "USB_ENUM_BEFORE_STOP [mrd -force -value 0xe000 16]"
    if {[mrd -force -value 0xe004]==0xbad0bad0} {error "Host failed before stop request"}
    mwr -force 0xe03c 1
    puts "USB_STOP_REQUEST [mrd -force -value 0xe03c]"
    set deadline [expr {[clock milliseconds]+3000}]
    while {[mrd -force -value 0xe004]!=0x600d600d} {
        if {[mrd -force -value 0xe004]==0xbad0bad0} {error "Host shutdown failed"}
        if {[clock milliseconds]>=$deadline} {error "Host shutdown timeout"}
        after 10
    }
} message options]
if {$trace_file ne ""} {close $trace_file}
if {[state] eq "Running"} {stop}
if {![string match "Stopped:*" [state]]} {error "Host CPU1 stop not confirmed"}
usb0_stop
set words [mrd -force -value 0xe000 16]
puts "USB_HOST_RESULT $words"
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Display CPU0 stopped"}
targets -set -filter {name =~ "*Cortex-A9*#1"}
mrd -force -bin -file [file join $folder snapshot.bin] 0xe100 932
mrd -force -bin -file [file join $folder touch.bin] 0xd000 64
set captured 0
for {set i 0} {$i<8} {incr i} {
    set before [mrd -force -value 0x1e044]
    mrd -force -bin -file [file join $folder feedback.bin] 0x1e040 16
    set after [mrd -force -value 0x1e044]
    if {$before==$after && ($after&1)==0} {set captured 1;break}
    after 10
}
if {!$captured} {error "Touch feedback changed during capture"}
if {$code} {
    set f [open [file join $folder result.json] w]
    puts $f "{\"status\":\"failed\",\"report\":\[[join $words ,]\],\"display_cpu\":\"Running\",\"host_cpu\":\"Stopped\",\"usb_dma\":\"Halted\"}"
    close $f
    return -options $options "$message; report=$words"
}
if {[lindex $words 0]!=0x55534831 || [lindex $words 1]!=0x600d600d || [lindex $words 2]!=6 ||
    [lindex $words 3]!=0 || [lindex $words 6]!=1 || [lindex $words 8]==0 ||
    ([lindex $words 13]&0x1000)==0} {error "Host report invalid"}
set f [open [file join $folder result.json] w]
puts $f "{\"status\":\"passed\",\"report\":\[[join $words ,]\],\"vid\":[lindex $words 4],\"pid\":[lindex $words 5],\"speed\":[lindex $words 7],\"config_length\":[lindex $words 8],\"hid_count\":[lindex $words 9],\"report_count\":[lindex $words 10],\"display_cpu\":\"Running\",\"host_cpu\":\"Stopped\",\"usb_dma\":\"Halted\"}"
close $f
disconnect
puts PASS_Z7_NANO_USB_HOST
