# Operate only the new-board framebuffer platform. The caller probes its PL identity first.
set root [file normalize [file join [file dirname [info script]] ..]]
set action [lindex $argv 0]
set serial [lindex $argv 1]
set elf [lindex $argv 2]
set init [lindex $argv 3]
set result [lindex $argv 4]
if {$action ni {run sample quiesce}} {error "Expected run/sample/quiesce"}
if {$action ne "sample"} {
    set argv [list $serial]
    source [file join $root scripts z7_nano_halt.tcl]
}
connect -url TCP:127.0.0.1:3121
after 1000
set cpu [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpu] != 1 || [dict get [lindex $cpu 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0 target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
proc read32 {address} {return [mrd -force -value $address]}
proc wait_register {address mask expected ms} {
    set deadline [expr {[clock milliseconds]+$ms}]
    while {([read32 $address]&$mask)!=$expected} {
        if {[clock milliseconds]>=$deadline} {error "Register timeout at $address"}
        after 10
    }
}
proc report {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [read32 0x1e008]
        set words [mrd -force -value 0x1e000 16]
        set after [read32 0x1e008]
        if {$before==$after && ($after&1)==0 && [lindex $words 2]==$after} {return $words}
        if {[clock milliseconds]>=$deadline} {error "No coherent display report"}
        after 10
    }
}
if {$action eq "quiesce"} {
    source [file join $root scripts z7_nano_usb_stop.tcl]
    usb0_stop
    # A freshly configured platform can still be isolated from the PS.
    set level_shifters [expr {[read32 0xf8000900]&15}]
    if {$level_shifters==0} {
        puts "PS_PL_ISOLATED_NO_DMA_ACCESS"
        puts PASS_Z7_NANO_DISPLAY_QUIESCE
        disconnect
        exit
    }
    if {$level_shifters!=15} {error "Partially enabled PS-PL interface; inspect before quiesce"}
    if {[read32 0x43c10000]!=0x46564231} {error "Wrong video peripheral"}
    mwr -force 0x43c10004 0
    wait_register 0x43c10004 2 0 3000
    mwr -force 0x43c00000 0
    wait_register 0x43c00004 1 1 3000
    mwr -force 0x43c00000 4
    wait_register 0x43c00000 4 0 3000
    wait_register 0x43c00004 1 1 3000
    puts PASS_Z7_NANO_DISPLAY_QUIESCE
    disconnect
    exit
}
if {$action eq "run"} {
    if {[read32 0xf8f02100]&1} {error "Active L2 cache"}
    if {[read32 0xf8000910]&2} {error "Required OCM bank is not mapped low"}
    source $init
    proc mask_poll {address mask} {
        set deadline [expr {[clock milliseconds]+10000}]
        while {([read32 $address]&$mask)==0} {
            if {[clock milliseconds]>=$deadline} {error "PS initialization timeout at $address"}
            after 10
        }
    }
    proc mask_delay {address ms} {after [expr {max(1,int(ceil(double($ms))))}]}
    ps7_init
    ps7_post_config
    if {[read32 0x43c10000]!=0x46564231 || [read32 0x43c10004]!=0 ||
        [read32 0x43c1000c]!=0 || [read32 0x43c10010]!=0 || [read32 0x43c10014]!=0 ||
        ([read32 0x43c00004]&1)!=1} {error "Fresh display reset state required"}
    rst -processor -stop
    dow $elf
    mwr -force 0x1e000 0 16
    con
    set deadline [expr {[clock milliseconds]+20000}]
    while {1} {
        set values [report]
        if {[lindex $values 1]==0x600d600d || [lindex $values 1]==0xbad0bad0} {break}
        if {[clock milliseconds]>=$deadline} {stop; error "Display startup timeout: $values"}
        after 100
    }
} else {set values [report]}
if {[state] ne "Running" || [lindex $values 0]!=0x46504231 || [lindex $values 1]!=0x600d600d ||
    [lindex $values 3]!=3 || [lindex $values 4]<4 || [lindex $values 6]<2 ||
    [lindex $values 7]!=0 || [lindex $values 8]!=0 || ([lindex $values 9]&0xf0)!=0 ||
    [lindex $values 10]!=614400 || ([lindex $values 14]&0x3805)!=0 || [lindex $values 15]!=0} {
    error "Display validation failed: $values"
}
set deadline [expr {[clock milliseconds]+5000}]
while {1} {
    after 100
    set next [report]
    if {[lindex $next 1]!=0x600d600d || [lindex $next 7]!=0 || [lindex $next 8]!=0 ||
        ([lindex $next 9]&0xf0)!=0 || [state] ne "Running"} {error "Display runtime error: $next"}
    if {[lindex $next 4]>[lindex $values 4] && [lindex $next 6]>[lindex $values 6] &&
        [lindex $next 13]>[lindex $values 13]} {break}
    if {[clock milliseconds]>=$deadline} {error "Display counters are not progressing"}
}
set f [open $result w]
puts $f "{\"status\":\"passed\",\"flips\":[lindex $next 4],\"good_frames\":[lindex $next 6],\"underflows\":[lindex $next 7],\"format_errors\":[lindex $next 8],\"vdma_status\":[lindex $next 9],\"last_frame_pixels\":[lindex $next 10],\"raw_frames\":[lindex $next 13],\"cpu_sctlr\":[lindex $next 14],\"error_code\":[lindex $next 15],\"report\":\[[join $next ,]\]}"
close $f
puts "DISPLAY_PROGRESS $next"
puts "PASS_Z7_NANO_DISPLAY_[string toupper $action]"
disconnect
