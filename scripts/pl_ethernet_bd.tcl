# PL RTL8211F (schematic sheets 5/11), 100BASE-T full duplex.
# Etherlite owns bounded on-chip TX/RX buffers; PG160 handles RGMII DDR/clocks.
set eth [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_ethernetlite:3.0 ethernet]
set_property -dict [list CONFIG.C_INCLUDE_GLOBAL_BUFFERS {0} CONFIG.C_DUPLEX {1} CONFIG.C_INCLUDE_MDIO {1}] $eth
# Vivado 2018.3 ships PG160 v4.0, 2026.1 ships v4.1; use the installed one.
set bridge [create_bd_cell -type ip -vlnv [lindex [lsort -dictionary [get_ipdefs -all xilinx.com:ip:gmii_to_rgmii:*]] end] rgmii]
set_property -dict [list CONFIG.SupportLevel {Include_Shared_Logic_in_Core} CONFIG.C_PHYADDR {8} CONFIG.RGMII_TXC_SKEW {0}] $bridge
connect_bd_intf_net [get_bd_intf_pins regs/M04_AXI] [get_bd_intf_pins ethernet/S_AXI]
connect_bd_net $clk [get_bd_pins ethernet/s_axi_aclk]
connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins ethernet/s_axi_aresetn]
connect_bd_net [get_bd_pins reset/peripheral_reset] [get_bd_pins rgmii/tx_reset] [get_bd_pins rgmii/rx_reset]
foreach {a b} {phy_tx_clk gmii_tx_clk phy_rx_clk gmii_rx_clk phy_crs gmii_crs phy_col gmii_col phy_dv gmii_rx_dv phy_rx_er gmii_rx_er phy_tx_en gmii_tx_en phy_mdc mdio_gem_mdc phy_mdio_i mdio_gem_i phy_mdio_o mdio_gem_o phy_mdio_t mdio_gem_t} {
    connect_bd_net [get_bd_pins ethernet/$a] [get_bd_pins rgmii/$b]
}
set tx [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 eth_tx]
set_property -dict [list CONFIG.IN0_WIDTH {4} CONFIG.IN1_WIDTH {4}] $tx
set zeros [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 eth_zeros]
set_property -dict [list CONFIG.CONST_WIDTH {4} CONFIG.CONST_VAL {0}] $zeros
connect_bd_net [get_bd_pins ethernet/phy_tx_data] [get_bd_pins eth_tx/In0]
connect_bd_net [get_bd_pins eth_zeros/dout] [get_bd_pins eth_tx/In1]
connect_bd_net [get_bd_pins eth_tx/dout] [get_bd_pins rgmii/gmii_txd]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins rgmii/gmii_tx_er]
set rx [create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice:1.0 eth_rx]
set_property -dict [list CONFIG.DIN_WIDTH {8} CONFIG.DIN_FROM {3} CONFIG.DIN_TO {0} CONFIG.DOUT_WIDTH {4}] $rx
connect_bd_net [get_bd_pins rgmii/gmii_rxd] [get_bd_pins eth_rx/Din]
connect_bd_net [get_bd_pins eth_rx/Dout] [get_bd_pins ethernet/phy_rx_data]
foreach {cell pin name} {rgmii clkin eth_ref_clk rgmii rgmii_txd eth_txd rgmii rgmii_txc eth_txc rgmii rgmii_tx_ctl eth_tx_ctl rgmii rgmii_rxd eth_rxd rgmii rgmii_rxc eth_rxc rgmii rgmii_rx_ctl eth_rx_ctl rgmii mdio_phy_mdc eth_mdc rgmii mdio_phy_i eth_mdio_i rgmii mdio_phy_o eth_mdio_o rgmii mdio_phy_t eth_mdio_t ethernet phy_rst_n eth_reset_n} {
    make_bd_pins_external [get_bd_pins $cell/$pin]
    set_property name $name [get_bd_ports ${pin}_0]
}
set_property CONFIG.FREQ_HZ 200000000 [get_bd_ports eth_ref_clk]
create_bd_addr_seg -range 0x10000 -offset 0x40e00000 [get_bd_addr_spaces ps7/Data] [get_bd_addr_segs ethernet/S_AXI/Reg] SEG_ETHERNET
