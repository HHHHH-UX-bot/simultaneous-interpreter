#!/usr/bin/env python3
"""Send one firmware-pinned model to the dedicated QLM1 UDP endpoint.

The firmware compares both the declared SHA256 and the received bytes against
its compiled model digest. This tool never sends a destination memory address.
The stable source port/session allow rerunning after an interrupted upload.
"""
import argparse
import atexit
import hashlib
import select
import socket
import struct
import sys
import time
import zlib

MAGIC = 0x314D4C51
HEADER = struct.Struct("<8I")
BEGIN, DATA, STATUS, FINISH, ABORT = range(1, 6)
NAMES = {0: "OK", 1: "READY", 2: "BAD_FORMAT", 3: "WRONG_MODEL",
         4: "OFFSET", 5: "BAD_CRC", 6: "BUSY", 7: "BAD_DIGEST",
         8: "BAD_SESSION", 9: "HASH_ERROR", 10: "NOT_COMPLETE"}


def packet(op, session, sequence, offset=0, payload=b""):
    prefix = struct.pack("<7I", MAGIC, op, session[0], session[1],
                         sequence & 0xFFFFFFFF, offset, len(payload))
    crc = zlib.crc32(payload, zlib.crc32(prefix)) & 0xFFFFFFFF
    return prefix + struct.pack("<I", crc) + payload


def decode_ack(data, session):
    if len(data) != HEADER.size:
        return None
    magic, op, lo, hi, seq, offset, code, crc = HEADER.unpack(data)
    if magic != MAGIC or (lo, hi) != session or not (op & 0x80):
        return None
    if zlib.crc32(data[:28]) & 0xFFFFFFFF != crc:
        return None
    return op & 0x7F, seq, offset, code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model")
    parser.add_argument("--board", default="192.168.1.50")
    parser.add_argument("--port", type=int, default=4178)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--source-port", type=int, default=42078)
    parser.add_argument("--chunk", type=int, default=1400)
    parser.add_argument("--window", type=int, default=1)
    parser.add_argument("--pace-ms", type=float, default=0)
    parser.add_argument("--retry-ms", type=float, default=20)
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    if not 1 <= args.chunk <= 1400 or not 1 <= args.window <= 32:
        parser.error("chunk must be 1..1400 and window 1..32")
    if args.pace_ms < 0 or args.retry_ms <= 0 or args.timeout <= 0:
        parser.error("invalid timing")
    # Windows' default ~15.6 ms timer would stretch a submillisecond pacing
    # wait into a 50-minute upload. Scope the 1 ms request to this process.
    if sys.platform == "win32":
        import ctypes
        winmm = ctypes.WinDLL("winmm")
        if winmm.timeBeginPeriod(1) == 0:
            atexit.register(winmm.timeEndPeriod, 1)
    # Hash/read in chunks rather than loading a second full model into Python.
    with open(args.model, "rb") as model:
        sha = hashlib.sha256()
        size = 0
        for block in iter(lambda: model.read(1024 * 1024), b""):
            sha.update(block)
            size += len(block)
        digest = sha.digest()
        if not 0 < size <= 0xFFFFFFFF:
            raise SystemExit("model size is outside the firmware protocol")
        session = struct.unpack("<2I", digest[:8])
        if session == (0, 0):
            session = (1, 0)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((args.bind, args.source_port))
        sock.connect((args.board, args.port))
        sock.setblocking(False)
        seq = 0
        start = time.perf_counter()

        def send(op, offset=0, payload=b""):
            nonlocal seq
            seq += 1
            sock.send(packet(op, session, seq, offset, payload))
            return seq & 0xFFFFFFFF

        def receive(wait):
            if not select.select([sock], [], [], max(0, wait))[0]:
                return None
            return decode_ack(sock.recv(2048), session)

        def exchange(op, offset=0, payload=b""):
            deadline = time.perf_counter() + 5
            while time.perf_counter() < deadline:
                sequence = send(op, offset, payload)
                until = time.perf_counter() + 0.3
                while time.perf_counter() < until:
                    ack = receive(until - time.perf_counter())
                    if ack and ack[0] == op and ack[1] == sequence:
                        return ack
            raise RuntimeError("no reply from model endpoint (UDP 4178)")

        begin = exchange(BEGIN, size, digest)
        code = begin[3]
        if code == 1:
            print(f"Already verified: {size:,} bytes, SHA256 {digest.hex()}")
            return
        if code != 0:
            raise RuntimeError(f"BEGIN rejected: {NAMES.get(code, code)}; "
                               "firmware must pin this exact model size/digest")
        committed = cursor = resume_at = begin[2]
        if not 0 <= committed <= size:
            raise RuntimeError("invalid receiver progress")
        print(f"Sending {size:,} bytes, SHA256 {digest.hex()}, resume={committed:,}", flush=True)
        next_send = last_progress = last_report = time.perf_counter()
        sent_bytes = 0
        while committed < size:
            now = time.perf_counter()
            if now - start > args.timeout:
                raise TimeoutError(f"upload timed out at {committed}/{size}")
            if now - last_progress > args.retry_ms / 1000:
                cursor = committed
                last_progress = now
            while cursor < size and cursor < committed + args.window * args.chunk and now >= next_send:
                count = min(args.chunk, size - cursor)
                model.seek(cursor)
                data = model.read(count)
                if len(data) != count:
                    raise RuntimeError("model changed or truncated during upload")
                send(DATA, cursor, data)
                cursor += count
                sent_bytes += count
                next_send = now + args.pace_ms / 1000
                now = time.perf_counter()
            ack = receive(min(0.01, max(0.0001, next_send - now)))
            if ack:
                _, _, offset, code = ack
                if code not in (0, 1, 4, 5):
                    raise RuntimeError(f"DATA rejected: {NAMES.get(code, code)}")
                if committed < offset <= size:
                    committed = offset
                    cursor = max(cursor, committed)
                    last_progress = time.perf_counter()
            now = time.perf_counter()
            if now - last_report >= 5:
                rate = (committed - resume_at) / max(now - start, 0.001) / 1048576
                print(f"{committed * 100 / size:5.1f}% {committed:,}/{size:,}, {rate:.2f} MiB/s", flush=True)
                last_report = now
        final = exchange(FINISH, size)
        if final[3] != 1:
            raise RuntimeError(f"final SHA256 rejected: {NAMES.get(final[3], final[3])}")
        elapsed = time.perf_counter() - start
        print(f"Verified READY: {size:,} total bytes, {size - resume_at:,} new bytes in {elapsed:.1f}s "
              f"({(size - resume_at) / elapsed / 1048576:.2f} MiB/s; transmitted {sent_bytes:,} bytes)")


if __name__ == "__main__":
    main()
