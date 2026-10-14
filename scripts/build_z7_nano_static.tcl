set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output z7_nano static]
set config [file join $root output z7_nano config]
set assets [file join $root output static_ui]
source [file join $config board.tcl]
file mkdir [file join $out reports]
set_param general.maxThreads 4
create_project z7_nano_static [file join $out vivado] -part $board_part -force
foreach filename {z7_nano_static_top.v static_ui_top.v static_ui_video.v hdmi_clock.v async_rst_sync.v dvi_encoder.v dvi_transmitter_top.v serializer_10_to_1.v} {
    add_files [file join $root rtl $filename]
}
foreach filename {static_ui_config.vh static_ui_tilemap.mem static_ui_tiles.mem static_ui_palette.mem} {
    add_files [file join $assets $filename]
}
set_property include_dirs [list $assets] [get_filesets sources_1]
add_files -fileset constrs_1 [file join $config hdmi.xdc]
set_property top z7_nano_static_top [get_filesets sources_1]
update_compile_order -fileset sources_1
synth_design -top z7_nano_static_top -part $board_part
opt_design
place_design
phys_opt_design
route_design
set_property BITSTREAM.CONFIG.USR_ACCESS 0x5a374e31 [current_design]
report_timing_summary -report_unconstrained -file [file join $out reports timing.rpt]
report_drc -file [file join $out reports drc.rpt]
report_io -file [file join $out reports io.rpt]
report_utilization -file [file join $out reports utilization.rpt]
if {[llength [get_drc_violations -filter {SEVERITY == "Error"}]]} {error "DRC failed"}
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup eq "" || $hold eq "" || $setup < 0 || $hold < 0} {error "Timing failed: $setup/$hold"}
foreach {port pin} $board_pins {
    if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "Pin mismatch: $port"}
}
foreach {pin period} {u_ui/u_clock/u_mmcm/CLKOUT0 20.0 u_ui/u_clock/u_mmcm/CLKOUT1 4.0} {
    set clock [get_clocks -of_objects [get_pins $pin]]
    if {[llength $clock] != 1 || abs([get_property PERIOD $clock]-$period)>0.001} {error "Clock mismatch: $pin"}
}
write_checkpoint -force [file join $out routed.dcp]
write_bitstream -force [file join $out z7_nano_static_top.bit]
set f [open [file join $out implementation.json] w]
puts $f "{\"part\":\"$board_part\",\"setup_slack_ns\":$setup,\"hold_slack_ns\":$hold,\"pins_verified\":true,\"clocks_verified\":true,\"usr_access\":[expr {0x5a374e31}]}"
close $f
puts PASS_Z7_NANO_STATIC_BUILD
close_project
