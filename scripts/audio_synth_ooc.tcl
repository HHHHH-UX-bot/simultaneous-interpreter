# Out-of-context synthesis check for the audio modules.
#
# The full design needs the block design (PS/VDMA/framebuffer) that
# build_z7_nano_audio.tcl generates with the project's Vivado version, so this
# script proves the audio RTL itself elaborates and synthesises for the target
# part without the block design.
#
# Usage: vivado -mode batch -source scripts/audio_synth_ooc.tcl
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output z7_nano audio_ooc]
set part xc7z010clg400-1
file mkdir [file join $out reports]

set modules {i2c_master wm8731_init audio_latency i2s_loopback audio_regs audio_overlay}
set sources {}
foreach f {i2c_master.v wm8731_init.v i2s_loopback.v audio_latency.v audio_regs.v audio_overlay.v} {
    lappend sources [file join $root rtl $f]
}
read_verilog $sources

foreach m $modules {
    synth_design -top $m -part $part -mode out_of_context
    report_utilization -file [file join $out reports ${m}_utilization.rpt]
    set luts [get_property SLICE_LUTS [get_cells -hier -filter {PRIMITIVE_GROUP == LUT}] -quiet]
    puts "PASS_OOC_$m"
}
puts PASS_AUDIO_SYNTH_OOC
