"""USB Host enumeration on CPU1; current CPU0 display keeps running."""
import argparse
import json
import shutil
import struct
import subprocess
import sys
from datetime import datetime,timezone
from pathlib import Path
import run_z7_nano as base
import run_z7_nano_display as display
import run_z7_nano_usb as probe
import prepare_z7_nano_touch
from inspect_z7_nano_usb import inspect

ROOT=base.ROOT
OUT=base.OUT/"usb"
VENDOR=ROOT/"software/third_party/tinyusb"
SRC=ROOT/"software/z7_nano_usb"
VENDOR_C=["src/tusb.c","src/common/tusb_fifo.c","src/host/usbh.c",
          "src/class/hid/hid_host.c","src/portable/ehci/ehci.c"]

def inputs():
    meta=json.loads((VENDOR/"UPSTREAM.json").read_text())
    vendor=["software/third_party/tinyusb/"+name for name in meta["local_files_sha256"]]
    for name,digest in meta["local_files_sha256"].items():
        if base.sha(VENDOR/name)!=digest:
            raise RuntimeError("Vendor provenance changed: "+name)
    return display.hashes(vendor+[
        "software/third_party/tinyusb/UPSTREAM.json","software/z7_nano_usb/host.c",
        "software/z7_nano_usb/usb_board.c","software/z7_nano_usb/usb_board.h",
        "software/z7_nano_usb/host_report.h","software/z7_nano_usb/host_link.ld",
        "software/z7_nano_usb/touch_input.c","software/z7_nano_usb/touch_input.h",
        "software/z7_nano_usb/touch_link.h","output/z7_nano/usb/touch_format.h",
        "scripts/prepare_z7_nano_touch.py","scripts/inspect_z7_nano_touch.py",
        "software/z7_nano_usb/tusb_config.h","software/ps_bringup/start.S",
        "scripts/run_z7_nano_usb_host.py","scripts/z7_nano_usb_host.tcl",
        "scripts/inspect_z7_nano_usb.py",
        "scripts/z7_nano_usb_stop.tcl","output/z7_nano/usb/usb_hw.h",
        "scripts/z7_nano_usb_cleanup.tcl",
        "hardware/z7_nano/source.json"])

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=("build","enumerate"))
    parser.add_argument("--observe-seconds",type=int,default=10)
    parser.add_argument("--power-trace",action="store_true",help="Record USB power status without changing power controls")
    parser.add_argument("--gcc",type=Path,default=base.GCC)
    parser.add_argument("--vivado",type=Path,default=base.VIVADO)
    parser.add_argument("--xsdb",type=Path,default=base.XSDB)
    args=parser.parse_args()
    if not 1<=args.observe_seconds<=300:
        parser.error("Observation must be 1..300 seconds")
    if args.action=="build":
        prepare_z7_nano_touch.main()
        before=inputs()
        display.save(OUT/"host_build.json",{"status":"building","input_hashes":before})
        flags=["-mcpu=cortex-a9","-marm","-mfloat-abi=soft","-mno-unaligned-access","-O2","-g","-ffreestanding",
               "-fno-builtin","-fno-stack-protector","-ffunction-sections","-fdata-sections",
               "-Wall","-Wextra","-Werror","-nostdlib","-Wl,--build-id=none,--gc-sections",
               "-T",SRC/"host_link.ld","-I",SRC,"-I",VENDOR/"src","-I",OUT,
               "-Wl,-Map="+str(OUT/"host.map")]
        base.run([args.gcc,*flags,ROOT/"software/ps_bringup/start.S",SRC/"host.c",SRC/"usb_board.c",SRC/"touch_input.c",
                  *(VENDOR/name for name in VENDOR_C),"-lgcc","-o",OUT/"host.elf"],
                 base.TMP/"usb_host_build","",60)
        data=(OUT/"host.elf").read_bytes()
        header=struct.unpack_from("<HHIIIIIHHHHHH",data,16)
        if data[:6]!=b"\x7fELF\x01\x01" or header[0]!=2 or header[1]!=40 or header[3]!=0x1000:
            raise RuntimeError("Host ELF must be ARM32 at Core1 OCM entry")
        segments=[]
        for index in range(header[9]):
            kind,offset,virtual,physical,file_size,memory_size,flags,alignment=struct.unpack_from(
                "<IIIIIIII",data,header[4]+index*header[8])
            if kind==1:
                if physical!=virtual or file_size>memory_size or offset+file_size>len(data) or \
                   physical<0x1000 or physical+memory_size>0x10000:
                    raise RuntimeError("Host ELF overlaps display OCM")
                segments.append({"start":physical,"file_bytes":file_size,"memory_bytes":memory_size})
        if inputs()!=before:
            raise RuntimeError("Host sources changed during compilation")
        display.save(OUT/"host_build.json",{"status":"passed","input_hashes":before,
                     "elf_sha256":base.sha(OUT/"host.elf"),"segments":segments,"unaligned_access":False})
    else:
        build=json.loads((OUT/"host_build.json").read_text())
        phy=json.loads((OUT/"probe_run.json").read_text())
        app=display.checked_app()
        active=json.loads((display.OUT/"active_app.json").read_text())
        if build["status"]!="passed" or build["input_hashes"]!=inputs() or base.sha(OUT/"host.elf")!=build["elf_sha256"]:
            raise RuntimeError("Current Host build required")
        if phy["status"]!="passed" or phy["input_hashes"]!=probe.inputs() or \
           phy["display_program_attempt"]!=active["program_attempt"] or active["status"]!="ready" or \
           active["elf_sha256"]!=app["elf_sha256"]:
            raise RuntimeError("Current PHY proof and running display required")
        folder=OUT/"host_attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        folder.mkdir(parents=True)
        shutil.copyfile(OUT/"host.elf",folder/"host.elf")
        shutil.copyfile(OUT/"connection.json",folder/"connection.json")
        if base.sha(folder/"host.elf")!=build["elf_sha256"]:
            raise RuntimeError("Host ELF snapshot changed")
        with base.hardware_lock():
            live=display.probe(args,folder)
            if live["usr_access"]!=0x5a374e32 or not live["done"]:
                raise RuntimeError("Live Z7N2 platform required")
            try:
                base.run([args.xsdb,ROOT/"scripts/z7_nano_usb_host.tcl",live["cable_serial"],
                          folder/"host.elf",folder,args.observe_seconds*1000,int(args.power_trace)],
                         folder/"run","PASS_Z7_NANO_USB_HOST",args.observe_seconds+60)
            except (subprocess.TimeoutExpired,KeyboardInterrupt,RuntimeError):
                # The child is dead here; retain the hardware lock through one bounded stop.
                try:
                    base.run([args.xsdb,ROOT/"scripts/z7_nano_usb_cleanup.tcl",live["cable_serial"],
                              folder/"cleanup.json"],folder/"cleanup","PASS_Z7_NANO_USB_CLEANUP",15)
                except Exception as cleanup_error:
                    print("USB DMA cleanup not confirmed: "+str(cleanup_error),file=sys.stderr)
                raise
        result=json.loads((folder/"result.json").read_text())
        descriptors=inspect(folder/"snapshot.bin")
        if descriptors["device"]["vid"]!=result["vid"] or descriptors["device"]["pid"]!=result["pid"]:
            raise RuntimeError("Captured USB identity differs from Host report")
        display.save(folder/"descriptors.json",descriptors)
        touch_words=struct.unpack("<64I",(folder/"touch.bin").read_bytes())
        pending=(touch_words[16]-touch_words[17])&0xffffffff
        if touch_words[0]!=0x544f5531 or touch_words[1]&1 or pending>8:
            raise RuntimeError("Touch capture ABI or queue state invalid")
        tap_indices=[(touch_words[17]+i)&0xffffffff for i in range(pending)]
        touch={"reports":touch_words[2],"pressed":touch_words[3],"x":touch_words[4],"y":touch_words[5],
               "raw_x":touch_words[6],"raw_y":touch_words[7],"downs":touch_words[9],"ups":touch_words[10],
               "moves":touch_words[11],"raw_range":list(touch_words[12:16]),"tap_write":touch_words[16],
               "tap_read":touch_words[17],"dropped_taps":touch_words[18],
               "taps":[dict(zip(("press_x","press_y","x","y"),touch_words[32+(i%8)*4:36+(i%8)*4])) for i in tap_indices]}
        display.save(folder/"touch.json",touch)
        feedback_words=struct.unpack("<16I",(folder/"feedback.bin").read_bytes())
        if feedback_words[0]!=0x544f5532 or feedback_words[1]&1 or feedback_words[2]!=touch_words[20]:
            raise RuntimeError("Current UI touch feedback required")
        feedback={"generation":feedback_words[2],"read_taps":feedback_words[3],
                  "accepted":dict(zip(("start","pause","resume","clear","font_minus","font_plus","toggle_source"),feedback_words[4:11])),
                  "ignored":feedback_words[11],"last_command":feedback_words[12],
                  "last_x":feedback_words[13],"last_y":feedback_words[14]}
        display.save(folder/"feedback.json",feedback)
        result.update(attempt=folder.relative_to(ROOT).as_posix(),elf_sha256=build["elf_sha256"],
                      display_program_attempt=active["program_attempt"],input_hashes=build["input_hashes"],
                      snapshot_sha256=base.sha(folder/"snapshot.bin"),feedback=feedback,
                      feedback_sha256=base.sha(folder/"feedback.bin"),
                      touch_sha256=base.sha(folder/"touch.bin"),touch=touch,
                      observation_seconds=args.observe_seconds,
                      connection_sha256=base.sha(folder/"connection.json"))
        if args.power_trace:
            result["power_trace_sha256"]=base.sha(folder/"power_trace.csv")
        display.save(OUT/"host_run.json",result)
    print("PASS_Z7_NANO_USB_HOST_"+args.action.upper())

if __name__=="__main__":
    main()
