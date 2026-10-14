# Generate a PS USB0 preset for extracting only the USB MIO/reset settings.
# This does not program hardware or overwrite the validated display platform.
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root output z7_nano usb config]
source [file join $root output z7_nano config board.tcl]
source [file join $root output z7_nano config ps7_config.tcl]
create_project nano_usb [file join $out vivado] -part $board_part -force
create_bd_design nano_usb
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
configure_z7_nano_ps $ps
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {0} CONFIG.PCW_USE_S_AXI_HP0 {0} \
    CONFIG.PCW_EN_CLK0_PORT {0} CONFIG.PCW_EN_RST0_PORT {0} \
    CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
    CONFIG.PCW_USB0_RESET_ENABLE {1} CONFIG.PCW_USB0_RESET_IO {MIO 46}] $ps
make_bd_intf_pins_external [get_bd_intf_pins ps7/DDR]
make_bd_intf_pins_external [get_bd_intf_pins ps7/FIXED_IO]
validate_bd_design
save_bd_design
generate_target all [get_files nano_usb.bd]
file copy -force [file join $out vivado nano_usb.gen sources_1 bd nano_usb ip nano_usb_ps7_0 ps7_init.tcl] [file join $out ps7_init.tcl]
set f [open [file join $out properties.txt] w]
foreach property {PCW_USB0_PERIPHERAL_ENABLE PCW_USB0_USB0_IO PCW_USB0_RESET_ENABLE PCW_USB0_RESET_IO
    PCW_MIO_28_IOTYPE PCW_MIO_46_IOTYPE PCW_UIPARAM_DDR_BUS_WIDTH PCW_APU_PERIPHERAL_FREQMHZ} {
    puts $f "$property [get_property CONFIG.$property $ps]"
}
close $f
puts PASS_Z7_NANO_USB_CONFIG
close_project
