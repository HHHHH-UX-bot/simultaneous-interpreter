"""Generate only the descriptor and constants proven for the attached screen."""
import json
from pathlib import Path
import run_z7_nano as base
from inspect_z7_nano_touch import layout

ROOT=base.ROOT
OUT=base.OUT/"usb"
def main():
    proof=json.loads((OUT/"touch_capture.json").read_text())
    folder=ROOT/proof["host_attempt"]
    if proof["status"]!="passed" or base.sha(folder/"snapshot.bin")!=proof["snapshot_sha256"]:
        raise RuntimeError("Actual touch capture required")
    capture=json.loads((folder/"descriptors.json").read_text())
    format=layout(capture)
    if format["descriptor_sha256"]!=proof["descriptor_sha256"]:
        raise RuntimeError("HID descriptor no longer matches actual capture")
    contacts=format["contacts"]
    if len(contacts)!=5 or format["report_id"]!=1 or format["report_bytes"]!=44 or format["contact_count"]!={"bit_offset":344,"bits":8}:
        raise RuntimeError("Expected captured five-contact format")
    expected={"tip":(0,1,0,1),"id":(8,8,0,1),"x":(16,16,0,4096),"y":(32,16,0,4096),"width":(48,16,0,4096)}
    for index,contact in enumerate(contacts):
        for name,(offset,bits,minimum,maximum) in expected.items():
            field=contact[name]
            if field!={"bit_offset":8+index*64+offset,"bits":bits,"logical_min":minimum,"logical_max":maximum}:
                raise RuntimeError("Unexpected contact field "+name)
    descriptor=bytes.fromhex(capture["hid_report_descriptors"][0]["bytes"])
    body="\n".join("    "+",".join(f"0x{v:02x}" for v in descriptor[i:i+16])+"," for i in range(0,len(descriptor),16))
    (OUT/"touch_format.h").write_text(
        "/* Generated from the actual KD070V02 HID capture; do not hand-edit. */\n"
        "#ifndef Z7_TOUCH_FORMAT_H\n#define Z7_TOUCH_FORMAT_H\n#include <stdint.h>\n"
        f"static const uint8_t touch_report_descriptor[{len(descriptor)}]={{\n{body}\n}};\n"
        "#define TOUCH_VID 0x1a86u\n#define TOUCH_PID 0xe5e3u\n"
        "#define TOUCH_REPORT_ID 1u\n#define TOUCH_REPORT_BYTES 44u\n"
        "#define TOUCH_SLOTS 5u\n#define TOUCH_STRIDE 8u\n#define TOUCH_RAW_MAX 4096u\n"
        "#define TOUCH_COUNT_OFFSET 43u\n#endif\n",encoding="utf-8")
    (OUT/"touch_format.json").write_text(json.dumps({"status":"passed","host_attempt":proof["host_attempt"],
        "descriptor_sha256":format["descriptor_sha256"],"header_sha256":base.sha(OUT/"touch_format.h")},indent=2)+"\n")
    print("PASS_TOUCH_FORMAT")
if __name__=="__main__":
    main()
