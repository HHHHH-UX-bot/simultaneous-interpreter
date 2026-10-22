# PG160's DDR constraints analyse the fastest supported PHY RX clock.
# Firmware advertises 100BASE-T full duplex only (actual RXC=25 MHz).
create_clock -name eth_rx_clock -period 8.000 [get_ports eth_rxc]
