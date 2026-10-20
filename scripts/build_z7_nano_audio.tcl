# Z7-Nano audio loopback build: the accepted display block design plus the audio
# register file on a third AXI port, the WM8731 I2C initialiser, the I2S slave
# loopback and the latency readout overlay.
#
# Differences from build_z7_nano_display.tcl:
#   * block design name nano_audio, regs interconnect with 3 masters-to-slaves
#     (VDMA, framebuffer_video, audio_regs) and M02_AXI exported as S_AXI_AUDIO,
#   * the audio RTL and the audio pin constraints are added,
#   * pins and clocks are re-checked for the audio side as well.
#
# Usage: vivado -mode batch -source scripts/build_z7_nano_audio.tcl -tclargs build
set root [file normalize [file join [file dirname [info script]] ..]]
set action [lindex $argv 0]
# Optional second argument: output directory. Used to build in a scratch tree
# while the main project is open in the GUI.
set out [expr {[llength $argv] > 1 ? [lindex $argv 1] : [file join $root output z7_nano audio]}]
set cfgdir [file join $root output z7_nano config]
source [file join $cfgdir board.tcl]
if {$action ni {design build}} {error "Expected design or build"}
file mkdir [file join $out reports]
set_param general.maxThreads 4
create_project nano_audio [file join $out vivado] -part $board_part -force

add_files [file join $root rtl framebuffer_video.v]
add_files [file join $root rtl audio_regs.v]
add_files [file join $root rtl audio_playback.v]
# The ESP8266 UART is a module reference inside the block design, so its
# sources must be in the project before create_bd_cell resolves them.
foreach filename {ps_wifi_uart.v wifi_uart_axi.v wifi_uart_phy.v wifi_byte_fifo.v} {
    add_files [file join $root rtl $filename]
}

# ---------------------------------------------------------------------------
# Block design
# ---------------------------------------------------------------------------
create_bd_design nano_audio
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
source [file join $cfgdir ps7_config.tcl]
configure_z7_nano_ps $ps
# The board preset only carries DDR and clock values; the display interface also
# needs the GP0 master and the 64-bit HP0 slave, and the PL supplies the clocks.
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_S_AXI_HP0_DATA_WIDTH {64} CONFIG.PCW_EN_CLK0_PORT {0} \
    CONFIG.PCW_EN_RST0_PORT {0} \
    CONFIG.PCW_EN_USB0 {1} CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39}] $ps
if {[get_property CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH $ps] ne {16 Bit} ||
    [get_property CONFIG.PCW_DDR_RAM_HIGHADDR $ps] ne {0x1FFFFFFF}} {error "Wrong DDR geometry"}
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
set_property CONFIG.NUM_MI 4 $regs
set memory [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 memory]
set_property CONFIG.NUM_MI 1 $memory
set reset [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset]
# C_EXT_RESET_HIGH is read-only on older Vivado, where the default is already
# active-high; only the auxiliary polarity needs to be forced.
catch {set_property CONFIG.C_EXT_RESET_HIGH {1} $reset}
catch {set_property CONFIG.C_AUX_RESET_HIGH {0} $reset}
set one [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 one]
set_property CONFIG.CONST_VAL 1 $one
set zero [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 zero]
set_property CONFIG.CONST_VAL 0 $zero
set clk [create_bd_port -dir I -type clk pixel_clk]
# -freq_hz is not accepted by older create_bd_port; set the property instead.
catch {set_property CONFIG.FREQ_HZ 50000000 $clk}
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

# Audio register file: instantiated inside the block design exactly like
# framebuffer_video, so the PS address space has a real slave segment. The audio
# chain stays in the top level; only its status and control scalars cross the
# block-design boundary.
set aregs [create_bd_cell -type module -reference audio_regs aregs]
connect_bd_intf_net [get_bd_intf_pins regs/M02_AXI] [get_bd_intf_pins aregs/S_AXI]
connect_bd_net $clk [get_bd_pins aregs/clk]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins aregs/reset_n]
set bclk_hz [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 bclk_hz]
set_property -dict [list CONFIG.CONST_WIDTH {32} CONFIG.CONST_VAL {3072000}] $bclk_hz
connect_bd_net [get_bd_pins bclk_hz/dout] [get_bd_pins aregs/bclk_hz]

set audio_status_in {live codec_found codec_addr_sel init_busy init_done init_error \
                     regs_written occ_frames latency_cycles adc_frames dac_frames \
                     overruns underruns bclk_cycles latency_us min_us max_us max_valid \
                     pin_count0 pin_count1 pin_count2 pin_count3 \
                     rb_r4 rb_r7 rb_r8 rb_r9 rb_valid sda_idle scl_idle \
                     sample_data sample_toggle sample_count playback_bclk playback_lrclk}
set audio_ctrl_out {init_trigger clear_extremes freeze_display translation_ms translation_valid \
                    cfg_r0 cfg_r1 cfg_r2 \
                    cfg_r3 cfg_r4 cfg_r5 cfg_r7 cfg_r8 playback_dac playback_enable}
foreach pin [concat $audio_status_in $audio_ctrl_out] {
    make_bd_pins_external [get_bd_pins aregs/$pin]
    set port [get_bd_ports ${pin}_0]
    if {[llength $port] != 1} {error "Missing audio port $pin"}
    set_property name $pin $port
}

create_bd_addr_seg -range 0x10000 -offset 0x43c00000 [get_bd_addr_spaces ps7/Data] [get_bd_addr_segs vdma/S_AXI_LITE/Reg] SEG_VDMA
set video_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins video/S_AXI]]
if {[llength $video_segment] != 1} {error "Video register segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c10000 [get_bd_addr_spaces ps7/Data] $video_segment SEG_VIDEO
set audio_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins aregs/S_AXI]]
if {[llength $audio_segment] != 1} {error "Audio register segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c20000 [get_bd_addr_spaces ps7/Data] $audio_segment SEG_AUDIO

# ESP8266 UART mailbox on the fourth AXI master, so the PS drives the AT link
# through the same register-file style as the audio block. Only esp_tx/esp_rx
# cross the block-design boundary.
set uart [create_bd_cell -type module -reference ps_wifi_uart uart]
connect_bd_intf_net [get_bd_intf_pins regs/M03_AXI] [get_bd_intf_pins uart/S_AXI]
connect_bd_net $clk [get_bd_pins uart/aclk]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins uart/aresetn]
foreach pin {esp_tx esp_rx} {
    make_bd_pins_external [get_bd_pins uart/$pin]
    set port [get_bd_ports ${pin}_0]
    if {[llength $port] != 1} {error "Missing UART port $pin"}
    set_property name $pin $port
}
set uart_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins uart/S_AXI]]
if {[llength $uart_segment] != 1} {error "UART register segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c30000 [get_bd_addr_spaces ps7/Data] $uart_segment SEG_UART
assign_bd_address [get_bd_addr_segs ps7/S_AXI_HP0/HP0_DDR_LOWOCM]
validate_bd_design
if {[get_property CONFIG.c_prmry_is_aclk_async $dma] != 0} {error "VDMA did not infer the common clock"}
if {[get_property CONFIG.C_EXT_RESET_HIGH $reset] != 1} {error "Reset polarity mismatch"}
if {[get_property CONFIG.C_AUX_RESET_HIGH $reset] != 0} {error "Auxiliary reset polarity mismatch"}
set auxiliary_net [get_bd_nets -of_objects [get_bd_pins reset/aux_reset_in]]
if {[get_bd_nets -of_objects [get_bd_pins one/dout]] ne $auxiliary_net} {error "Unused active-low auxiliary reset must be tied high"}
save_bd_design
set bd [get_files nano_audio.bd]
set_property generate_synth_checkpoint false $bd
generate_target all $bd
# The wrapper ports are named after the exported BD pins: check the generated
# wrapper source instead of get_ports, which needs an open (elaborated) design.
set wrapper [make_wrapper -files $bd -top]
add_files $wrapper
set wrapper_path [file normalize [lindex $wrapper 0]]
set fh [open $wrapper_path r]
set wrapper_text [read $fh]
close $fh
foreach pin [concat $audio_status_in $audio_ctrl_out] {
    if {![regexp "\\m${pin}\\M" $wrapper_text]} {error "Wrapper audio port $pin missing"}
}
write_bd_tcl -force [file join $out nano_audio_bd.tcl]

# ---------------------------------------------------------------------------
# Sources and constraints (added before the design exit so the GUI project is
# complete as well), then synthesis and implementation.
# ---------------------------------------------------------------------------
foreach filename {z7_nano_audio_top.v i2c_master.v wm8731_init.v i2s_loopback.v \
                  audio_latency.v audio_regs.v audio_overlay.v audio_pin_activity.v \
                  audio_sample_tap.v \
                  hdmi_clock.v async_rst_sync.v dvi_encoder.v dvi_transmitter_top.v \
                  serializer_10_to_1.v} {
    add_files [file join $root rtl $filename]
}
add_files -fileset constrs_1 [file join $cfgdir hdmi.xdc]
add_files -fileset constrs_1 [file join $cfgdir audio.xdc]
set_property top z7_nano_audio_top [get_filesets sources_1]
update_compile_order -fileset sources_1

if {$action eq "design"} {puts PASS_Z7_NANO_AUDIO_DESIGN; close_project; exit}
synth_design -top z7_nano_audio_top -part $board_part
opt_design
# The tap waits one extra 20 ns destination cycle for this held data bundle.
# Bound data/control route skew to 15 ns; async clock groups do not check it.
set tap_ends [get_cells -hier -regexp {.*u_sample_tap/(data1_reg\[[0-9]+\]|t1_reg)}]
if {[llength $tap_ends] != 17} {error "Microphone CDC endpoints missing"}
set tap_starts [all_fanin -flat -startpoints_only -only_cells -to [get_pins -of_objects $tap_ends -filter {REF_PIN_NAME == D}]]
if {[llength $tap_starts] != 17} {error "Microphone CDC sources missing"}
set_bus_skew -from $tap_starts -to $tap_ends 15.0
place_design
phys_opt_design
route_design
set_property BITSTREAM.CONFIG.USR_ACCESS 0x5a374e33 [current_design]
report_timing_summary -report_unconstrained -file [file join $out reports timing.rpt]
set tap_skew_report [report_bus_skew -return_string]
set tap_skew_file [open [file join $out reports microphone_cdc_skew.rpt] w]
puts $tap_skew_file $tap_skew_report
close $tap_skew_file
if {![regexp {Slack \(MET\)} $tap_skew_report] || [regexp {Slack \(VIOLATED\)} $tap_skew_report]} {
    error "Microphone CDC route skew not verified"
}
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
foreach {port pin} {i2s_bclk H16 i2s_dadlrc H17 i2s_adclrc D19 i2s_adcdat B20 i2s_dacdat C20 i2c_scl B19 i2c_sda A20 esp_tx N17 esp_rx P18} {
    if {[get_property PACKAGE_PIN [get_ports $port]] ne $pin} {error "Audio pin mismatch: $port"}
}
foreach {pin period} {u_clock/u_mmcm/CLKOUT0 20.0 u_clock/u_mmcm/CLKOUT1 4.0} {
    set clock [get_clocks -of_objects [get_pins $pin]]
    if {[llength $clock] != 1 || abs([get_property PERIOD $clock]-$period)>0.001} {error "Clock mismatch: $pin"}
}
# In slave mode the I2S clock is an input clock and must match the module's
# BCLK period; in master mode the PL drives it and the I/O paths are excluded
# from timing analysis instead.
set bclk [get_clocks -of_objects [get_ports i2s_bclk]]
if {[llength $bclk] == 1} {
    if {abs([get_property PERIOD $bclk]-325.520) > 0.01} {error "i2s_bclk period mismatch"}
} else {
    puts "INFO: i2s_bclk is driven by the PL (master mode); I/O paths excluded"
}
write_checkpoint -force [file join $out routed.dcp]
write_bitstream -force [file join $out z7_nano_audio_top.bit]
set f [open [file join $out implementation.json] w]
puts $f "{\"part\":\"$board_part\",\"setup_slack_ns\":$setup,\"hold_slack_ns\":$hold,\"pins_verified\":true,\"audio_pins_verified\":true,\"clocks_verified\":true,\"audio_base_addr\":\"0x43c20000\",\"usr_access\":[expr {0x5a374e33}]}"
close $f
puts PASS_Z7_NANO_AUDIO_BUILD
close_project
