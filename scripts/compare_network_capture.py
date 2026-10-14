"""Reuse exact native pixel comparison with the current network app evidence."""
import argparse
from pathlib import Path
import run_network_ui as app
import compare_subtitle_capture as pixels
def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--gcc-directory',required=True,type=Path);a=p.parse_args()
    app.checked_app();app.checked_platform()
    return pixels.compare(a.folder,a.gcc_directory,app.OUT,
        app.ROOT/'output/subtitle_ui/host_validation.json',app.OUT/'build-app.json')
if __name__=='__main__':raise SystemExit(main())
