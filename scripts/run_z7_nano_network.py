"""Select the network app on the already verified Z7N3 hardware."""
import sys
import run_z7_nano_wifi as wifi
import run_z7_nano_display as display


def configure():
    wifi.configure()
    display.PREPARES += ("prepare_z7_nano_network.py",)
    display.LINKER = display.OUT/"ui_network.ld"
    display.APP = ("software/ps_bringup/start.S", "software/network_ui/main.c",
        "software/network_ui/network.c", "software/network_ui/stream_pilot.c", "software/network_ui/protocol.c", "software/ps_wifi/wifi_at_probe.c",
        "software/network_ui/cloud_events.c", "software/network_ui/cloud_frame.c", "software/network_ui/cloud_wire.c", "software/network_ui/cloud_live.c",
        "software/subtitle_ui/ui_model.c", "software/subtitle_ui/ui_render.c", "software/subtitle_ui/ui_demo.c",
        "software/subtitle_ui/runtime.c", "software/subtitle_ui/ui_touch.c", "output/subtitle_ui/ui_assets.c")
    display.EXTRA_APP_INPUTS += ("software/network_ui/network.h", "software/network_ui/stream_pilot.h", "software/network_ui/protocol.h",
        "scripts/run_z7_nano_network.py", "scripts/prepare_z7_nano_network.py",
        "software/network_ui/cloud_events.h", "software/network_ui/cloud_wire.h", "software/network_ui/cloud_live.h",
        "third_party/jsmn/jsmn.h", "third_party/wslay/lib/wslay_frame.c", "third_party/wslay/lib/wslay_frame.h",
        "third_party/wslay/lib/wslay_net.h", "third_party/wslay/lib/includes/wslay/wslay.h")
    display.EXTRA_APP_FLAGS += ("-DZ7_UART_921600", "-DZ7_NETWORK_CAPABILITIES", "-ffunction-sections", "-fdata-sections",
        "-fstack-usage", "-I", str(display.ROOT/"third_party/wslay/lib/includes"))


if __name__ == "__main__":
    if len(sys.argv)>1 and sys.argv[1]=="build":
        raise SystemExit("Use the verified UART hardware build; build-app selects network software only")
    configure()
    display.main()
