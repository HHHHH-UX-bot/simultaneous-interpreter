"""Capture CH0/CH1 from SLogic Combo 8 without the bundled GLib timer path.

Vendor command and sample packing:
https://github.com/sipeed/libsigrok/tree/slogic-dev/src/hardware/sipeed-slogic-analyzer/slogic
Raw files may contain live communication: keep them in ignored output/.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
import time
from pathlib import Path


def capture(dll, output, seconds, rate=20_000_000):
    directory = os.add_dll_directory(str(dll.resolve().parent))
    usb = c.CDLL(str(dll.resolve()))
    def bind(name, result, *args):
        fn = getattr(usb, name)
        fn.restype, fn.argtypes = result, list(args)
        return fn
    ptr = c.c_void_p
    bind('libusb_init', c.c_int, c.POINTER(ptr))
    bind('libusb_exit', None, ptr)
    bind('libusb_open_device_with_vid_pid', ptr, ptr, c.c_uint16, c.c_uint16)
    bind('libusb_close', None, ptr)
    bind('libusb_claim_interface', c.c_int, ptr, c.c_int)
    bind('libusb_release_interface', c.c_int, ptr, c.c_int)
    bind('libusb_control_transfer', c.c_int, ptr, c.c_uint8, c.c_uint8,
         c.c_uint16, c.c_uint16, ptr, c.c_uint16, c.c_uint)
    bind('libusb_bulk_transfer', c.c_int, ptr, c.c_ubyte, ptr,
         c.c_int, c.POINTER(c.c_int), c.c_uint)
    context = ptr()
    if usb.libusb_init(c.byref(context)):
        raise RuntimeError('libusb_init failed')
    handle = usb.libusb_open_device_with_vid_pid(context, 0x359f, 0x0300)
    if not handle:
        usb.libusb_exit(context)
        raise RuntimeError('SLogic Combo 8 (359f:0300) not available')
    claimed = False
    buffer = c.create_string_buffer(256 * 1024)
    transferred = c.c_int()
    def drain():
        for _ in range(32):
            code = usb.libusb_bulk_transfer(handle, 0x81, buffer, 65536, c.byref(transferred), 50)
            if code or not transferred.value:
                break
    try:
        code = usb.libusb_claim_interface(handle, 0)
        if code:
            raise RuntimeError(f'USB claim failed: {code}; close other analyzer applications')
        claimed = True
        drain()
        command = c.create_string_buffer(struct.pack('<HBB', rate // 1_000_000, 2, 0))
        code = usb.libusb_control_transfer(handle, 0x40, 0xb1, 0, 0, command, 4, 500)
        if code != 4:
            raise RuntimeError(f'SLogic start failed: {code}')
        output.mkdir(parents=True, exist_ok=True)
        samples = int(seconds * rate)
        wanted = (samples + 3) // 4
        total = packets = 0
        digest = hashlib.sha256()
        started = time.monotonic()
        print('CAPTURE_ARMED', flush=True)
        with (output / 'samples_2ch.bin').open('wb') as stream:
            while total < wanted:
                size = min(len(buffer), ((wanted-total+511)//512)*512)
                code = usb.libusb_bulk_transfer(handle, 0x81, buffer, size, c.byref(transferred), 1500)
                if code or not transferred.value:
                    raise RuntimeError(f'USB capture incomplete: code={code}, bytes={total}/{wanted}')
                chunk = buffer.raw[:min(transferred.value, wanted-total)]
                stream.write(chunk)
                digest.update(chunk)
                total += len(chunk)
                packets += 1
        elapsed = time.monotonic()-started
        record = {'status': 'captured', 'device': '359f:0300 / SLogic Combo 8',
                  'sample_rate_hz': rate, 'channels': [0, 1], 'samples': samples,
                  'packing': 'four samples per byte, earliest sample in bits 1:0',
                  'bytes': total, 'bulk_transfers': packets, 'wall_seconds': round(elapsed, 6),
                  'sample_seconds': samples/rate, 'sha256': digest.hexdigest(),
                  'method': 'vendor Combo8 start command and synchronous libusb bulk; no GLib session',
                  'dll_sha256': hashlib.sha256(dll.read_bytes()).hexdigest()}
        (output / 'capture.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(record), flush=True)
        return record
    finally:
        if claimed:
            drain()
            usb.libusb_release_interface(handle, 0)
        usb.libusb_close(handle)
        usb.libusb_exit(context)
        directory.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=2)
    args = parser.parse_args()
    if not 0 < args.seconds <= 120:
        parser.error('--seconds must be in (0,120]')
    capture(args.dll, args.output, args.seconds)
