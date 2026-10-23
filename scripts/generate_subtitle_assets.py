"""Generate the prototype font and UTF-32 text assets.

The demonstration labels/samples keep their small subset; the live Chinese
caption font (28 px) is additionally extended with the GB2312 level-1 common
character set so real speech translations are shown accurately. The font path
is an operator-supplied CLI argument and is never part of the product UI or
manifest. No font license or complete Chinese font is implied by producing this
private prototype asset subset; assets are generated into output/ only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import re
import struct
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version


ROOT = Path(__file__).resolve().parents[1]
FONT_SIZES = (14, 18, 28, 36, 44)
LIVE_FONT_SIZE = 28
MAX_TEXT_CODEPOINTS = 255
ASSET_LIMIT = 3 * 1024 * 1024
REQUIRED_LABELS = {
    "brand", "language", "demo", "caption", "source", "session", "time",
    "count", "history", "input", "touch", "listening", "recognizing",
    "translating", "final", "paused", "ready", "empty", "pause", "resume",
    "start", "clear", "original", "font", "small", "normal", "large",
    "settings", "retained", "pending", "more",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _u16(data: bytes, at: int) -> int:
    return struct.unpack_from(">H", data, at)[0]


def _u32(data: bytes, at: int) -> int:
    return struct.unpack_from(">I", data, at)[0]


def read_cmap_codepoints(path: Path, font_index: int = 0) -> set[int]:
    """Read the Unicode cmap of a TTF/TTC without external font libraries."""
    data = path.read_bytes()
    if data[:4] == b"ttcf":
        if _u32(data, 4) not in (0x00010000, 0x00020000):
            raise ValueError("Unsupported TTC version")
        count = _u32(data, 8)
        if font_index >= count:
            raise ValueError("TTC font index out of range")
        base = _u32(data, 12 + 4 * font_index)
    else:
        base = 0
    num_tables = _u16(data, base + 4)
    cmap_off = None
    for i in range(num_tables):
        rec = base + 12 + 16 * i
        if data[rec:rec + 4] == b"cmap":
            cmap_off = _u32(data, rec + 8)
            break
    if cmap_off is None:
        raise ValueError("Font has no cmap table")
    codepoints: set[int] = set()
    num_sub = _u16(data, cmap_off + 2)
    for i in range(num_sub):
        rec = cmap_off + 4 + 8 * i
        platform, encoding = _u16(data, rec), _u16(data, rec + 2)
        if (platform, encoding) not in ((3, 1), (3, 10), (0, 3), (0, 4), (0, 6)):
            continue
        sub = cmap_off + _u32(data, rec + 4)
        fmt = _u16(data, sub)
        if fmt == 4:
            seg_x2 = _u16(data, sub + 6)
            seg = seg_x2 // 2
            end = sub + 14
            start = end + seg_x2 + 2
            delta = start + seg_x2
            range_offset = delta + seg_x2
            for j in range(seg):
                e = _u16(data, end + 2 * j)
                s = _u16(data, start + 2 * j)
                d = _u16(data, delta + 2 * j)
                ro = _u16(data, range_offset + 2 * j)
                if s == 0xFFFF:
                    continue
                for c in range(s, e + 1):
                    if ro == 0:
                        cp = (c + d) & 0xFFFF
                    else:
                        glyph_at = range_offset + 2 * j + ro + 2 * (c - s)
                        g = _u16(data, glyph_at) if glyph_at + 2 <= len(data) else 0
                        if not g:
                            continue
                        cp = (g + d) & 0xFFFF
                    codepoints.add(cp)
        elif fmt == 12:
            groups = _u32(data, sub + 12)
            for j in range(groups):
                rec2 = sub + 16 + 12 * j
                s = _u32(data, rec2)
                e = _u32(data, rec2 + 4)
                for c in range(s, e + 1):
                    codepoints.add(c)
    return codepoints


def gb2312_level1_codepoints() -> list[int]:
    """Common (GB2312 level-1) Han characters, 3755 codepoints."""
    chars: set[int] = set()
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                char = bytes((high, low)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            chars.add(ord(char))
    return sorted(chars)


def load_text(path: Path) -> tuple[dict[str, str], list[dict[str, str]], list[int]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("Unsupported demo schema_version")
    labels, samples = data["labels"], data["samples"]
    if not isinstance(labels, dict) or not REQUIRED_LABELS <= labels.keys():
        raise ValueError("Missing required UI labels")
    if not 4 <= len(samples) <= 6:
        raise ValueError("Expected 4..6 demonstration sentence pairs")
    texts: list[str] = []
    for key, value in labels.items():
        if not re.fullmatch(r"[a-z][a-z0-9_]*", key):
            raise ValueError(f"Invalid label key {key!r}")
        if not isinstance(value, str) or not value or "\0" in value:
            raise ValueError(f"Invalid label value {key!r}")
        texts.append(value)
    for i, item in enumerate(samples):
        for key in ("source", "translated"):
            value = item[key]
            if not isinstance(value, str) or not 1 <= len(value) <= MAX_TEXT_CODEPOINTS:
                raise ValueError(f"Sample {i} {key} must contain 1..255 codepoints")
            if "\0" in value or "\n" in value or "\r" in value:
                raise ValueError(f"Sample {i} {key} must be one UTF-32 string")
            texts.append(value)
    if max(len(item["translated"]) for item in samples) < 120:
        raise ValueError("A long Chinese sample of at least 120 codepoints is required")
    # Printable ASCII supports elapsed-time fields and any numeric UI counter.
    texts.extend(data.get("additional_texts", []))
    codepoints = set(range(32, 127)) | {ord("□")}
    codepoints.update(ord(char) for text in texts for char in text)
    if any(cp < 32 or 0xD800 <= cp <= 0xDFFF for cp in codepoints):
        raise ValueError("Unsupported control or surrogate codepoint")
    return labels, samples, sorted(codepoints)


def pack_coverage(coverage: bytes) -> bytes:
    result = bytearray((len(coverage) + 1) // 2)
    for i, value in enumerate(coverage):
        if not 0 <= value <= 15:
            raise ValueError("Coverage exceeds a 4-bit pixel")
        result[i // 2] |= value << (4 if not i & 1 else 0)
    return bytes(result)


def unpack_coverage(packed: bytes, count: int) -> bytes:
    result = bytes((packed[i // 2] >> (4 if not i & 1 else 0)) & 15 for i in range(count))
    if count & 1 and packed and packed[-1] & 15:
        raise ValueError("Odd glyph has a nonzero padding nibble")
    return result


def make_font(path: Path, size: int, codepoints: list[int]) -> tuple[list[dict], bytes, dict]:
    font = ImageFont.truetype(str(path), size=size, index=0)
    ascent, descent = font.getmetrics()
    glyphs, pixels = [], bytearray()
    ink_glyphs = 0
    for codepoint in codepoints:
        char = chr(codepoint)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        width, height = right - left, bottom - top
        advance = math.ceil(font.getlength(char))
        if width <= 0 or height <= 0:
            width = height = 0
            coverage = b""
        else:
            image = Image.new("L", (width, height))
            ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor="ls")
            coverage = bytes((value * 15 + 127) // 255 for value in image.tobytes())
            if any(coverage):
                ink_glyphs += 1
            elif char != " ":
                raise ValueError(f"Empty glyph at {size}px U+{codepoint:04X}")
        packed = pack_coverage(coverage)
        if unpack_coverage(packed, width * height) != coverage:
            raise ValueError(f"4-bit roundtrip failed at {size}px U+{codepoint:04X}")
        glyphs.append(dict(codepoint=codepoint, offset=len(pixels), width=width,
                           height=height, advance=advance, x_offset=left,
                           y_offset=size + top))
        pixels.extend(packed)
    validate_font(glyphs, bytes(pixels), codepoints)
    metrics = dict(size=size, glyph_count=len(glyphs), pixel_bytes=len(pixels),
                   glyph_record_bytes=len(glyphs) * 20, ascent=ascent, descent=descent,
                   baseline_from_line_top=size, ink_glyphs=ink_glyphs,
                   min_y_offset=min(g["y_offset"] for g in glyphs),
                   max_ink_bottom=max(g["y_offset"] + g["height"] for g in glyphs),
                   max_width=max(g["width"] for g in glyphs),
                   max_height=max(g["height"] for g in glyphs))
    return glyphs, bytes(pixels), metrics


def validate_font(glyphs: list[dict], pixels: bytes, codepoints: list[int]) -> None:
    if [g["codepoint"] for g in glyphs] != codepoints:
        raise ValueError("Glyph table must be sorted and complete")
    expected_offset = 0
    for glyph in glyphs:
        if glyph["offset"] != expected_offset:
            raise ValueError("Glyph range is not contiguous")
        for key in ("width", "height", "advance"):
            if not 0 <= glyph[key] <= 0xFFFF:
                raise ValueError(f"Glyph {key} exceeds uint16_t")
        for key in ("x_offset", "y_offset"):
            if not -32768 <= glyph[key] <= 32767:
                raise ValueError(f"Glyph {key} exceeds int16_t")
        count = glyph["width"] * glyph["height"]
        end = expected_offset + (count + 1) // 2
        if end > len(pixels):
            raise ValueError("Glyph extends past its font payload")
        unpack_coverage(pixels[expected_offset:end], count)
        expected_offset = end
    if expected_offset != len(pixels):
        raise ValueError("Unreferenced font pixel payload")


def byte_array(name: str, payload: bytes) -> str:
    lines = [f"static const uint8_t {name}[] UI_ASSET = {{"]
    lines.extend("    " + ", ".join(f"0x{value:02x}" for value in payload[i:i + 24]) + ","
                 for i in range(0, len(payload), 24))
    lines.append("};\n")
    return "\n".join(lines)


def text_array(name: str, value: str) -> str:
    codepoints = [ord(char) for char in value] + [0]
    lines = [f"static const uint32_t {name}[] UI_ASSET = {{"]
    lines.extend("    " + ", ".join(f"0x{cp:08x}u" for cp in codepoints[i:i + 12]) + ","
                 for i in range(0, len(codepoints), 12))
    lines.append("};\n")
    return "\n".join(lines)


def parse_c_array(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\[\] UI_ASSET = \{(.*?)\n\};", source, re.S)
    if not match:
        raise ValueError(f"Generated C array {name} missing")
    return match.group(1)


def validate_serialized_c(path: Path, fonts: dict[int, tuple[list[dict], bytes]],
                          labels: dict[str, str], samples: list[dict[str, str]],
                          size_codepoints: dict[int, list[int]]) -> None:
    """Parse the actual C output and independently reconstruct each bitmap range."""
    source = path.read_text(encoding="utf-8")
    fields = ("codepoint", "offset", "width", "height", "advance", "x_offset", "y_offset")
    for size, (expected_glyphs, expected_pixels) in fonts.items():
        parsed_pixels = bytes(int(value, 16) for value in
                              re.findall(r"0x([0-9a-f]{2})", parse_c_array(source, f"font_{size}_pixels")))
        records = re.findall(r"\{([^{}]+)\}", parse_c_array(source, f"font_{size}_glyphs"))
        parsed_glyphs = []
        for record in records:
            values = [int(value.strip().removesuffix("u")) for value in record.split(",")]
            if len(values) != len(fields):
                raise ValueError("Serialized glyph ABI field count mismatch")
            parsed_glyphs.append(dict(zip(fields, values)))
        if parsed_pixels != expected_pixels or parsed_glyphs != expected_glyphs:
            raise ValueError(f"Serialized {size}px font differs from validated input")
        validate_font(parsed_glyphs, parsed_pixels, size_codepoints[size])
    texts = {f"label_{key}": value for key, value in labels.items()}
    texts.update({f"sample_{i}_{key}": item[key] for i, item in enumerate(samples)
                  for key in ("source", "translated")})
    for name, value in texts.items():
        parsed = [int(cp, 16) for cp in re.findall(r"0x([0-9a-f]{8})u", parse_c_array(source, name))]
        if parsed != [ord(char) for char in value] + [0]:
            raise ValueError(f"Serialized UTF-32 text {name} differs from JSON")


def generate(font_path: Path, input_path: Path, output: Path,
             extend_size: int = LIVE_FONT_SIZE) -> dict:
    labels, samples, codepoints = load_text(input_path)
    size_codepoints = {size: codepoints for size in FONT_SIZES}
    extended_count = 0
    if extend_size:
        if extend_size not in FONT_SIZES:
            raise ValueError("Extend size must be one of FONT_SIZES")
        extended = sorted(set(codepoints) | set(gb2312_level1_codepoints()))
        if len(extended) > 0xFFFF:
            raise ValueError("Extended glyph table exceeds ui_font.count")
        size_codepoints[extend_size] = extended
        extended_count = len(extended)
    cmap = read_cmap_codepoints(font_path)
    missing = [cp for cp in sorted({cp for cps in size_codepoints.values() for cp in cps}) if cp not in cmap]
    if missing:
        raise ValueError("Missing glyphs: " + ", ".join(f"U+{cp:04X}" for cp in missing[:16]))
    output.mkdir(parents=True, exist_ok=True)
    chunks = [
        "/* Generated by scripts/generate_subtitle_assets.py; modify the JSON source. */",
        '#include "ui_assets.h"',
        '#define UI_ASSET __attribute__((section(".ui_assets"), used, aligned(4)))\n',
        "_Static_assert(sizeof(struct ui_glyph) == 20, \"ui_glyph ABI\");\n",
        "#if UINTPTR_MAX == UINT32_MAX",
        "_Static_assert(sizeof(struct ui_font) == 12, \"ARM ui_font ABI\");",
        "_Static_assert(sizeof(struct ui_label) == 8, \"ARM ui_label ABI\");",
        "_Static_assert(sizeof(struct ui_demo_text) == 8, \"ARM ui_demo_text ABI\");",
        "#endif\n",
    ]
    metrics = []
    font_payloads = {}
    preview_font = None
    for size in FONT_SIZES:
        glyphs, pixels, font_metrics = make_font(font_path, size, size_codepoints[size])
        font_payloads[size] = (glyphs, pixels)
        metrics.append(font_metrics)
        chunks.append(byte_array(f"font_{size}_pixels", pixels))
        chunks.append(f"static const struct ui_glyph font_{size}_glyphs[] UI_ASSET = {{")
        chunks.extend("    {%uu, %uu, %u, %u, %u, %d, %d}," %
                      tuple(glyph[k] for k in ("codepoint", "offset", "width", "height",
                                               "advance", "x_offset", "y_offset")) for glyph in glyphs)
        chunks.append("};\n")
        if size == 36:
            preview_font = (glyphs, pixels)
    chunks.append("const struct ui_font ui_fonts[] UI_ASSET = {")
    chunks.extend(f"    {{{size}, {len(size_codepoints[size])}, font_{size}_glyphs, font_{size}_pixels}},"
                  for size in FONT_SIZES)
    chunks.extend(["};", f"const uint32_t ui_font_count UI_ASSET = {len(FONT_SIZES)};\n"])
    for key, value in labels.items():
        chunks.append(f'static const char key_{key}[] UI_ASSET = "{key}";')
        chunks.append(text_array(f"label_{key}", value))
    chunks.append("const struct ui_label ui_labels[] UI_ASSET = {")
    chunks.extend(f"    {{key_{key}, label_{key}}}," for key in labels)
    chunks.extend(["};", f"const uint32_t ui_label_count UI_ASSET = {len(labels)};\n"])
    for i, item in enumerate(samples):
        chunks.append(text_array(f"sample_{i}_source", item["source"]))
        chunks.append(text_array(f"sample_{i}_translated", item["translated"]))
    chunks.append("const struct ui_demo_text ui_demo_samples[] UI_ASSET = {")
    chunks.extend(f"    {{sample_{i}_source, sample_{i}_translated}}," for i in range(len(samples)))
    chunks.extend(["};", f"const uint32_t ui_demo_sample_count UI_ASSET = {len(samples)};\n"])
    generated = output / "ui_assets.c"
    generated.write_text("\n".join(chunks), encoding="utf-8", newline="\n")
    validate_serialized_c(generated, font_payloads, labels, samples, size_codepoints)
    # Include 4-byte alignment padding for every asset object in this estimate.
    pixel_bytes = sum(item["pixel_bytes"] for item in metrics)
    glyph_bytes = sum(item["glyph_record_bytes"] for item in metrics)
    text_bytes = sum((len(value) + 1) * 4 for value in labels.values())
    text_bytes += sum((len(item[key]) + 1) * 4 for item in samples for key in ("source", "translated"))
    key_bytes = sum(len(key) + 1 for key in labels)
    table_bytes = len(FONT_SIZES) * 12 + len(labels) * 8 + len(samples) * 8 + 12
    padding_bound = 3 * (2 * len(FONT_SIZES) + 2 * len(labels) + 2 * len(samples) + 6)
    payload_bound = pixel_bytes + glyph_bytes + text_bytes + key_bytes + table_bytes + padding_bound
    if payload_bound > ASSET_LIMIT:
        raise ValueError("Asset payload exceeds the reserved 3 MiB ARM TLS DDR region")
    preview = output / "font_preview.png"
    make_preview(preview_font, preview)
    manifest = dict(
        schema_version=1,
        generated_utc=datetime.now(timezone.utc).isoformat(),
        status="passed",
        scope="Demonstration subset plus GB2312 level-1 live-caption font; no live audio or ASR proof.",
        input_sha256=digest(input_path), font_sha256=digest(font_path),
        generator_sha256=digest(Path(__file__)),
        header_sha256=digest(ROOT / "software/subtitle_ui/ui_assets.h"),
        font_file_name=font_path.name,
        python_version=platform.python_version(), pillow_version=pillow_version,
        missing_codepoints=[], codepoint_count=len(codepoints),
        codepoints=[f"U+{cp:04X}" for cp in codepoints],
        extended_font_size=extend_size,
        extended_codepoint_count=extended_count,
        per_size_codepoint_count={str(size): len(size_codepoints[size]) for size in FONT_SIZES},
        pixel_encoding="row-major 4-bit coverage, high nibble first, odd final low nibble zero",
        metrics=metrics, label_count=len(labels), sample_count=len(samples),
        sample_lengths=[dict(source=len(item["source"]), translated=len(item["translated"])) for item in samples],
        pixel_bytes=pixel_bytes, glyph_record_bytes=glyph_bytes, text_bytes=text_bytes,
        arm32_payload_upper_bound=payload_bound, reserved_bytes=ASSET_LIMIT,
        checks=dict(font_cmap_coverage=True, glyph_ranges=True, bitmap_roundtrip=True,
                    sorted_codepoints=True, zero_padding_nibbles=True, text_limits=True,
                    arm32_size_bound=True, serialized_c_roundtrip=True),
        outputs={"ui_assets.c": digest(generated), "font_preview.png": digest(preview)},
    )
    (output / "assets_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                                                encoding="utf-8", newline="\n")
    return manifest


def make_preview(font_data: tuple[list[dict], bytes] | None, path: Path) -> None:
    """Preview decodes packed assets rather than re-rendering the source font."""
    if font_data is None:
        raise ValueError("Preview font missing")
    glyphs, pixels = font_data
    lookup = {g["codepoint"]: g for g in glyphs}
    image = Image.new("RGB", (1000, 220), "#f4f7fa")
    lines = ["译声  中文 → 中文  模拟演示", "请确认每个人都能清楚地听到发言。", "09:30  长句自动分页  触摸待接入"]
    for row, text in enumerate(lines):
        x, y = 24, 16 + row * 66
        for char in text:
            glyph = lookup.get(ord(char))
            if glyph is None:
                x += 14
                continue
            count = glyph["width"] * glyph["height"]
            if count:
                end = glyph["offset"] + (count + 1) // 2
                coverage = unpack_coverage(pixels[glyph["offset"]:end], count)
                alpha = Image.frombytes("L", (glyph["width"], glyph["height"]), bytes(v * 17 for v in coverage))
                image.paste("#162b3b", (x + glyph["x_offset"], y + glyph["y_offset"]), alpha)
            x += glyph["advance"]
    image.save(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--input", type=Path, default=ROOT / "assets/subtitle_demo.json")
    parser.add_argument("--output", type=Path, default=ROOT / "output/subtitle_ui")
    parser.add_argument("--extend-size", type=int, default=LIVE_FONT_SIZE,
                        help="font size extended with GB2312 level-1 characters (0 disables)")
    args = parser.parse_args()
    manifest = generate(args.font, args.input, args.output, args.extend_size)
    print(json.dumps({"status": manifest["status"], "codepoints": manifest["codepoint_count"],
                      "extended_font_size": manifest["extended_font_size"],
                      "extended_codepoint_count": manifest["extended_codepoint_count"],
                      "pixel_bytes": manifest["pixel_bytes"],
                      "arm32_payload_upper_bound": manifest["arm32_payload_upper_bound"],
                      "sample_lengths": manifest["sample_lengths"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
