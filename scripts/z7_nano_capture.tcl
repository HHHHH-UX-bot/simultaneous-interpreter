# Freeze the CPU producer only; the VDMA continues displaying its complete buffer.
set serial [lindex $argv 0]
set folder [lindex $argv 1]
connect -url TCP:127.0.0.1:3121
after 1000
set cpu [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]
if {[llength $cpu]!=1 || [dict get [lindex $cpu 0] jtag_cable_serial] ne $serial} {error "Unexpected CPU0 target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "UI CPU is not running"}
set deadline [expr {[clock milliseconds]+5000}]
while {1} {
    set before [mrd -force -value 0x1e084]
    set info [mrd -force -value 0x1e080 16]
    set after [mrd -force -value 0x1e084]
    if {$before==$after && ($after&1)==0 && [lindex $info 1]==$after} {break}
    if {[clock milliseconds]>=$deadline} {error "No coherent UI report"}
    after 10
}
if {[lindex $info 0]!=0x55493131 || [lindex $info 2]!=0x1b00000 ||
    [lindex $info 3]!=11456 || [lindex $info 4]!=11392 || [lindex $info 15]!=3} {error "Wrong UI ABI"}
stop
if {![string match "Stopped:*" [state]]} {error "UI CPU did not stop"}
set capture_failed [catch {
    set deadline [expr {[clock milliseconds]+1500}]
    while {1} {
        set park [mrd -force -value 0x43c00028]
        set desired [expr {$park&0x1f}]
        set current [expr {($park>>16)&0x1f}]
        if {$desired==$current} {break}
        if {[clock milliseconds]>=$deadline} {error "VDMA park did not settle"}
        after 10
    }
    if {$current>1} {error "Invalid framebuffer index"}
    set snapshot [expr {0x1b00000+$current*11456}]
    set header [mrd -force -value $snapshot 8]
    if {[lindex $header 0]!=0x55495331 || [lindex $header 1] ni {1 2} ||
        [lrange $header 2 3] ne {11456 11392}} {error "Wrong active snapshot"}
    set base [expr {$current?0x01400000:0x01000000}]
    mrd -force -bin -file [file join $folder snapshot.bin] $snapshot 2864
    mrd -force -bin -file [file join $folder framebuffer.bin] $base 614400
} capture_message capture_options]
set resume_failed [catch {
    con
    if {[state] ne "Running"} {error "CPU resume unconfirmed"}
} resume_message]
disconnect
if {$capture_failed} {
    if {$resume_failed} {puts stderr "CPU_RESUME_ERROR: $resume_message"}
    return -options $capture_options $capture_message
}
if {$resume_failed} {error $resume_message}
set f [open [file join $folder result.json] w]
puts $f "{\"status\":\"passed\",\"buffer\":$current,\"base\":$base,\"snapshot\":$snapshot}"
close $f
puts PASS_Z7_NANO_CAPTURE
