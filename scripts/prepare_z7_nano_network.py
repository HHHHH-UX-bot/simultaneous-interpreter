"""Add public/private network mailboxes to the accepted UART/touch layout."""
import json
from run_z7_nano import ROOT, OUT, sha


def main():
    for name in ("protocol_tests.json", "runtime_tests.json", "client_tests.json"):
        record = json.loads((ROOT/"output/network_ui"/name).read_text(encoding="utf-8"))
        if record["Status"] != "passed" or any(sha(ROOT/p) != h for p, h in record["SourceHashes"].items()):
            raise RuntimeError("Current shared network validation required: "+name)
    folder = OUT/"wifi"
    linker = (folder/"ui_at.ld").read_text(encoding="utf-8")
    anchor = "    .ui_assets :"
    if linker.count(anchor) != 1:
        raise RuntimeError("UART/touch linker anchor changed")
    sections = ('    .network_mailbox 0x0001E140 (NOLOAD) : { KEEP(*(.network_mailbox)) } > OCM\n'
                '    ASSERT(SIZEOF(.network_mailbox) == 0x40, "Network public ABI changed")\n'
                '    .network_config 0x0001E180 (NOLOAD) : { KEEP(*(.network_config)) } > OCM\n'
                '    ASSERT(SIZEOF(.network_config) == 0x80, "Network private ABI changed")\n')
    (folder/"ui_network.ld").write_text(linker.replace(anchor, sections+anchor), encoding="utf-8")
    print("PASS_Z7_NANO_NETWORK_PREPARE")


if __name__ == "__main__":
    main()
