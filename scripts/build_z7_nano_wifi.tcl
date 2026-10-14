# Derive only UART wiring and the third GP0 target from the accepted new-board display.
set root [file normalize [file join [file dirname [info script]] ..]]
proc replace_once {text old new} {
    if {[string first $old $text]<0 || [string first $old $text [expr {[string first $old $text]+[string length $old]}]]>=0} {
        error "Display build anchor missing or ambiguous: $old"
    }
    return [string map [list $old $new] $text]
}
set f [open [file join $root scripts build_z7_nano_display.tcl] r]
set body [read $f]
close $f
set body [replace_once $body {set out [file join $root output z7_nano display]} {set out [file join $root output z7_nano wifi]}]
set body [replace_once $body {source [file join $root output z7_nano config board.tcl]} {source [file join $out board.tcl]}]
set body [replace_once $body {add_files [file join $root rtl framebuffer_video.v]} {
add_files [file join $root rtl framebuffer_video.v]
foreach filename {wifi_uart_phy.v wifi_byte_fifo.v wifi_uart_axi.v ps_wifi_uart.v} {
    add_files [file join $root rtl $filename]
}}]
set body [replace_once $body {set_property CONFIG.NUM_MI 2 $regs} {set_property CONFIG.NUM_MI 3 $regs
set uart [create_bd_cell -type module -reference ps_wifi_uart uart]}]
set body [replace_once $body {connect_bd_net $rst [get_bd_pins reset/ext_reset_in]} {
connect_bd_net $clk [get_bd_pins uart/aclk]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins uart/aresetn]
connect_bd_net $rst [get_bd_pins reset/ext_reset_in]}]
set body [replace_once $body {connect_bd_intf_net [get_bd_intf_pins regs/M01_AXI] [get_bd_intf_pins video/S_AXI]} {
connect_bd_intf_net [get_bd_intf_pins regs/M01_AXI] [get_bd_intf_pins video/S_AXI]
connect_bd_intf_net [get_bd_intf_pins regs/M02_AXI] [get_bd_intf_pins uart/S_AXI]
foreach pin {esp_rx esp_tx} {
    make_bd_pins_external [get_bd_pins uart/$pin]
    set_property name $pin [get_bd_ports ${pin}_0]
}
set uart_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins uart/S_AXI]]
if {[llength $uart_segment]!=1} {error "UART segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c20000 [get_bd_addr_spaces ps7/Data] $uart_segment SEG_UART}]
set body [replace_once $body {add_files -fileset constrs_1 [file join $root output z7_nano config hdmi.xdc]} {
add_files -fileset constrs_1 [file join $root output z7_nano config hdmi.xdc]
add_files -fileset constrs_1 [file join $out uart.xdc]}]
set body [replace_once $body {write_bitstream -force} {
foreach port {esp_tx esp_rx} {
    if {[get_property IOSTANDARD [get_ports $port]] ne "LVCMOS33"} {error "UART voltage standard mismatch"}
}
write_bitstream -force}]
set body [string map {z7_nano_framebuffer_top z7_nano_wifi_top 0x5a374e32 0x5a374e33} $body]
eval $body
