"""Extract USB MIO settings without reinitializing the running DDR/display."""
import json
import re
from run_z7_nano import ROOT, OUT, sha


def main():
    folder = OUT/"usb"
    board = json.loads((ROOT/"hardware/z7_nano/source.json").read_text(encoding="utf-8"))
    source = folder/"config/ps7_init.tcl"
    text = source.read_text(encoding="utf-8")
    section = text.split("proc ps7_mio_init_data_3_0 {} {", 1)[1].split("\n}", 1)[0]
    all_mio = {int(address, 16): (int(mask, 16), int(value, 16))
        for address, mask, value in re.findall(r"mask_write\s+(0X[0-9A-F]+)\s+(0x[0-9A-F]+)\s+(0x[0-9A-F]+)", section)}
    first, last = board["USB"]["ULPI_MIO"]
    pins = list(range(first, last+1))
    pairs = [(0xf8000700+4*pin, *all_mio[0xf8000700+4*pin]) for pin in pins]
    reset = board["USB"]["ResetMIO"]
    # GPIO on the same 1.8 V MIO bank, with the peripheral selector cleared.
    reset_config = pairs[0][2]&~0xff
    header = ["/* USB-only configuration; no DDR, PLL or PS-PL reset operations. */",
        f"#define USB_BASE 0x{int(board['USB']['BaseAddress'],16):08x}u",
        f"#define USB_RESET_MIO {reset}u", f"#define USB_RESET_CONFIG 0x{reset_config:08x}u",
        "static const uint32_t usb_mio_config[][3] = {"]
    header += [f"    {{0x{address:08x}u,0x{mask:08x}u,0x{value:08x}u}}," for address, mask, value in pairs]
    header += ["};"]
    (folder/"usb_hw.h").write_text("\n".join(header)+"\n", encoding="utf-8")
    (folder/"configuration.json").write_text(json.dumps({"board_source_sha256": sha(ROOT/"hardware/z7_nano/source.json"),
        "vivado_usb_init_sha256": sha(source), "ulpi_mio": pins, "reset_mio": reset,
        "reset_mio_config": reset_config, "mio_pairs": pairs,
        "scope": "USB MIO only; generated full PS init is reference data and is not executed"}, indent=2)+"\n", encoding="utf-8")
    print("PASS_Z7_NANO_USB_PREPARE")


if __name__ == "__main__":
    main()
