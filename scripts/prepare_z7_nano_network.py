"""Add public/private network mailboxes to the accepted UART/touch layout."""
import json
from run_z7_nano import ROOT, OUT, sha


def main():
    tls = json.loads((OUT/"tls_boot/host_validation.json").read_text(encoding="utf-8"))
    if tls["status"] != "passed" or any(sha(ROOT/p)!=h for p,h in tls["input_hashes"].items()):
        raise RuntimeError("Current ARM TLS bootstrap validation required")
    for name in ("protocol_tests.json", "runtime_tests.json", "client_tests.json"):
        record = json.loads((ROOT/"output/network_ui"/name).read_text(encoding="utf-8"))
        if record["Status"] != "passed" or any(sha(ROOT/p) != h for p, h in record["SourceHashes"].items()):
            raise RuntimeError("Current shared network validation required: "+name)
    caps = json.loads((ROOT/"output/z7_nano/capabilities/host_validation.json").read_text(encoding="utf-8"))
    if caps["status"] != "passed" or any(sha(ROOT/p) != h for p,h in caps["source_hashes"].items()):
        raise RuntimeError("Current capability query validation required")
    stream = json.loads((ROOT/"output/z7_nano/stream/host_validation.json").read_text(encoding="utf-8"))
    if stream["status"] != "passed" or any(sha(ROOT/p) != h for p,h in stream["input_hashes"].items()):
        raise RuntimeError("Current stream sender validation required")
    for name in ("cloud_events/validation.json", "cloud_live/host_validation.json"):
        record=json.loads((OUT/name).read_text(encoding="utf-8"))
        if record["status"]!="passed" or any(sha(ROOT/p)!=h for p,h in record["input_hashes"].items()):
            raise RuntimeError("Current cloud consumer validation required: "+name)
    folder = OUT/"wifi"
    linker = (folder/"ui_at.ld").read_text(encoding="utf-8")
    anchor = "    .ui_assets :"
    if linker.count(anchor) != 1:
        raise RuntimeError("UART/touch linker anchor changed")
    sections = ('    .network_mailbox 0x0001E140 (NOLOAD) : { KEEP(*(.network_mailbox)) } > OCM\n'
                '    ASSERT(SIZEOF(.network_mailbox) == 0x40, "Network public ABI changed")\n'
                '    .network_config 0x0001E180 (NOLOAD) : { KEEP(*(.network_config)) } > OCM\n'
                '    ASSERT(SIZEOF(.network_config) == 0x80, "Network private ABI changed")\n')
    sections += ('    .network_capabilities 0x0001E200 (NOLOAD) : { KEEP(*(.network_capabilities)) } > OCM\n'
                 '    ASSERT(SIZEOF(.network_capabilities) == 0x40, "Capability public ABI changed")\n')
    sections += ('    .network_upload_control 0x0001E240 (NOLOAD) : { KEEP(*(.network_upload_control)) } > OCM\n'
                 '    ASSERT(SIZEOF(.network_upload_control) == 4, "Upload control ABI changed")\n')
    sections += ('    .stream_pilot 0x0001E280 (NOLOAD) : { KEEP(*(.stream_pilot)) } > OCM\n'
                 '    ASSERT(SIZEOF(.stream_pilot) == 0x40, "Stream pilot ABI changed")\n')
    sections += ('    .cloud_mailbox 0x0001E2C0 (NOLOAD) : { KEEP(*(.cloud_mailbox)) } > OCM\n'
                 '    ASSERT(SIZEOF(.cloud_mailbox) == 0x40, "Cloud public ABI changed")\n')
    linker=linker.replace('    .text :',
        '    .cloud_text 0x01980000 : { *(.text.cloud_* .text.jsmn_* .text.wslay_* .text.network_tick .rodata.cloud_*) } > FONT_DDR\n'
        '    ASSERT(SIZEOF(.cloud_text) <= 0x80000, "Cloud code exceeds DDR reserve")\n'
        '    .text :')
    linker=linker.replace(anchor,sections+'    .ui_assets 0x01800000 :')
    linker=linker.replace('ASSERT(SIZEOF(.ui_assets) <= 0x200000', 'ASSERT(SIZEOF(.ui_assets) <= 0x180000')
    linker=linker.replace('    __stack_top =',
        '    ASSERT(__ui_runtime_end <= 0x01B08000, "Snapshots overlap cloud runtime")\n'
        '    .cloud_runtime 0x01B08000 (NOLOAD) : { *(.cloud_runtime) } > SNAPSHOT_DDR\n'
        '    ASSERT(SIZEOF(.cloud_runtime) <= 0x6000, "Cloud runtime exceeds DDR reserve")\n'
        '    .cloud_final 0x01B0E000 (NOLOAD) : { KEEP(*(.cloud_final)) } > SNAPSHOT_DDR\n'
        '    ASSERT(SIZEOF(.cloud_final) == 2052, "Cloud caption ABI changed")\n'
        '    .cloud_masks 0x01B0FF00 (NOLOAD) : { KEEP(*(.cloud_masks)) } > SNAPSHOT_DDR\n'
        '    ASSERT(SIZEOF(.cloud_masks) == 128, "Cloud control mask pool changed")\n'
        '    __stack_top =')
    (folder/"ui_network.ld").write_text(linker, encoding="utf-8")
    print("PASS_Z7_NANO_NETWORK_PREPARE")


if __name__ == "__main__":
    main()
