"""Derive this screen's input fields from its captured HID descriptor."""
import argparse
import hashlib
import json
from pathlib import Path

def input_fields(descriptor):
    state={0:0,1:0,2:0,7:0,8:0,9:0}
    stack=[]
    usages=[]
    collections=[]
    offsets={}
    fields=[]
    at=0
    collection_id=0
    while at<len(descriptor):
        prefix=descriptor[at];at+=1
        if prefix==0xfe:
            raise ValueError("Long HID items are outside this captured format")
        size=(0,1,2,4)[prefix&3]
        if at+size>len(descriptor):
            raise ValueError("Truncated HID item")
        data=descriptor[at:at+size];at+=size
        value=int.from_bytes(data,"little")
        kind=(prefix>>2)&3;tag=prefix>>4
        if kind==1:
            if tag==10:
                stack.append(dict(state))
            elif tag==11:
                state=stack.pop()
            elif tag in state:
                state[tag]=int.from_bytes(data,"little",signed=True) if tag==1 or (tag==2 and state[1]<0) else value
        elif kind==2:
            if tag!=0:
                raise ValueError("Usage ranges are outside this captured format")
            usages.append(value if size==4 else (state[0]<<16)|value)
        elif kind==0:
            if tag==10:
                collection_id+=1
                collections.append((collection_id,usages[-1] if usages else 0))
            elif tag==12:
                collections.pop()
            elif tag==8:
                rid=state[8]
                offset=offsets.get(rid,8 if rid else 0)
                if not value&1:
                    if value&6!=2 or not usages:
                        raise ValueError("Touch inputs must be absolute variable fields")
                    for index in range(state[9]):
                        fields.append({"report_id":rid,"usage":usages[min(index,len(usages)-1)],
                            "bit_offset":offset+index*state[7],"bits":state[7],
                            "logical_min":state[1],"logical_max":state[2],
                            "finger":next((n for n,u in reversed(collections) if u==0x0d0022),None)})
                offsets[rid]=offset+state[7]*state[9]
            usages=[]
    if stack or collections:
        raise ValueError("Unclosed HID state")
    return fields,offsets

def layout(capture):
    device=capture["device"]
    if (device["vid"],device["pid"])!=(0x1a86,0xe5e3) or len(capture["hid_report_descriptors"])!=1:
        raise ValueError("Expected the captured KD070V02 USB device")
    hid=capture["hid_report_descriptors"][0]
    descriptor=bytes.fromhex(hid["bytes"])
    fields,offsets=input_fields(descriptor)
    counts=[f for f in fields if f["usage"]==0x0d0054]
    if len(counts)!=1:
        raise ValueError("Expected one contact count input")
    count=counts[0];rid=count["report_id"]
    selected=[f for f in fields if f["report_id"]==rid]
    names={0x0d0042:"tip",0x0d0051:"id",0x010030:"x",0x010031:"y",0x0d0048:"width"}
    contacts=[]
    for finger in dict.fromkeys(f["finger"] for f in selected if f["finger"] is not None):
        contact={names[f["usage"]]:{k:f[k] for k in ("bit_offset","bits","logical_min","logical_max")}
                 for f in selected if f["finger"]==finger and f["usage"] in names}
        if not {"tip","id","x","y"}<=contact.keys():
            raise ValueError("Incomplete contact fields")
        contacts.append(contact)
    scan=next(f for f in selected if f["usage"]==0x0d0056)
    return {"usb_identity":{"vid":device["vid"],"pid":device["pid"]},
            "descriptor_sha256":hashlib.sha256(descriptor).hexdigest(),"descriptor_length":len(descriptor),
            "interface_index":hid["interface_index"],"report_id":rid,"report_bytes":(offsets[rid]+7)//8,
            "contacts":contacts,"contact_count":{k:count[k] for k in ("bit_offset","bits")},
            "scan_time":{k:scan[k] for k in ("bit_offset","bits")}}

def value(data,field):
    offset=field["bit_offset"];bits=field["bits"]
    return (int.from_bytes(data,"little")>>offset)&((1<<bits)-1)

def decode(data,format):
    if len(data)!=format["report_bytes"] or data[0]!=format["report_id"]:
        raise ValueError("HID input does not match the captured descriptor")
    count=value(data,format["contact_count"])
    if count>len(format["contacts"]):
        raise ValueError("Contact count exceeds descriptor slots")
    contacts=[]
    for fields in format["contacts"][:count]:
        contact={name:value(data,field) for name,field in fields.items()}
        for axis in ("x","y"):
            if not fields[axis]["logical_min"]<=contact[axis]<=fields[axis]["logical_max"]:
                raise ValueError("Contact outside descriptor logical range")
        contacts.append(contact)
    return {"contact_count":count,"scan_time":value(data,format["scan_time"]),"contacts":contacts}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("descriptors",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    capture=json.loads(args.descriptors.read_text())
    result=layout(capture)
    result["recent_inputs"]=[dict(index=r["index"],**decode(bytes.fromhex(r["bytes"]),result))
                             for r in capture["recent_raw_reports"]]
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("PASS_TOUCH_LAYOUT",result["report_bytes"],"bytes",len(result["contacts"]),"slots",len(result["recent_inputs"]),"captured inputs")

if __name__=="__main__":
    main()
