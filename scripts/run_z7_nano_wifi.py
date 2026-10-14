"""New-board UART/AT variant; share the accepted display and USB runtimes."""
import run_z7_nano_display as display


def configure():
    display.OUT = display.base.OUT/"wifi"
    display.PLATFORM_ID = 0x5a374e33
    display.TOP = "z7_nano_wifi_top"
    display.BUILD_SCRIPT = "build_z7_nano_wifi.tcl"
    display.PROGRAM_ACTION = "program-wifi"
    display.PREPARES = ("prepare_z7_nano.py", "prepare_z7_nano_display.py", "prepare_z7_nano_wifi.py")
    display.PREFIX = "wifi"
    display.LINKER = display.OUT/"ui_at.ld"
    display.RTL = ("z7_nano_wifi_top.v", "framebuffer_video.v", "hdmi_clock.v", "async_rst_sync.v",
                   "dvi_encoder.v", "dvi_transmitter_top.v", "serializer_10_to_1.v",
                   "wifi_uart_phy.v", "wifi_byte_fifo.v", "wifi_uart_axi.v", "ps_wifi_uart.v")
    display.APP = ("software/ps_bringup/start.S", "software/ps_wifi/main.c", "software/ps_wifi/wifi_at_probe.c",
                   "software/subtitle_ui/ui_model.c", "software/subtitle_ui/ui_render.c",
                   "software/subtitle_ui/ui_demo.c", "software/subtitle_ui/runtime.c",
                   "software/subtitle_ui/ui_touch.c", "output/subtitle_ui/ui_assets.c")
    display.EXTRA_PLATFORM_INPUTS = ("hardware/z7_nano/esp8266.json", "scripts/prepare_z7_nano_wifi.py",
        "scripts/build_z7_nano_wifi.tcl", "scripts/run_z7_nano_wifi.py", "scripts/run_z7_nano_display.py",
        "scripts/hardware_z7_nano.tcl", "output/z7_nano/wifi/uart.xdc", "output/z7_nano/wifi/board.tcl")
    display.EXTRA_APP_INPUTS = ("software/subtitle_ui/main.c", "software/subtitle_ui/link.ld",
        "software/ps_wifi/wifi_at_probe.h", "scripts/prepare_z7_nano_wifi.py", "scripts/run_z7_nano_wifi.py",
        "scripts/run_z7_nano_display.py")
    display.EXTRA_APP_FLAGS = ("-mno-unaligned-access",)


if __name__ == "__main__":
    configure()
    display.main()
