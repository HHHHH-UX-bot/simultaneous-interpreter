set action [lindex $argv 0]
set serial [lindex $argv 1]
set elf [lindex $argv 2]
set folder [lindex $argv 3]
set expected_generation [lindex $argv 4]
if {$action ni {start sample stop}} {error "Expected start/sample/stop"}
source [file join [file dirname [info script]] z7_nano_usb_stop.tcl]
connect -url TCP:127.0.0.1:3121
set target_deadline [expr {[clock milliseconds]+10000}]
foreach core {0 1} {
    while {1} {
        set matches [targets -target-properties -filter "name =~ \"*Cortex-A9*#$core\""]
        if {[llength $matches]==1 && [dict exists [lindex $matches 0] jtag_cable_serial]} {
            if {[dict get [lindex $matches 0] jtag_cable_serial] ne $serial} {error "Unexpected live Host target"}
            break
        }
        if {[clock milliseconds]>=$target_deadline} {error "Live Host cable identity unavailable"}
        after 200
    }
}
proc read32 {address} {return [mrd -force -value $address]}
proc select_core {core} {targets -set -filter "name =~ \"*Cortex-A9*#$core\""}
proc coherent {address words} {
    for {set i 0} {$i<8} {incr i} {
        set before [read32 [expr {$address+4}]]
        set values [mrd -force -value $address $words]
        set after [read32 [expr {$address+4}]]
        if {$before==$after && ($after&1)==0 && [lindex $values 1]==$after} {return $values}
        after 10
    }
    error "No coherent touch block at $address"
}
proc require_display {} {
    select_core 0
    if {[state] ne "Running" || [read32 0x1e000]!=0x46504231 || [read32 0x1e004]!=0x600d600d} {error "Display CPU0 is not ready"}
    select_core 1
}
require_display
if {[read32 0xe0002100]!=0x01000040 || [read32 0xf8000910]&1} {error "USB0/OCM identity mismatch"}
set owned 0
set generation_known 0
set code [catch {
    if {$action eq "start"} {
        if {![string match "Stopped:*" [state]]} {error "Stop existing CPU1 before starting Host"}
        usb0_stop
        set owned 1
        set f [open [file join $folder owned.json] w]
        puts $f "{\"action\":\"start\",\"cable_serial\":\"$serial\"}"
        close $f
        dow $elf
        mwr -force 0xe000 0 16
        con
        set deadline [expr {[clock milliseconds]+15000}]
        while {[read32 0xe008]!=4} {
            if {[read32 0xe004]==0xbad0bad0} {error "Host initialization failed"}
            if {[clock milliseconds]>=$deadline} {error "Host enumeration timeout"}
            after 50
        }
        set expected_generation [read32 0xd050]
        set generation_known 1
        set f [open [file join $folder owned.json] w]
        puts $f "{\"action\":\"start\",\"cable_serial\":\"$serial\",\"generation\":$expected_generation}"
        close $f
    } else {
        if {[read32 0xe000]!=0x55534831 || [read32 0xd000]!=0x544f5531 ||
            [read32 0xd050]!=$expected_generation} {error "Live Host generation mismatch"}
        set owned 1
        set generation_known 1
        set f [open [file join $folder owned.json] w]
        puts $f "{\"action\":\"$action\",\"cable_serial\":\"$serial\",\"generation\":$expected_generation}"
        close $f
    }
    if {$action eq "stop"} {
        if {[state] eq "Running" && [read32 0xe004]!=0xbad0bad0} {
            mwr -force 0xe03c 1
            set deadline [expr {[clock milliseconds]+3000}]
            while {[read32 0xe004]!=0x600d600d} {
                if {[read32 0xe004]==0xbad0bad0} {error "Host shutdown failed"}
                if {[clock milliseconds]>=$deadline} {error "Host shutdown timeout"}
                after 10
            }
        }
        if {[state] eq "Running"} {stop}
        if {![string match "Stopped:*" [state]]} {error "Host stop unconfirmed"}
        usb0_stop
    } else {
        set heartbeat [read32 0xe038]
        after 1000
        set report [mrd -force -value 0xe000 16]
        if {[state] ne "Running" || [lindex $report 0]!=0x55534831 ||
            [lindex $report 1]!=1 || [lindex $report 2]!=4 || [lindex $report 3]!=0 ||
            [lindex $report 4]!=0x1a86 || [lindex $report 5]!=0xe5e3 ||
            [lindex $report 14]==$heartbeat || ([lindex $report 12]&0x1005)!=0x1005 ||
            ([read32 0xe0002140]&1)==0 || ([read32 0xe0002144]&0x1000)!=0} {error "Live Host is not progressing"}
    }
    set report [mrd -force -value 0xe000 16]
    set touch [coherent 0xd000 64]
    set deadline [expr {[clock milliseconds]+3000}]
    while {1} {
        set feedback [coherent 0x1e040 16]
        if {[lindex $feedback 0]==0x544f5532 && [lindex $feedback 2]==$expected_generation} {break}
        if {[clock milliseconds]>=$deadline} {error "Current touch feedback missing"}
        after 10
    }
    if {[lindex $touch 0]!=0x544f5531 || [lindex $touch 20]!=$expected_generation ||
        (([lindex $touch 16]-[lindex $touch 17])&0xffffffff)>8} {error "Touch queue identity/range mismatch"}
    set host_state [state]
    require_display
    set f [open [file join $folder result.json] w]
    puts $f "{\"status\":\"passed\",\"action\":\"$action\",\"generation\":$expected_generation,\"host_cpu\":\"$host_state\",\"display_cpu\":\"Running\",\"report\":\[[join $report ,]\],\"touch\":\[[join $touch ,]\],\"feedback\":\[[join $feedback ,]\]}"
    close $f
} message options]
if {$code && $owned} {
    select_core 1
    if {$generation_known && ([read32 0xe000]!=0x55534831 || [read32 0xd000]!=0x544f5531 ||
        [read32 0xd050]!=$expected_generation)} {
        disconnect
        error "Live Host generation changed; cleanup not owned: $message"
    }
    if {[state] eq "Running"} {stop}
    if {![string match "Stopped:*" [state]]} {error "Live Host failure stop unconfirmed: $message"}
    usb0_stop
    require_display
    set f [open [file join $folder cleanup.json] w]
    puts $f "{\"status\":\"passed\",\"host_cpu\":\"Stopped\",\"usb_dma\":\"Halted\",\"display_cpu\":\"Running\"}"
    close $f
}
disconnect
if {$code} {return -options $options $message}
puts "PASS_Z7_NANO_USB_LIVE_[string toupper $action]"
