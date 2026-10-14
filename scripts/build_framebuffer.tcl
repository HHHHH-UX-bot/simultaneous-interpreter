set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output framebuffer]
set action [lindex $argv 0]
if {$action ni {design build}} {error "Expected design or build"}
file mkdir [file join $out reports]
set_param general.maxThreads 4
create_project framebuffer [file join $out vivado] -part xc7z010clg400-1 -force
add_files [file join $root rtl framebuffer_video.v]
create_bd_design ps_display
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
source [file join $out ps7_config.tcl]
configure_rev_b_ps $ps
make_bd_intf_pins_external [get_bd_intf_pins ps7/DDR]
make_bd_intf_pins_external [get_bd_intf_pins ps7/FIXED_IO]
set dma [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 vdma]
set_property -dict [list CONFIG.c_include_s2mm {0} CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_sg {0} CONFIG.c_num_fstores {2} CONFIG.c_mm2s_genlock_mode {0} \
    CONFIG.c_include_internal_genlock {0} CONFIG.c_use_fsync {1} CONFIG.c_use_mm2s_fsync {1} \
    CONFIG.c_m_axi_mm2s_data_width {64} CONFIG.c_m_axis_mm2s_tdata_width {32} \
    CONFIG.c_mm2s_linebuffer_depth {8192} CONFIG.c_mm2s_max_burst_length {16} \
    CONFIG.c_include_mm2s_dre {0}] $dma
set video [create_bd_cell -type module -reference framebuffer_video video]
set regs [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 regs]
set_property CONFIG.NUM_MI 2 $regs
set memory [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 memory]
set_property CONFIG.NUM_MI 1 $memory
set reset [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset]
set_property -dict [list CONFIG.C_EXT_RESET_HIGH {1} CONFIG.C_AUX_RESET_HIGH {0}] $reset
set one [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 one]
set_property CONFIG.CONST_VAL 1 $one
set zero [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 zero]
set_property CONFIG.CONST_VAL 0 $zero
set clk [create_bd_port -dir I -type clk -freq_hz 50000000 pixel_clk]
set rst [create_bd_port -dir I -type rst pixel_reset]
set_property CONFIG.POLARITY ACTIVE_HIGH $rst
connect_bd_net $clk [get_bd_pins reset/slowest_sync_clk] [get_bd_pins video/clk] \
    [get_bd_pins ps7/M_AXI_GP0_ACLK] [get_bd_pins ps7/S_AXI_HP0_ACLK] \
    [get_bd_pins vdma/s_axi_lite_aclk] [get_bd_pins vdma/m_axi_mm2s_aclk] [get_bd_pins vdma/m_axis_mm2s_aclk]
foreach cell {regs memory} {
    foreach pin [get_bd_pins -of_objects [get_bd_cells $cell] -filter {TYPE == clk}] {connect_bd_net $clk $pin}
    foreach pin [get_bd_pins -of_objects [get_bd_cells $cell] -filter {TYPE == rst}] {connect_bd_net [get_bd_pins reset/interconnect_aresetn] $pin}
}
connect_bd_net $rst [get_bd_pins reset/ext_reset_in]
# The auxiliary reset is active-low; tie it HIGH when unused.
connect_bd_net [get_bd_pins one/dout] [get_bd_pins reset/dcm_locked] [get_bd_pins reset/aux_reset_in]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins reset/mb_debug_sys_rst]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins video/reset_n] [get_bd_pins vdma/axi_resetn]
connect_bd_intf_net [get_bd_intf_pins ps7/M_AXI_GP0] [get_bd_intf_pins regs/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins regs/M00_AXI] [get_bd_intf_pins vdma/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins regs/M01_AXI] [get_bd_intf_pins video/S_AXI]
connect_bd_intf_net [get_bd_intf_pins vdma/M_AXI_MM2S] [get_bd_intf_pins memory/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins memory/M00_AXI] [get_bd_intf_pins ps7/S_AXI_HP0]
connect_bd_intf_net [get_bd_intf_pins vdma/M_AXIS_MM2S] [get_bd_intf_pins video/S_AXIS]
connect_bd_net [get_bd_pins video/dma_fsync] [get_bd_pins vdma/mm2s_fsync]
foreach pin {video_hs video_vs video_de video_rgb frame_count} {
    make_bd_pins_external [get_bd_pins video/$pin]
    set port [get_bd_ports ${pin}_0]
    if {[llength $port] != 1} {error "Missing external video port $pin"}
    set_property name $pin $port
}
create_bd_addr_seg -range 0x10000 -offset 0x43c00000 [get_bd_addr_spaces ps7/Data] [get_bd_addr_segs vdma/S_AXI_LITE/Reg] SEG_VDMA
set video_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins video/S_AXI]]
if {[llength $video_segment] != 1} {error "Video register segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c10000 [get_bd_addr_spaces ps7/Data] $video_segment SEG_VIDEO
assign_bd_address [get_bd_addr_segs ps7/S_AXI_HP0/HP0_DDR_LOWOCM]
validate_bd_design
if {[get_property CONFIG.c_prmry_is_aclk_async $dma] != 0} {error "VDMA did not infer the common clock"}
if {[get_property CONFIG.C_EXT_RESET_HIGH $reset] != 1} {error "Reset polarity mismatch"}
if {[get_property CONFIG.C_AUX_RESET_HIGH $reset] != 0} {error "Auxiliary reset polarity mismatch"}
set auxiliary_net [get_bd_nets -of_objects [get_bd_pins reset/aux_reset_in]]
if {[get_bd_nets -of_objects [get_bd_pins one/dout]] ne $auxiliary_net} {error "Unused active-low auxiliary reset must be tied high"}
save_bd_design
set bd [get_files ps_display.bd]
set_property generate_synth_checkpoint false $bd
generate_target all $bd
set wrapper [make_wrapper -files $bd -top]
add_files $wrapper
write_bd_tcl -force [file join $out ps_display_bd.tcl]
set f [open [file join $out reports configuration.rpt] w]
foreach cell {ps7 vdma} {
    foreach property [lsort [list_property [get_bd_cells $cell]]] {
        if {[string match CONFIG.* $property]} {puts $f "$cell $property [get_property $property [get_bd_cells $cell]]"}
    }
}
close $f
puts "FRAMEBUFFER_WRAPPER $wrapper"
if {$action eq "design"} {puts PASS_FRAMEBUFFER_DESIGN; close_project; exit}
foreach filename {framebuffer_top.v hdmi_clock.v async_rst_sync.v dvi_encoder.v dvi_transmitter_top.v serializer_10_to_1.v} {
    add_files [file join $root rtl $filename]
}
foreach filename {hdmi.xdc wifi_uart.xdc} {add_files -fileset constrs_1 [file join $root constraints $filename]}
set_property top framebuffer_top [get_filesets sources_1]
update_compile_order -fileset sources_1
synth_design -top framebuffer_top -part xc7z010clg400-1
write_checkpoint -force [file join $out synth.dcp]
opt_design
place_design
phys_opt_design
route_design
set_property BITSTREAM.CONFIG.USR_ACCESS 0x46564231 [current_design]
write_checkpoint -force [file join $out routed.dcp]
report_timing_summary -report_unconstrained -file [file join $out reports timing.rpt]
report_drc -file [file join $out reports drc.rpt]
report_utilization -file [file join $out reports utilization.rpt]
report_io -file [file join $out reports io.rpt]
if {[llength [get_drc_violations -filter {SEVERITY == "Error"}]]} {error "DRC failed"}
set setup_path [get_timing_paths -delay_type max -max_paths 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup_path]!=1 || [llength $hold_path]!=1} {error "Timing evidence missing"}
set setup [get_property SLACK $setup_path]
set hold [get_property SLACK $hold_path]
if {$setup<0 || $hold<0} {error "Timing failed: $setup/$hold"}
foreach {port pin} {sys_clk K17 sys_rst_n M19 hdmi_en H18 led_locked T12 led_heartbeat U12
    tmds_clk_p H16 tmds_clk_n H17
    {tmds_data_p[0]} D19 {tmds_data_n[0]} D20
    {tmds_data_p[1]} C20 {tmds_data_n[1]} B20
    {tmds_data_p[2]} B19 {tmds_data_n[2]} A20 esp_tx U15 esp_rx W15} {
    if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "Pin mismatch $port"}
}
foreach {pin period} {u_clock/u_mmcm/CLKOUT0 20.0 u_clock/u_mmcm/CLKOUT1 4.0} {
    set clock [get_clocks -of_objects [get_pins $pin]]
    if {[llength $clock]!=1 || abs([get_property PERIOD $clock]-$period)>0.001} {error "Clock mismatch $pin"}
}
write_bitstream -force [file join $out framebuffer_top.bit]
write_hw_platform -fixed -force -file [file join $out ps_display.xsa]
file copy -force [file join $out vivado framebuffer.gen sources_1 bd ps_display ip ps_display_ps7_0 ps7_init.tcl] [file join $out ps7_init.tcl]
set f [open [file join $out implementation.json] w]
puts $f "{\"part\":\"xc7z010clg400-1\",\"setup_slack_ns\":$setup,\"hold_slack_ns\":$hold,\"pins_verified\":true,\"clocks_verified\":true,\"usr_access\":[expr {0x46564231}]}"
close $f
puts PASS_FRAMEBUFFER_BUILD
close_project
