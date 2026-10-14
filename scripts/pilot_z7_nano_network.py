"""Known LAN subtitles through the new-board UART; no model inference here."""
import argparse
import json
import socket
import uuid
from datetime import datetime, timezone
from pathlib import Path
from network_demo_client import Peer, SOURCE, TRANSLATION, SECOND, LAST, model_text, model_history, history_entry
import inspect_z7_nano_network as observer
import run_z7_nano_network as network
import run_z7_nano_display as display


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vivado", type=Path, default=display.base.VIVADO)
    parser.add_argument("--xsdb", type=Path, default=display.base.XSDB)
    parser.add_argument("--native-gcc-directory", type=Path, default=Path("D:/cenvironment/mingw64/bin"))
    args = parser.parse_args()
    network.configure()
    folder = display.OUT/"pilots"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    events = []
    expected_history = []
    record = {"status": "pending", "attempt": folder.relative_to(display.ROOT).as_posix(),
              "scope": "Known simulated LAN text, ACKs, model and exact pixels; no audio, ASR or MT"}
    with display.base.hardware_lock():
        try:
            before = observer.sample(args, folder/"before")
            connection = json.loads((display.OUT/"connection.json").read_text(encoding="utf-8"))
            if connection["status"]!="passed" or connection["program_attempt"]!=before["program_attempt"] or \
               connection["elf_sha256"]!=before["elf_sha256"] or before["NetworkResult"]["Phase"]!=3:
                raise RuntimeError("Current new-board association and application required")
            address = connection["station_address"]
            nonce = uuid.uuid4().hex[:12]
            unique_source = "Network reconnect "+nonce
            with socket.create_connection((address,8266), timeout=8) as stream:
                stream.settimeout(8)
                peer = Peer(stream, events)
                hello = peer.send(0)
                if hello[7]!=0:
                    raise RuntimeError("Fresh network session required")
                peer.final(1, SOURCE, TRANSLATION)
                expected_history.append(history_entry(peer.epoch,1,SOURCE,TRANSLATION))
                peer.send(3, 2, 1, 0, "\U0001f600", code=4)
                peer.send(3, 2, 1, 0, "Network test", code=5, epoch=peer.epoch-1)
                peer.send(0, code=7, request=peer.request)
                peer.final(2, "Network test "+nonce, SECOND)
                expected_history.append(history_entry(peer.epoch,2,"Network test "+nonce,SECOND))
            released = observer.sample(args, folder/"released")
            if released["NetworkResult"]["Phase"]!=3 or released["NetworkResult"]["Client"]!=0xffffffff:
                raise RuntimeError("TCP disconnect did not release the client")
            with socket.create_connection((address,8266), timeout=8) as stream:
                stream.settimeout(8)
                peer = Peer(stream, events, session=2)
                hello = peer.send(0)
                if hello[6:8]!=(2,2):
                    raise RuntimeError("Reconnect lost the accepted finals")
                peer.send(1,2)
                final = peer.final(1, unique_source, LAST)
                if final[6:8]!=(3,3):
                    raise RuntimeError("Final count/history mismatch")
                expected_history.append(history_entry(peer.epoch,1,unique_source,LAST))
            after = observer.sample(args, folder/"after")
            public = after["NetworkResult"]
            if public["Phase"]!=3 or public["Error"] or public["Overflow"] or public["Framing"] or public["Rejected"]!=3:
                raise RuntimeError("Final network counters failed")
            args.action = "capture"
            display.execute_app(args, folder/"frame")
            capture = json.loads((display.OUT/"capture.json").read_text(encoding="utf-8"))
            snapshot = (display.ROOT/capture["attempt"]/"capture/snapshot.bin").read_bytes()
            if model_text(snapshot,20)!=unique_source or model_text(snapshot,1044)!=LAST or model_history(snapshot)!=expected_history:
                raise RuntimeError("Captured board model differs from sent network finals")
            record.update(status="passed", program_attempt=before["program_attempt"], elf_sha256=before["elf_sha256"],
                station_address=address, unique_source=unique_source, final_translation=LAST, finals=3,
                public=public, capture=capture, expected_history=expected_history, screen_acceptance="pending", requests=events)
        except (Exception, KeyboardInterrupt) as error:
            record.update(status="failed", error=str(error), requests=events)
        finally:
            display.save(folder/"result.json", record)
            display.save(display.OUT/"pilot.json", record)
    if record["status"]!="passed":
        raise SystemExit("New-board LAN pilot stopped: "+record["error"])
    print("PASS_Z7_NANO_NETWORK_PILOT finals=3 pixels=614400")


if __name__ == "__main__":
    main()
