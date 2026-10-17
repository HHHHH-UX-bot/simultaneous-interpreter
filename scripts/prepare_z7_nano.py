"""Generate board inputs from the single Z7-Nano source record."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/z7_nano/config"


def main():
    board = json.loads((ROOT / "hardware/z7_nano/source.json").read_text(encoding="utf-8"))
    OUT.mkdir(parents=True, exist_ok=True)
    xdc = ["# Generated from hardware/z7_nano/source.json.",
           "create_clock -name sys_clk -period 20.000 [get_ports sys_clk]"]
    for port, pin in board["Pins"].items():
        if port.startswith("tmds_") and ("_n" in port):
            continue  # Differential negative mates are derived by Vivado.
        xdc.append(f"set_property PACKAGE_PIN {pin} [get_ports {{{port}}}]")
    xdc += ["set_property IOSTANDARD LVCMOS33 [get_ports {sys_clk sys_rst_n led_n}]",
            "set_property IOSTANDARD TMDS_33 [get_ports {tmds_clk_p tmds_clk_n tmds_data_p[*] tmds_data_n[*]}]",
            "set_property PULLUP true [get_ports sys_rst_n]",
            "set_false_path -from [get_ports sys_rst_n]",
            "set_false_path -to [get_ports led_n]"]
    (OUT / "hdmi.xdc").write_text("\n".join(xdc)+"\n", encoding="utf-8")
    pins = " ".join(f"{{{p}}} {v}" for p, v in board["Pins"].items())
    (OUT / "board.tcl").write_text(
        f"set board_part {{{board['Part']}}}\nset board_serial {{{board['JTAGSerial']}}}\n"
        f"set board_pins [list {pins}]\n", encoding="utf-8")
    pairs = "\n".join(f"    CONFIG.{k} {{{v}}} \\" for k, v in sorted(board["PSParameters"].items()))
    (OUT / "ps7_config.tcl").write_text(
        "# Generated from hardware/z7_nano/source.json.\n"
        "proc configure_z7_nano_ps {cell} {\n    set_property -dict [list \\\n"
        + pairs + "\n    ] $cell\n}\n", encoding="utf-8")

    # Audio loopback constraints. Kept in a separate file so the accepted
    # display build inputs (hdmi.xdc, board.tcl) stay byte-identical.
    audio = board["AudioPins"]
    bclk_period = board["AudioBclkPeriodNs"]
    master = board.get("AudioRole", "slave") == "master"
    axdc = ["# Generated from hardware/z7_nano/source.json (AudioPins).",
            "# JP2 (GPIO2) pins 13-19; mapping and evidence: hardware/z7_nano/audio_module.json"]
    if master:
        # BCLK is a 3.125 MHz divided output (320 ns per bit) generated from the
        # 50 MHz pixel clock, so every bit of the interface is slow relative to
        # the fabric. The I/O paths are therefore excluded from timing analysis;
        # replace this with create_generated_clock plus I/O delays once the
        # interface is proven on hardware (RISKS R20).
        pass
    else:
        axdc.append(f"create_clock -name i2s_bclk -period {bclk_period} [get_ports i2s_bclk]")
    for port, pin in audio.items():
        axdc.append(f"set_property PACKAGE_PIN {pin} [get_ports {{{port}}}]")
    axdc += [
        f"set_property IOSTANDARD LVCMOS33 [get_ports {{{' '.join(audio.keys())}}}]",
        "set_property PULLUP true [get_ports {i2c_scl i2c_sda}]"]
    if master:
        # The PL drives BCLK, both LRCLKs and DACDAT at 3.125 MHz; the codec only
        # drives ADCDAT back. Slow divided outputs are excluded from timing.
        axdc += [
            "set_false_path -to [get_ports {i2s_bclk i2s_dadlrc i2s_adclrc i2s_dacdat}]",
            "set_false_path -from [get_ports i2s_adcdat]",
        ]
    else:
        axdc += [
            "# The module drives ADCDAT/LRCLKs from its own BCLK; these are conservative",
            "# board estimates until the wiring is measured.",
            "set_input_delay -clock i2s_bclk -max 20.000 [get_ports {i2s_adcdat i2s_adclrc i2s_dadlrc}]",
            "set_input_delay -clock i2s_bclk -min 2.000 [get_ports {i2s_adcdat i2s_adclrc i2s_dadlrc}]",
            "set_output_delay -clock i2s_bclk -max 20.000 [get_ports i2s_dacdat]",
            "set_output_delay -clock i2s_bclk -min 2.000 [get_ports i2s_dacdat]",
        ]
    axdc += [
        "# I2C is a 100 kHz open-drain bus: no meaningful setup/hold relationship.",
        "set_false_path -from [get_ports {i2c_scl i2c_sda}]",
        "set_false_path -to [get_ports {i2c_scl i2c_sda}]",
    ]
    if not master:
        axdc += [
            "# In slave mode BCLK comes from the module crystal, so it is "
            "asynchronous to the PL MMCM.",
            "set_clock_groups -asynchronous -group {i2s_bclk} \\",
            "    -group [get_clocks -of_objects [get_pins u_clock/u_mmcm/CLKOUT0]]",
        ]
    (OUT / "audio.xdc").write_text("\n".join(axdc) + "\n", encoding="utf-8")
    print("PASS_Z7_NANO_PREPARE")


if __name__ == "__main__":
    main()
