set serial [lindex $argv 0]
set elf [lindex $argv 1]
set init [lindex $argv 2]
set result [lindex $argv 3]
set root [file normalize [file join [file dirname [info script]] ..]]
set argv [list $serial]
source [file join $root scripts z7_nano_halt.tcl]
connect -url TCP:127.0.0.1:3121
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[mrd -force -value 0xf8f02100] & 1} {error "Active L2 cache; PS initialization not permitted"}
if {[mrd -force -value 0xf8000910] & 2} {error "Required low OCM bank is not mapped"}
source $init
proc mask_poll {address mask} {
    set deadline [expr {[clock milliseconds]+10000}]
    while {([mrd -force -value $address] & $mask) == 0} {
        if {[clock milliseconds] >= $deadline} {error "PS initialization timeout at $address"}
        after 10
    }
}
proc mask_delay {address ms} {after [expr {max(1,int(ceil(double($ms))))}]}
ps7_init
rst -processor -stop
dow $elf
mwr -force 0x0001e000 0 16
con
set deadline [expr {[clock milliseconds]+60000}]
while {1} {
    set state [mrd -force -value 0x0001e004]
    if {$state == 0x600d600d || $state == 0xbad0bad0} {break}
    if {[clock milliseconds] >= $deadline} {stop; error "DDR test timeout"}
    after 100
}
stop
set values [mrd -force -value 0x0001e000 13]
puts "DDR_RESULT $values"
if {[lindex $values 0] != 0x50534431 || [lindex $values 1] != 0x600d600d ||
    [lindex $values 2] != 8 || [lindex $values 3] != 5242948 ||
    [lindex $values 8] != 0x01000000 || [lindex $values 9] != 0x00400000 ||
    ([lindex $values 12] & 0x3805) != 0} {error "DDR test failed: $values"}
set f [open $result w]
puts $f "{\"status\":\"passed\",\"board\":\"Z7-Nano 7010\",\"words_checked\":[lindex $values 3],\"test_bytes\":[lindex $values 9],\"ddr_status\":[lindex $values 11],\"cpu_sctlr\":[lindex $values 12],\"scope\":\"4 MiB patterns plus sparse address probes; not a full-capacity test\"}"
close $f
disconnect
puts PASS_Z7_NANO_DDR_TEST
