set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts ps_debug_common.tcl]
set action [lindex $argv 0]
set elf [lindex $argv 1]
set init [lindex $argv 2]
if {$action ni {run sample}} {error "Expected run/sample"}
set connected 0
set exit_code 0
proc read32 {addr} {return [mrd -force -value $addr]}
proc mask_write {addr mask val} {mwr -force $addr [expr {([read32 $addr]&~$mask)|($val&$mask)}]}
proc bounded_poll {addr mask} {
    set deadline [expr {[clock milliseconds]+10000}]
    while {([read32 $addr]&$mask)==0} {
        if {[clock milliseconds]>=$deadline} {error "PS init timeout at $addr"}
        after 10
    }
}
proc read_report {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [read32 0x0001e008]
        set values [mrd -force -value 0x0001e000 16]
        set after [read32 0x0001e008]
        if {$before==$after && ($after&1)==0 && [lindex $values 2]==$after} {return $values}
        if {[clock milliseconds]>=$deadline} {error "No coherent display report"}
        after 10
    }
}
try {
    connect -url TCP:127.0.0.1:3121
    set connected 1
    select_ps_core 0
    if {$action eq "run"} {
        halt_ps_core 1
        halt_ps_core 0
        if {([read32 0xf8f02100]&1)!=0} {error "Active L2 cache requires startup review"}
        source $init
        proc mask_poll {addr mask} {bounded_poll $addr $mask}
        proc mask_delay {addr val} {after [expr {max(1,int(ceil(double($val))))}]}
        ps7_init
        ps7_post_config
        if {([read32 0xf8000910]&2)!=0} {error "OCM bank 1 not mapped low"}
        puts "VIDEO_ID [read32 0x43c10000]"
        if {[read32 0x43c10000]!=0x46564231} {error "Video register identity mismatch"}
        if {[read32 0x43c10004]!=0 || [read32 0x43c1000c]!=0 ||
            [read32 0x43c10010]!=0 || [read32 0x43c10014]!=0 || ([read32 0x43c00004]&1)!=1} {
            error "Fresh FPGA reset state required before application launch"
        }
        rst -processor -stop
        dow $elf
        mwr -force 0x0001e000 0 16
        con
        set deadline [expr {[clock milliseconds]+20000}]
        while {1} {
            set values [read_report]
            set status [lindex $values 1]
            if {$status==0x600d600d || $status==0xbad0bad0} {break}
            if {[clock milliseconds]>=$deadline} {error "Display application timeout: $values"}
            after 100
        }
    } else {set values [read_report]}
    puts "DISPLAY_RESULT $values"
    puts "CPU0_STATE [state]"
    if {[state] ne "Running"} {error "Display CPU is not running"}
    if {[llength $values]!=16 || [lindex $values 0]!=0x46504231 || [lindex $values 1]!=0x600d600d ||
        [lindex $values 3]!=3 || [lindex $values 4]<4 || [lindex $values 6]<2 ||
        [lindex $values 7]!=0 || [lindex $values 8]!=0 || ([lindex $values 9]&0xf0)!=0 ||
        [lindex $values 10]!=614400 || ([lindex $values 14]&0x3805)!=0 || [lindex $values 15]!=0} {
        error "Display application verification failed"
    }
    set before_values $values
    set progress_deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        after 100
        set next [read_report]
        if {[lindex $next 1]==0xbad0bad0} {error "Display application reported failure: $next"}
        if {[lindex $next 4]>[lindex $before_values 4] &&
            [lindex $next 6]>[lindex $before_values 6] && [lindex $next 13]>[lindex $before_values 13]} {break}
        if {[clock milliseconds]>=$progress_deadline} {error "Display counters are not progressing"}
    }
    if {[lindex $next 7]!=0 || [lindex $next 8]!=0 || ([lindex $next 9]&0xf0)!=0 || [state] ne "Running"} {
        error "Display errors during progress verification"
    }
    puts "DISPLAY_PROGRESS $next"
    puts "VIDEO_REGISTERS [mrd -force -value 0x43c10000 11]"
    puts "VDMA_REGISTERS [mrd -force -value 0x43c00000 12]"
    puts "PASS_FRAMEBUFFER_[string toupper $action]"
} on error {message options} {
    puts stderr "ERROR_PS_BRINGUP: $message"
    if {$action eq "run" && $connected} {
        catch {halt_ps_core 1}
        catch {halt_ps_core 0}
    }
    set exit_code 1
} finally {
    if {$connected} {catch {disconnect}}
}
exit $exit_code
