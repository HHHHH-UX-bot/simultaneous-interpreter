# User wiring, schematic pages 2/5/9: Bank 34 is powered at 3.3 V.
# FPGA U15 TX -> ESP RXD; ESP TXD -> FPGA W15 RX.
set_property PACKAGE_PIN U15 [get_ports esp_tx]
set_property PACKAGE_PIN W15 [get_ports esp_rx]
set_property IOSTANDARD LVCMOS33 [get_ports {esp_tx esp_rx}]
set_property DRIVE 8 [get_ports esp_tx]
set_property SLEW SLOW [get_ports esp_tx]
set_property PULLUP true [get_ports esp_rx]
# The RX pin crosses into the dedicated two-flop synchronizer.
set_false_path -from [get_ports esp_rx]
# UART is asynchronous to the external receiver, with no external clock.
set_false_path -to [get_ports esp_tx]
