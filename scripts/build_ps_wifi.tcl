# Specialize the verified display builder without changing the accepted baseline.
# Each replacement is asserted so an upstream change fails before implementation.
set root [file normalize [file join [file dirname [info script]] ..]]
proc replace_once {text old new} {
    set start [string first $old $text]
    if {$start<0 || [string first $old $text [expr {$start+[string length $old]}]]>=0} {
        error "Display builder specialization is ambiguous: $old"
    }
    return [string map [list $old $new] $text]
}
set f [open [file join $root scripts build_framebuffer.tcl] r]
set body [read $f]
close $f
set body [replace_once $body {set out [file join $root output framebuffer]} {set out [file join $root output ps_wifi]}]
set body [replace_once $body {add_files [file join $root rtl framebuffer_video.v]} {
add_files [file join $root rtl framebuffer_video.v]
foreach filename {wifi_uart_phy.v wifi_byte_fifo.v wifi_uart_axi.v ps_wifi_uart.v} {
    add_files [file join $root rtl $filename]
}}]
set body [replace_once $body {set_property CONFIG.NUM_MI 2 $regs} {set_property CONFIG.NUM_MI 3 $regs
set uart [create_bd_cell -type module -reference ps_wifi_uart uart]}]
set body [replace_once $body {connect_bd_net $rst [get_bd_pins reset/ext_reset_in]} {connect_bd_net $clk [get_bd_pins uart/aclk]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins uart/aresetn]
connect_bd_net $rst [get_bd_pins reset/ext_reset_in]}]
set body [replace_once $body {connect_bd_intf_net [get_bd_intf_pins regs/M01_AXI] [get_bd_intf_pins video/S_AXI]} {connect_bd_intf_net [get_bd_intf_pins regs/M01_AXI] [get_bd_intf_pins video/S_AXI]
connect_bd_intf_net [get_bd_intf_pins regs/M02_AXI] [get_bd_intf_pins uart/S_AXI]
foreach pin {esp_rx esp_tx} {
    make_bd_pins_external [get_bd_pins uart/$pin]
    set_property name $pin [get_bd_ports ${pin}_0]
}
set uart_segment [get_bd_addr_segs -of_objects [get_bd_intf_pins uart/S_AXI]]
if {[llength $uart_segment]!=1} {error "UART segment missing"}
create_bd_addr_seg -range 0x10000 -offset 0x43c20000 [get_bd_addr_spaces ps7/Data] $uart_segment SEG_UART}]
set body [replace_once $body {set_property BITSTREAM.CONFIG.USR_ACCESS 0x46564231} {set_property BITSTREAM.CONFIG.USR_ACCESS 0x46574e31}]
# The implementation report contains the configuration identity a second time.
set body [string map {framebuffer_top ps_wifi_top 0x46564231 0x46574e31} $body]
eval $body
if {[lindex $argv 0] eq "build"} {
    file copy -force [file join $root output ps_wifi ps_wifi_top.bit] [file join $root output ps_wifi framebuffer_top.bit]
}
