set root [file normalize [file join [file dirname [info script]] ..]]
set core $::env(SPEAKER_HLS_CORE)
if {$core ni {mfcc cnn}} {error "Expected mfcc or cnn"}
cd [file join $root output speaker_cnn]
open_project hls_$core
open_solution solution
cosim_design -rtl verilog -tool xsim -trace_level none
puts PASS_SPEAKER_HLS_RTL_COSIM
exit
