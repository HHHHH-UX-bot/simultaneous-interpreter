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
    print("PASS_Z7_NANO_PREPARE")


if __name__ == "__main__":
    main()
