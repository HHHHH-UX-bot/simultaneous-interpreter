# Runtime UI controls only. The caller must first verify the loaded application
# and initialized model via the loader and UDP status; no credential reads.
if {$argc!=1} {error "Expected matching local_voice_diag address"}
set diag [lindex $argv 0]
if {![string is integer -strict $diag] || $diag<0x01a80000 || $diag>=0x01b00000} {error "Invalid public diagnostic window"}
connect -url tcp:127.0.0.1:3121
after 300
for {set i 0} {$i<20} {incr i} {
 set t [targets -target-properties -filter {name == "xc7z010"}]
 if {[llength $t]==1 && [dict exists [lindex $t 0] jtag_cable_serial]} {break}
 after 100
}
if {[llength $t]!=1 || ![dict exists [lindex $t 0] jtag_cable_serial] || [dict get [lindex $t 0] jtag_cable_serial] ne "210241277120"} {error "Wrong or unavailable cable"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
if {[state] ne "Running"} {error "Running CPU0 required"}
proc r {a} {return [lindex [mrd -force -value $a] 0]}
if {[r 0x1e140]!=0x3154454e || [r $diag]!=0x31564c51} {error "Matching application/public voice ABI required"}
# Select local; START or RESUME is accepted only in its matching session state.
foreach c {2 30 1 3} {
 set q [expr {[r 0x1e0c4]+1}]
 mwr -force 0x1e0c8 [r 0x1e0a8]
 mwr -force 0x1e0cc $c
 mwr -force 0x1e0c4 $q
 for {set i 0} {$i<100 && [r 0x1e0d0]!=$q} {incr i} {after 20}
 if {[r 0x1e0d0]!=$q} {error "UI control acknowledgement timeout"}
 after 150
}
after 300
if {[r [expr {$diag+8}]]!=1} {error "ASR role not armed; voice was not started"}
puts LOCAL_VOICE_ASR_ARMED
disconnect
exit
