# Only the existing simulated UI is accessed; no reset, DDR init or ELF download.
set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts ps_debug_common.tcl]
set action [lindex $argv 0]
set folder [lindex $argv 1]
set command [lindex $argv 2]
set expected_epoch [lindex $argv 3]
if {$action ni {sample command capture}} {error "Expected sample/command/capture"}
set connected 0
set stopped 0
set exit_code 0
proc ui_word {address} {return [mrd -force -value $address]}
proc ui_info {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [ui_word 0x1e084]
        set values [mrd -force -value 0x1e080 16]
        set after [ui_word 0x1e084]
        if {$before==$after && ($after&1)==0 && [lindex $values 1]==$after} {
            if {[lindex $values 0]!=0x55493131 || [lindex $values 15]!=3 ||
                [lindex $values 2]!=0x1b00000 || [lindex $values 3]<10000 || [lindex $values 3]>32768 ||
                [lindex $values 4]<10000 || [lindex $values 4]>16384 || [lindex $values 5]>1} {
                error "Invalid simulated UI identity/layout/guard report"
            }
            return $values
        }
        if {[clock milliseconds]>=$deadline} {error "No coherent UI report"}
        after 10
    }
}
proc display_info {} {
    set deadline [expr {[clock milliseconds]+5000}]
    while {1} {
        set before [ui_word 0x1e008]
        set values [mrd -force -value 0x1e000 16]
        set after [ui_word 0x1e008]
        if {$before==$after && ($after&1)==0 && [lindex $values 2]==$after} {return $values}
        if {[clock milliseconds]>=$deadline} {error "No coherent display report"}
        after 10
    }
}
try {
    connect -url TCP:127.0.0.1:3121
    set connected 1
    select_ps_core 0
    if {[state] ne "Running"} {error "Subtitle CPU is not running"}
    set before [ui_info]
    if {$action eq "command"} {
        if {$command<1 || $command>11 || [ui_word 0x1e0c0]!=0x55494331} {error "Invalid UI command/identity"}
        if {$expected_epoch eq ""} {set expected_epoch [lindex $before 10]}
        set request [expr {([ui_word 0x1e0d0]+1)&0xffffffff}]
        mwr -force 0x1e0c8 $expected_epoch
        mwr -force 0x1e0cc $command
        mwr -force 0x1e0c4 $request
        set deadline [expr {[clock milliseconds]+5000}]
        while {1} {
            set info [ui_info]
            if {[ui_word 0x1e0d0]==$request && [lindex $info 14]==$request &&
                [lindex $info 7]>[lindex $before 7]} {break}
            if {[clock milliseconds]>=$deadline} {error "UI command acknowledgment/display timeout"}
            after 20
        }
        puts "UI_COMMAND_RESULT $request [ui_word 0x1e0d4] [ui_word 0x1e0d8]"
    } elseif {$action eq "capture"} {
        # Freeze the CPU producer; VDMA remains running on an immutable buffer.
        halt_ps_core 0
        set stopped 1
        # A halt can interrupt the seqlock writer. Use the already coherent,
        # immutable layout, then choose the actual VDMA buffer after settling.
        set info $before
        set deadline [expr {[clock milliseconds]+1500}]
        while {1} {
            set park [ui_word 0x43c00028]
            set desired [expr {$park&0x1f}]
            set current [expr {($park>>16)&0x1f}]
            if {$current==$desired} {break}
            if {[clock milliseconds]>=$deadline} {error "VDMA park did not settle for capture"}
            after 10
        }
        if {$current>1} {error "Unexpected framebuffer index"}
        set snapshot [expr {[lindex $info 2]+$current*[lindex $info 3]}]
        set header [mrd -force -value $snapshot 8]
        if {[lindex $header 0]!=0x55495331 || [lindex $header 1]!=1 ||
            [lindex $header 2]!=[lindex $info 3] || [lindex $header 3]!=[lindex $info 4]} {
            error "Invalid active framebuffer snapshot"
        }
        file mkdir $folder
        mrd -force -bin -file [file join $folder snapshot.bin] $snapshot [expr {[lindex $info 3]/4}]
        set base [expr {$current?0x01400000:0x01000000}]
        mrd -force -bin -file [file join $folder framebuffer.bin] $base 614400
        puts "UI_CAPTURE_RESULT $current $base $snapshot"
        con
        set stopped 0
        if {[state] ne "Running"} {error "CPU resume after capture unconfirmed"}
    } else {set info $before}
    puts "UI_RESULT [ui_info]"
    puts "DISPLAY_RESULT [display_info]"
    if {[state] ne "Running"} {error "CPU is not running after UI operation"}
    puts "PASS_SUBTITLE_[string toupper $action]"
} on error {message options} {
    puts stderr "ERROR_SUBTITLE_UI: $message"
    set exit_code 1
} finally {
    # Failure deliberately leaves a stopped CPU stopped. The Python controller
    # records/cleans this state rather than resuming after an uncertain capture.
    if {$connected} {catch {disconnect}}
}
exit $exit_code
