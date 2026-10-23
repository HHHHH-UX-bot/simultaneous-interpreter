set root [file normalize [file join [file dirname [info script]] ..]]
file mkdir [file join $root output speaker_cnn]
cd [file join $root output speaker_cnn]
open_project -reset hls_cnn
set_top speaker_cnn_core
add_files [file join $root software speaker_cnn speaker_cnn.cpp] -cflags "-DSPEAKER_HLS"
add_files -tb [file join $root tests speaker_cnn cnn_hls_tb.cpp] -cflags "-I[file join $root tests speaker_cnn]"
open_solution -reset solution
set_part xc7z010clg400-1
create_clock -period 20 -name default
csim_design -clean
csynth_design
puts PASS_SPEAKER_CNN_HLS_SYNTHESIS
exit
