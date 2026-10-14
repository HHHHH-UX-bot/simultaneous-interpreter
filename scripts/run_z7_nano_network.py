"""Select the network app on the already verified Z7N3 hardware."""
import sys
import run_z7_nano_wifi as wifi
import run_z7_nano_display as display


def configure():
    wifi.configure()
    display.PREPARES += ("prepare_z7_nano_network.py",)
    display.LINKER = display.OUT/"ui_network.ld"
    display.APP = ("software/ps_bringup/start.S", "software/network_ui/main.c",
        "software/network_ui/network.c", "software/network_ui/protocol.c", "software/ps_wifi/wifi_at_probe.c",
        "software/subtitle_ui/ui_model.c", "software/subtitle_ui/ui_render.c", "software/subtitle_ui/ui_demo.c",
        "software/subtitle_ui/runtime.c", "software/subtitle_ui/ui_touch.c", "output/subtitle_ui/ui_assets.c")
    display.EXTRA_APP_INPUTS += ("software/network_ui/network.h", "software/network_ui/protocol.h",
        "scripts/run_z7_nano_network.py", "scripts/prepare_z7_nano_network.py")


if __name__ == "__main__":
    if len(sys.argv)>1 and sys.argv[1]=="build":
        raise SystemExit("Use the verified UART hardware build; build-app selects network software only")
    configure()
    display.main()
