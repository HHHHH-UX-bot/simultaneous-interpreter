set root [file normalize [file join [file dirname [info script]] ..]]
file mkdir [file join $root output speaker_cnn]
cd [file join $root output speaker_cnn]
open_project -reset hls_mfcc
set_top speaker_mfcc_core
add_files [file join $root software speaker_cnn speaker_mfcc.cpp] -cflags "-DSPEAKER_HLS -I[file join $root software speaker_cnn]"
add_files -tb [file join $root tests speaker_cnn mfcc_hls_tb.cpp] -cflags "-I[file join $root tests speaker_cnn]"
open_solution -reset solution
set_part xc7z010clg400-1
create_clock -period 20 -name default
csim_design -clean
csynth_design
puts PASS_SPEAKER_MFCC_HLS_SYNTHESIS
exit
