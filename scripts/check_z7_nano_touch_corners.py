"""Verify the five physical taps requested in TL, TR, BR, BL, center order."""
import json
from pathlib import Path
import run_z7_nano as base
import run_z7_nano_display as display
OUT=base.OUT/"usb"
def main():
    run=json.loads((OUT/"host_run.json").read_text())
    touch=run["touch"]
    taps=touch["taps"]
    boxes=[(0,0,102,60),(921,0,1023,60),(921,539,1023,599),(0,539,102,599),(410,240,614,360)]
    passed=run["status"]=="passed" and len(taps)==5 and touch["dropped_taps"]==0 and touch["downs"]==5 and touch["ups"]==5
    checks=[]
    for name,box,tap in zip(("top-left","top-right","bottom-right","bottom-left","center"),boxes,taps):
        x0,y0,x1,y1=box
        inside=x0<=tap["press_x"]<=x1 and y0<=tap["press_y"]<=y1 and x0<=tap["x"]<=x1 and y0<=tap["y"]<=y1
        checks.append({"position":name,"expected_box":box,"tap":tap,"passed":inside})
        passed=passed and inside
    result={"status":"passed" if passed else "failed","scope":"five-point orientation/range and click edges, no UI button acceptance",
            "host_attempt":run["attempt"],"host_elf_sha256":run["elf_sha256"],"touch_sha256":run["touch_sha256"],
            "decoder_test_sha256":base.sha(OUT/"touch_input_tests.json"),"checks":checks,"touch":touch}
    display.save(OUT/"touch_corners.json",result)
    if not passed:
        raise RuntimeError("Five physical tap positions did not match; inspect touch_corners.json")
    print("PASS_TOUCH_CORNERS",[{"position":c["position"],"x":c["tap"]["x"],"y":c["tap"]["y"]} for c in checks])
if __name__=="__main__":
    main()
