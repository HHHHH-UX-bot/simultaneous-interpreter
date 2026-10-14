"""Derive UART pins and the AT/touch memory layout; do not touch hardware."""
import json
import shutil
from run_z7_nano import ROOT, OUT, sha


def main():
    wiring = json.loads((ROOT/"hardware/z7_nano/esp8266.json").read_text(encoding="utf-8"))
    if wiring["status"] != "user_confirmed_wiring" or wiring["io_standard"] != "LVCMOS33 / Bank34":
        raise RuntimeError("Confirmed new-board 3.3V UART wiring required")
    pins = {"esp_tx": wiring["signals"][0]["package_pin"], "esp_rx": wiring["signals"][1]["package_pin"]}
    if pins != {"esp_tx": "N17", "esp_rx": "P18"}:
        raise RuntimeError("Unexpected new-board UART pin mapping")
    simulation = json.loads((ROOT/"output/ps_wifi/simulation.json").read_text(encoding="utf-8"))
    if simulation["Status"] != "passed" or any(sha(ROOT/p) != h for p, h in simulation["SourceHashes"].items()):
        raise RuntimeError("Current full-address UART simulation required")
    at_tests = json.loads((ROOT/"output/ps_wifi/probe_host_tests.json").read_text(encoding="utf-8"))
    if at_tests["Status"] != "passed" or any(sha(ROOT/p) != h for p, h in at_tests["SourceHashes"].items()):
        raise RuntimeError("Current autonomous AT checks required")
    folder = OUT/"wifi"
    folder.mkdir(parents=True, exist_ok=True)
    for name in ("ps7_config.tcl", "framebuffer_hw.h"):
        shutil.copyfile(OUT/"display"/name, folder/name)
    (folder/"uart.xdc").write_text("\n".join(
        f"set_property PACKAGE_PIN {pin} [get_ports {port}]\nset_property IOSTANDARD LVCMOS33 [get_ports {port}]"
        for port, pin in pins.items())+"\n"
        "set_property DRIVE 8 [get_ports esp_tx]\nset_property SLEW SLOW [get_ports esp_tx]\n"
        "set_property PULLUP true [get_ports esp_rx]\n"
        "# The independent UART peer has no synchronous external clock.\n"
        "set_false_path -from [get_ports esp_rx]\nset_false_path -to [get_ports esp_tx]\n", encoding="utf-8")
    (folder/"board.tcl").write_text("source [file join $root output z7_nano config board.tcl]\n"+
        "\n".join(f"dict set board_pins {port} {pin}" for port, pin in pins.items())+"\n", encoding="utf-8")
    linker = (ROOT/"software/subtitle_ui/link.ld").read_text(encoding="utf-8")
    anchor = "    .ui_assets :"
    if linker.count(anchor) != 1:
        raise RuntimeError("UI linker anchor changed")
    section = ('    .wifi_mailbox 0x0001E100 (NOLOAD) : { KEEP(*(.wifi_mailbox)) } > OCM\n'
               '    ASSERT(SIZEOF(.wifi_mailbox) == 0x40, "Wi-Fi status ABI changed")\n')
    (folder/"ui_at.ld").write_text(linker.replace(anchor, section+anchor), encoding="utf-8")
    (folder/"configuration.json").write_text(json.dumps({
        "usr_access": 0x5a374e33, "wiring_sha256": sha(ROOT/"hardware/z7_nano/esp8266.json"),
        "pins": pins, "io_standard": "LVCMOS33", "uart_base": 0x43c20000,
        "clock_hz": 50000000, "divisor": 434, "baud": 115200, "format": "8N1",
        "uart_simulation": "output/ps_wifi/simulation.json", "at_checks": "output/ps_wifi/probe_host_tests.json",
        "scope": "Generated new-board UART/AT configuration; no hardware execution or Wi-Fi association"
    }, indent=2)+"\n", encoding="utf-8")
    print("PASS_Z7_NANO_WIFI_PREPARE")


if __name__ == "__main__":
    main()
