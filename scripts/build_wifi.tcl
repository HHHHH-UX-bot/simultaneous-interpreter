set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output wifi]
set assets [file join $root output static_ui]
file mkdir [file join $out reports]
set_param general.maxThreads 4
create_project wifi_ui [file join $out vivado] -part xc7z010clg400-1 -force
foreach filename {static_ui_top.v static_ui_video.v hdmi_clock.v async_rst_sync.v dvi_encoder.v dvi_transmitter_top.v serializer_10_to_1.v wifi_byte_fifo.v wifi_uart_phy.v wifi_uart_axi.v wifi_ui_top.v} {
    add_files [file join $root rtl $filename]
}
add_files [file join $assets static_ui_config.vh]
foreach filename {static_ui_tilemap.mem static_ui_tiles.mem static_ui_palette.mem} {add_files [file join $assets $filename]}
set_property include_dirs [list $assets] [get_filesets sources_1]
foreach filename {hdmi.xdc wifi_uart.xdc} {add_files -fileset constrs_1 [file join $root constraints $filename]}
create_ip -name jtag_axi -vendor xilinx.com -library ip -version 1.2 -module_name wifi_jtag_axi
set_property CONFIG.PROTOCOL 2 [get_ips wifi_jtag_axi]
set_property generate_synth_checkpoint false [get_files wifi_jtag_axi.xci]
generate_target all [get_ips wifi_jtag_axi]
set_property top wifi_ui_top [get_filesets sources_1]
update_compile_order -fileset sources_1
synth_design -top wifi_ui_top -part xc7z010clg400-1
report_utilization -file [file join $out reports utilization_synth.rpt]
write_checkpoint -force [file join $out synth.dcp]
set bram [expr {[llength [get_cells -hier -filter {REF_NAME == RAMB36E1}]] + 0.5*[llength [get_cells -hier -filter {REF_NAME == RAMB18E1}]]}]
if {$bram > 48 || $bram < 18} {error "Unexpected BRAM use $bram"}
opt_design
place_design
phys_opt_design
route_design
write_checkpoint -force [file join $out routed.dcp]
report_timing_summary -report_unconstrained -file [file join $out reports timing.rpt]
report_drc -file [file join $out reports drc.rpt]
if {[llength [get_drc_violations -filter {SEVERITY == "Error"}]]} {error "DRC failed"}
report_io -file [file join $out reports io.rpt]
report_utilization -file [file join $out reports utilization.rpt]
report_cdc -file [file join $out reports cdc.rpt]
set setup_path [get_timing_paths -delay_type max -max_paths 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup_path] != 1 || [llength $hold_path] != 1} {error "Missing timing evidence"}
set setup [get_property SLACK $setup_path]
set hold [get_property SLACK $hold_path]
if {$setup < 0 || $hold < 0} {error "Timing failed $setup/$hold"}
foreach {port pin} {sys_clk K17 sys_rst_n M19 hdmi_en H18 led_locked T12 led_heartbeat U12
    tmds_clk_p H16 tmds_clk_n H17
    {tmds_data_p[0]} D19 {tmds_data_n[0]} D20
    {tmds_data_p[1]} C20 {tmds_data_n[1]} B20
    {tmds_data_p[2]} B19 {tmds_data_n[2]} A20
    esp_tx U15 esp_rx W15} {
    if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "Pin mismatch $port"}
}
foreach port {esp_tx esp_rx} {
    if {[get_property IOSTANDARD [get_ports $port]] ne "LVCMOS33"} {error "UART voltage standard mismatch"}
}
foreach {pin period} {u_clock/u_mmcm/CLKOUT0 20.0 u_clock/u_mmcm/CLKOUT1 4.0} {
    set clock [get_clocks -of_objects [get_pins $pin]]
    if {[llength $clock] != 1 || abs([get_property PERIOD $clock]-$period)>0.001} {error "Clock mismatch $pin"}
}
write_bitstream -force [file join $out wifi_ui_top.bit]
set f [open [file join $out implementation.json] w]
puts $f "{\"part\":\"xc7z010clg400-1\",\"setup_slack_ns\":$setup,\"hold_slack_ns\":$hold,\"bram36_equivalent\":$bram,\"pins_verified\":true,\"clocks_verified\":true}"
close $f
puts "PASS_WIFI_BUILD"
close_project
