"""Compare the new board's captured frame with the unchanged native renderer."""
import ctypes
import hashlib
import json
import os
import struct
from PIL import Image
from run_z7_nano import ROOT, sha
from validate_subtitle_ui import Stats


def compare(folder, app, compiler_directory, host_record=None):
    host = json.loads((host_record or ROOT/"output/subtitle_ui/host_validation.json").read_text(encoding="utf-8"))
    if host["Status"] != "passed" or any(sha(ROOT/p) != h for p, h in host["SourceHashes"].items()):
        raise RuntimeError("Current shared native renderer validation required")
    library_path = ROOT/host["NativeLibrary"]
    if sha(library_path) != host["NativeLibrarySHA256"]:
        raise RuntimeError("Native renderer library changed")
    snapshot = (folder/"snapshot.bin").read_bytes()
    raw = (folder/"framebuffer.bin").read_bytes()
    header = struct.unpack_from("<8I", snapshot)
    if len(raw) != 614400*4 or len(snapshot) != 11448 or header[0] != 0x55495331 or header[1] not in (1,2) or header[2:4] != (11448,11384):
        raise RuntimeError("Captured frame/snapshot size or identity changed")
    # DLL dependencies use the same compiler directory as the original native checks.
    with os.add_dll_directory(str(compiler_directory)):
        library = ctypes.CDLL(str(library_path))
        library.ui_host_from_state.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Stats)]
        library.ui_host_from_state.restype = ctypes.c_int
        state = (ctypes.c_uint32*(11384//4)).from_buffer_copy(snapshot[32:32+11384])
        pixels = (ctypes.c_uint32*614400)()
        stats = Stats()
        if not library.ui_host_from_state(state, 11384, header[4], header[5], pixels, ctypes.byref(stats)):
            raise RuntimeError("Native renderer rejected captured state")
        if header[1]==2:
            library.ui_host_latency.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_uint32,ctypes.POINTER(Stats)]
            library.ui_host_latency(pixels,header[7],ctypes.byref(stats))
        expected = bytes(pixels)
    native_stats = [getattr(stats, key) for key, _ in stats._fields_]
    board_stats = list(struct.unpack_from("<8I", snapshot, 32+11384))
    mismatches = sum(a != b for a, b in zip(struct.unpack("<614400I", raw), struct.unpack("<614400I", expected)))
    Image.frombytes("RGB", (1024, 600), raw, "raw", "BGRX").save(folder/"screen.png")
    result = {"status": "passed" if not mismatches and board_stats == native_stats else "failed",
        "pixels_compared": 614400, "pixel_mismatches": mismatches,
        "board_pixel_sha256": sha(folder/"framebuffer.bin"),
        "native_pixel_sha256": hashlib.sha256(expected).hexdigest(),
        "snapshot_sha256": sha(folder/"snapshot.bin"), "elf_sha256": app["elf_sha256"],
        "board_glyph_stats": board_stats, "native_glyph_stats": native_stats,
        "native_library_sha256": host["NativeLibrarySHA256"],
        "scope": "Captured DDR pixels versus native rendering; physical HDMI acceptance is separate"}
    (folder/"pixel_comparison.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    if result["status"] != "passed" or stats.missing_glyphs or stats.clipped_glyph_pixels:
        raise RuntimeError("Board/native frame comparison failed")
    return result
