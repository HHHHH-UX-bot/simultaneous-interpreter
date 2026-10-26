# PS-only hardware description for the SD-card FSBL.
# Same PS configuration as build_z7_nano_audio.tcl (DDR, clocks, GP0, 64-bit
# HP0, USB0) plus SD0 on MIO 40..45 for loading BOOT.BIN partitions and UART0
# on MIO 14..15 (CH340 Type-C port) for FSBL boot messages. The PL bitstream is
# not rebuilt: it carries no PS MIO/peripheral configuration.
# Usage: vivado -mode batch -source build_sd_boot_platform.tcl -tclargs <out_dir>
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file normalize [lindex $argv 0]]
set cfgdir [file join $root output z7_nano config]
source [file join $cfgdir board.tcl]
file mkdir $out
create_project sd_boot_ps [file join $out vivado] -part $board_part -force
create_bd_design sd_boot_ps
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
source [file join $cfgdir ps7_config.tcl]
configure_z7_nano_ps $ps
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_S_AXI_HP0_DATA_WIDTH {64} CONFIG.PCW_EN_CLK0_PORT {0} \
    CONFIG.PCW_EN_RST0_PORT {0} \
    CONFIG.PCW_EN_USB0 {1} CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
    CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} \
    CONFIG.PCW_SD0_GRP_CD_ENABLE {0} CONFIG.PCW_SD0_GRP_WP_ENABLE {0} \
    CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} CONFIG.PCW_UART0_UART0_IO {MIO 14 .. 15} \
    CONFIG.PCW_UART0_BAUD_RATE {115200}] $ps
if {[get_property CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH $ps] ne {16 Bit} ||
    [get_property CONFIG.PCW_DDR_RAM_HIGHADDR $ps] ne {0x1FFFFFFF}} {error "Wrong DDR geometry"}
make_bd_intf_pins_external [get_bd_intf_pins ps7/DDR]
make_bd_intf_pins_external [get_bd_intf_pins ps7/FIXED_IO]
# The AXI ports exist only so ps7_init programs the same GP0/HP0 settings;
# their clocks come from the PL in the real design.
set aclk [create_bd_port -dir I -type clk -freq_hz 50000000 axi_aclk]
connect_bd_net $aclk [get_bd_pins ps7/M_AXI_GP0_ACLK] [get_bd_pins ps7/S_AXI_HP0_ACLK]
make_bd_intf_pins_external [get_bd_intf_pins ps7/M_AXI_GP0]
make_bd_intf_pins_external [get_bd_intf_pins ps7/S_AXI_HP0]
validate_bd_design
save_bd_design
set bd [get_files sd_boot_ps.bd]
generate_target all $bd
make_wrapper -files $bd -top -import
write_hw_platform -fixed -force [file join $out sd_boot_ps.xsa]
set init [glob -nocomplain [file join $out vivado sd_boot_ps.gen sources_1 bd sd_boot_ps ip * ps7_init.tcl]]
if {[llength $init] != 1} {error "ps7_init.tcl not generated"}
file copy -force [lindex $init 0] [file join $out ps7_init_sd_boot.tcl]
puts PASS_SD_BOOT_PS_PLATFORM
close_project
