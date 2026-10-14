set root [file normalize [file join [file dirname [info script]] ..]]
set assets [file join $root output static_ui]
set out [file join $root output fpga_static]
file mkdir [file join $out reports]
set_param general.maxThreads 4
create_project static_ui [file join $out vivado] -part xc7z010clg400-1 -force
foreach filename {static_ui_top.v static_ui_video.v hdmi_clock.v async_rst_sync.v dvi_encoder.v dvi_transmitter_top.v serializer_10_to_1.v} {
    add_files [file join $root rtl $filename]
}
add_files [file join $assets static_ui_config.vh]
foreach filename {static_ui_tilemap.mem static_ui_tiles.mem static_ui_palette.mem} {
    add_files [file join $assets $filename]
}
set_property include_dirs [list $assets] [get_filesets sources_1]
add_files -fileset constrs_1 [file join $root constraints hdmi.xdc]
set_property top static_ui_top [get_filesets sources_1]
update_compile_order -fileset sources_1
synth_design -top static_ui_top -part xc7z010clg400-1
report_utilization -file [file join $out reports utilization_synth.rpt]
write_checkpoint -force [file join $out synth.dcp]
set bram36 [llength [get_cells -hier -filter {REF_NAME == RAMB36E1}]]
set bram18 [llength [get_cells -hier -filter {REF_NAME == RAMB18E1}]]
set bram_equivalent [expr {$bram36 + 0.5*$bram18}]
if {$bram_equivalent <= 0 || $bram_equivalent > 48} {error "Unexpected BRAM usage: $bram_equivalent"}
puts "PASS_STATIC_SYNTHESIS: bram36_equivalent=$bram_equivalent"
opt_design
place_design
phys_opt_design
route_design
write_checkpoint -force [file join $out routed.dcp]
report_timing_summary -report_unconstrained -file [file join $out reports timing.rpt]
report_drc -file [file join $out reports drc.rpt]
set drc_errors [get_drc_violations -filter {SEVERITY == "Error"}]
if {[llength $drc_errors]} {error "DRC errors: $drc_errors"}
report_io -file [file join $out reports io.rpt]
report_utilization -file [file join $out reports utilization.rpt]
set setup_path [get_timing_paths -delay_type max -max_paths 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup_path] == 0 || [llength $hold_path] == 0} {error "Missing timing paths"}
set setup_slack [get_property SLACK $setup_path]
set hold_slack [get_property SLACK $hold_path]
if {$setup_slack < 0 || $hold_slack < 0} {error "Timing failed: $setup_slack/$hold_slack"}
foreach {port pin} {sys_clk K17 sys_rst_n M19 hdmi_en H18 led_locked T12 led_heartbeat U12
    tmds_clk_p H16 tmds_clk_n H17
    {tmds_data_p[0]} D19 {tmds_data_n[0]} D20
    {tmds_data_p[1]} C20 {tmds_data_n[1]} B20
    {tmds_data_p[2]} B19 {tmds_data_n[2]} A20} {
    if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "Pin mismatch: $port"}
}
foreach {pin period} {u_clock/u_mmcm/CLKOUT0 20.0 u_clock/u_mmcm/CLKOUT1 4.0} {
    set clock [get_clocks -of_objects [get_pins $pin]]
    if {[llength $clock] != 1 || abs([get_property PERIOD $clock]-$period)>0.001} {error "Generated clock mismatch: $pin"}
}
write_bitstream -force [file join $out static_ui_top.bit]
set result [open [file join $out build_result.json] w]
puts $result "{\"part\":\"xc7z010clg400-1\",\"setup_slack_ns\":$setup_slack,\"hold_slack_ns\":$hold_slack,\"bram36_equivalent\":$bram_equivalent,\"pins_verified\":true,\"clocks_verified\":true,\"volatile_configuration\":true}"
close $result
puts "PASS_STATIC_BUILD: setup=$setup_slack hold=$hold_slack"
close_project
