# ZYNQ_MINI_REVB, xc7z010clg400-1: schematic pages 2, 5, 11.
create_clock -name sys_clk -period 20.000 [get_ports sys_clk]
set_property PACKAGE_PIN K17 [get_ports sys_clk]
set_property PACKAGE_PIN M19 [get_ports sys_rst_n]
set_property PULLUP true [get_ports sys_rst_n]
set_property PACKAGE_PIN H18 [get_ports hdmi_en]
set_property PACKAGE_PIN T12 [get_ports led_locked]
set_property PACKAGE_PIN U12 [get_ports led_heartbeat]
set_property IOSTANDARD LVCMOS33 [get_ports {sys_clk sys_rst_n hdmi_en led_locked led_heartbeat}]

# Constrain P-side package pins; Vivado derives the differential N-side mates.
set_property PACKAGE_PIN H16 [get_ports tmds_clk_p]
set_property PACKAGE_PIN D19 [get_ports {tmds_data_p[0]}]
set_property PACKAGE_PIN C20 [get_ports {tmds_data_p[1]}]
set_property PACKAGE_PIN B19 [get_ports {tmds_data_p[2]}]
set_property IOSTANDARD TMDS_33 [get_ports {tmds_clk_p tmds_clk_n tmds_data_p[*] tmds_data_n[*]}]

# Physical button is asynchronous; release is synchronized in each pixel domain.
set_false_path -from [get_ports sys_rst_n]
# Indicators carry status, not an externally clocked data interface.
set_false_path -to [get_ports {led_locked led_heartbeat}]
