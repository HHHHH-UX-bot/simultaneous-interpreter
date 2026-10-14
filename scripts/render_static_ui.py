"""Render an explicitly static mock UI and encode palette/tile ROM assets."""
import hashlib
import json
import math
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output" / "static_ui"
OUT.mkdir(parents=True, exist_ok=True)
HTML = ROOT / "output" / "ui" / "index.html"
manifest = OUT / "asset_manifest.json"
manifest.write_text('{"status":"generating"}\n', encoding="utf-8")
html_hash = hashlib.sha256(HTML.read_bytes()).hexdigest()

with sync_playwright() as pw:
    browser = pw.chromium.launch(channel="msedge", headless=True)
    page = browser.new_page(viewport={"width": 1120, "height": 900}, device_scale_factor=1)
    faults = []
    page.on("pageerror", lambda error: faults.append(str(error)))
    page.goto(HTML.as_uri())
    page.add_style_tag(content=".device{border-radius:0!important;box-shadow:none!important}")
    page.locator("#startButton").click()
    expect(page.locator("#sentenceCount")).to_have_text("2", timeout=12000)
    page.locator("#startButton").click()
    # Snapshot overrides only; the interactive HTML source is kept unchanged.
    page.evaluate("""() => {
      document.querySelector('#statusText').textContent = '静态演示';
      document.querySelector('.demo-tag').textContent = '模拟文案';
      document.querySelector('#phaseTag').textContent = '演示译文';
      document.querySelector('#sourceState').textContent = '示例原文';
      document.querySelector('#messageText').textContent = '当前为静态展示，触摸与音频功能尚未接入。';
      document.querySelector('.input-caption').textContent = '设备状态';
      document.querySelector('#inputStatus').textContent = '音频 / 触摸未接入';
      document.querySelector('#startLabel').textContent = '开始翻译';
      document.querySelector('#startIcon').setAttribute('href','#i-mic');
      document.querySelector('#elapsed').textContent = '00:07';
      document.querySelectorAll('.history-item time').forEach((e,i) => e.textContent = i === 0 ? '00:07' : '00:03');
      document.querySelector('#toast').hidden = true;
    }""")
    page.locator("#device").screenshot(path=str(OUT / "ui_source.png"))
    if faults:
        raise RuntimeError(faults)
    browser.close()

assert hashlib.sha256(HTML.read_bytes()).hexdigest() == html_hash, "HTML changed during rendering"
source = Image.open(OUT / "ui_source.png").convert("RGB")
assert source.size == (1024, 600)
# Keep the original action/text/surface colors instead of allowing an adaptive
# palette to merge the small teal button into dark text. Remaining colors cover
# antialiasing. Duplicate padding colors are normalized back to index zero.
fixed_palette = [
    (255,255,255), (237,243,245), (228,236,239), (226,241,238),
    (215,226,229), (23,42,56), (82,101,117), (0,108,104),
    (58,75,88), (124,138,147), (170,181,188), (205,214,218),
    (48,136,132), (106,169,165), (179,211,210), (248,250,251),
]
palette_image = Image.new("P", (16, 1))
palette_data = [channel for color in fixed_palette for channel in color]
palette_image.putpalette(palette_data + [255,255,255] * 240)
palette_image.putdata(range(16))
quantized = source.quantize(palette=palette_image, dither=Image.Dither.NONE)
normalized = bytes(index if index < 16 else 0 for index in quantized.tobytes())
indexed = Image.frombytes("P", source.size, normalized)
indexed.putpalette(palette_data + [255,255,255] * 240)
indexed.save(OUT / "ui_quantized.png")
indices = indexed.tobytes()
palette_bytes = indexed.getpalette()[:48]
palette = [tuple(palette_bytes[i:i + 3]) for i in range(0, 48, 3)]
assert len(palette) == 16 and len(indices) == 1024 * 600

tiles = []
tile_ids = {}
tile_map = []
for row in range(75):
    for column in range(128):
        tile = b"".join(indices[(row * 8 + dy) * 1024 + column * 8:
                                (row * 8 + dy) * 1024 + column * 8 + 8] for dy in range(8))
        if tile not in tile_ids:
            tile_ids[tile] = len(tiles)
            tiles.append(tile)
        tile_map.append(tile_ids[tile])

assert len(tiles) <= 4096, "12-bit tile index exhausted"
estimated_bram36 = math.ceil(len(tiles) * 64 / 8192) + math.ceil(len(tile_map) / 2048)
assert estimated_bram36 <= 48, "Leave BRAM margin for implementation; optimize assets first"

# Independently decode the generated tiling and compare the complete image.
reconstructed = bytearray(len(indices))
for y in range(600):
    for x in range(1024):
        map_index = (y // 8) * 128 + (x // 8)
        reconstructed[y * 1024 + x] = tiles[tile_map[map_index]][(y % 8) * 8 + x % 8]
assert bytes(reconstructed) == indices

def mem_file(name, values, digits):
    (OUT / name).write_text("".join(format(v, "0" + str(digits) + "x") + "\n" for v in values), encoding="ascii")

mem_file("static_ui_tilemap.mem", tile_map, 3)
mem_file("static_ui_tiles.mem", b"".join(tiles), 1)
mem_file("static_ui_palette.mem", [(r << 16) | (g << 8) | b for r, g, b in palette], 6)
mem_file("static_ui_expected.mem", indices, 1)
(OUT / "static_ui_config.vh").write_text(
    "// Generated by render_static_ui.py; regenerate instead of hand editing.\n"
    + chr(96) + "define STATIC_UI_TILE_COUNT " + str(len(tiles)) + "\n"
    + chr(96) + "define STATIC_UI_TILE_PIXELS " + str(len(tiles) * 64) + "\n",
    encoding="ascii",
)
asset_names = [
    "ui_source.png", "ui_quantized.png", "static_ui_tilemap.mem",
    "static_ui_tiles.mem", "static_ui_palette.mem", "static_ui_expected.mem", "static_ui_config.vh",
]
result = {
    "status": "passed", "checked_at_utc": datetime.now(timezone.utc).isoformat(),
    "scope": "Static simulated UI; no live touch, audio or model service",
    "html_sha256": html_hash,
    "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "resolution": [1024, 600], "palette": palette, "palette_colors": 16,
    "tile_size": [8, 8], "unique_tiles": len(tiles), "map_entries": len(tile_map),
    "tile_pixels": len(tiles) * 64, "estimated_bram36": estimated_bram36,
    "palette_strategy": "Fixed UI colors and antialias shades, no dithering",
    "roundtrip_pixels_checked": len(indices), "index_sha256": hashlib.sha256(indices).hexdigest(),
    "files": {name: hashlib.sha256((OUT / name).read_bytes()).hexdigest() for name in asset_names},
}
manifest.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: result[k] for k in ("status", "unique_tiles", "tile_pixels", "estimated_bram36", "roundtrip_pixels_checked")}))
