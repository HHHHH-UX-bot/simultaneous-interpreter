set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output ps_bringup]
set action [lindex $argv 0]
set elf_path [lindex $argv 1]
source [file join $root scripts ps_debug_common.tcl]
if {$action ni {probe run}} {error "Use run_ps_bringup.py probe/run"}
set connected 0
set core_selected 0
set exit_code 0
proc read32 {addr} {return [mrd -force -value $addr]}
proc mask_write {addr mask val} {
    mwr -force $addr [expr {([read32 $addr] & ~$mask) | ($val & $mask)}]
}
proc bounded_poll {addr mask} {
    set deadline [expr {[clock milliseconds] + 10000}]
    while {([read32 $addr] & $mask) == 0} {
        if {[clock milliseconds] >= $deadline} {error "Initialization timeout at $addr mask $mask"}
        after 10
    }
}
try {
    connect -url TCP:127.0.0.1:3121
    set connected 1
    if {[llength [targets -target-properties -filter {name == "xc7z010"}]] != 1} {error "Expected exactly one xc7z010"}
    if {[llength [targets -target-properties -filter {name =~ "*Cortex-A9*#0"}]] != 1} {error "Expected one Cortex-A9 core 0"}
    select_ps_core 0
    set core_selected 1
    puts "PS_VERSION [read32 0xf8007080]"
    puts "BOOT_MODE [read32 0xf800025c]"
    puts "DDR_CTRL_BEFORE [read32 0xf8006000]"
    puts "L2_CTRL_BEFORE [read32 0xf8f02100]"
    puts "SCU_CTRL_BEFORE [read32 0xf8f00000]"
    if {([read32 0xf8000910] & 2) != 0} {error "OCM bank 1 not mapped at low addresses"}
    if {$action eq "probe"} {
        puts "PASS_PS_PROBE"
    } else {
        halt_ps_core 1
        puts "CPU1_BEFORE [rrd pc]"
        halt_ps_core 0
        puts "CPU0_BEFORE [rrd pc]"
        if {([read32 0xf8f02100] & 1) != 0} {error "Active L2 cache requires a separate startup review"}
        source [file join $root hardware zynq_mini_revb ps7_init.tcl]
        # Vendor data remains unchanged; replace helpers that otherwise permit
        # warning-only or effectively unbounded initialization failures.
        proc mask_poll {addr mask} {bounded_poll $addr $mask}
        proc mask_delay {addr val} {after [expr {max(1,int(ceil(double($val))))}]}
        puts PS_INITIALIZATION_BEGIN
        ps7_init
        puts "DDR_STATUS_AFTER [read32 0xf8006054]"
        rst -processor -stop
        if {![file isfile $elf_path]} {error "Missing attempt ELF snapshot"}
        dow $elf_path
        mwr -force 0x0001e000 0 16
        puts CPU_MEMORY_TEST_BEGIN
        con
        set deadline [expr {[clock milliseconds] + 60000}]
        while {1} {
            set result_state [read32 0x0001e004]
            if {$result_state == 0x600d600d || $result_state == 0xbad0bad0} {break}
            if {[clock milliseconds] >= $deadline} {error "CPU memory test timed out"}
            after 100
        }
        halt_ps_core 0
        set values [mrd -force -value 0x0001e000 13]
        if {[llength $values] != 13} {error "Missing CPU result mailbox"}
        puts "CPU_RESULT $values"
        if {[lindex $values 0] != 0x50534431 || [lindex $values 1] != 0x600d600d ||
            [lindex $values 2] != 8 || [lindex $values 3] != 5242948 ||
            [lindex $values 8] != 0x01000000 || [lindex $values 9] != 0x00400000 ||
            ([lindex $values 12] & 0x3805) != 0} {
            error "CPU/DDR verification failed: $values"
        }
        puts PASS_PS_DDR_RUN
    }
} on error {message options} {
    puts stderr "ERROR_PS_BRINGUP: $message"
    if {$core_selected && $action eq "run"} {
        catch {halt_ps_core 1}
        catch {halt_ps_core 0}
    }
    set exit_code 1
} finally {
    if {$connected} {catch {disconnect}}
}
exit $exit_code
