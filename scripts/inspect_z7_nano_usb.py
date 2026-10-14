"""Decode the captured USB descriptors without guessing touch coordinate bytes."""
import argparse
import json
import struct
from pathlib import Path

def configuration(data):
    descriptors=[]
    at=0
    while at<len(data):
        size,kind=data[at:at+2]
        if size<2 or at+size>len(data):
            raise ValueError("Malformed USB descriptor length")
        part=data[at:at+size]
        row={"offset":at,"type":kind,"bytes":part.hex()}
        if kind==4 and size>=9:
            row.update(interface=part[2],alternate=part[3],endpoints=part[4],
                       usb_class=part[5],subclass=part[6],protocol=part[7])
        elif kind==5 and size>=7:
            row.update(endpoint=part[2],attributes=part[3],max_packet=struct.unpack_from("<H",part,4)[0],interval=part[6])
        descriptors.append(row)
        at+=size
    return descriptors

def inspect(path):
    data=path.read_bytes()
    if len(data)!=3728:
        raise ValueError("Host snapshot size must be 3728 bytes")
    header=struct.unpack_from("<12I",data)
    device_length,config_length=header[:2]
    if device_length!=18 or config_length>512 or any(size>512 for size in header[2:6]):
        raise ValueError("Descriptor capture bounds failed")
    device=data[48:48+device_length]
    config=data[80:80+config_length]
    hid=[data[592+512*i:592+512*i+size] for i,size in enumerate(header[2:6])]
    raw_count=header[10]
    raw=[]
    for index in range(max(0,raw_count-16),raw_count):
        offset=2640+(index%16)*68
        interface,length=data[offset:offset+2]
        if length>64:
            raise ValueError("Raw HID report size invalid")
        raw.append({"index":index,"interface":interface,"length":length,"bytes":data[offset+4:offset+4+length].hex()})
    return {"device":{"bytes":device.hex(),"usb_class":device[4],"subclass":device[5],"protocol":device[6],
                      "endpoint0_max_packet":device[7],"vid":struct.unpack_from("<H",device,8)[0],
                      "pid":struct.unpack_from("<H",device,10)[0],"configurations":device[17]},
            "configuration_length":config_length,"configuration":configuration(config),
            "hid_report_descriptors":[{"interface_index":i,"interface_protocol":header[6+i],"length":len(d),"bytes":d.hex()}
                                      for i,d in enumerate(hid) if d],
            "raw_count":raw_count,"recent_raw_reports":raw}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("snapshot",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    result=inspect(args.snapshot)
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print("PASS_USB_DESCRIPTORS",json.dumps(result["device"]),"HID",len(result["hid_report_descriptors"]))

if __name__=="__main__":
    main()
