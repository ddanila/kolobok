#!/usr/bin/env python3
"""Build deterministic indexed VGA assets and KOLOBOK.DAT."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
TILE = 16
TRANSPARENT = 0

# VGA-friendly RGB palette. The file stores 6-bit DAC values.
COLORS = [
    (0, 0, 0), (15, 21, 44), (73, 176, 211), (35, 117, 151),
    (229, 242, 218), (24, 70, 42), (45, 119, 52), (112, 178, 65),
    (55, 35, 28), (100, 61, 35), (153, 102, 55), (205, 174, 75),
    (163, 91, 25), (225, 157, 35), (255, 211, 73), (255, 246, 204),
    (114, 25, 31), (190, 38, 45), (244, 76, 63), (218, 92, 37),
    (240, 195, 119), (124, 72, 38), (99, 105, 112), (181, 188, 177),
    (23, 24, 25), (145, 50, 31), (181, 123, 65), (39, 89, 154),
    (88, 205, 212), (91, 52, 116), (228, 130, 146), (246, 218, 92),
]
COLORS += [(0, 0, 0)] * (256 - len(COLORS))


def pal_image(size: tuple[int, int]) -> Image.Image:
    image = Image.new("P", size, TRANSPARENT)
    flat = [component for rgb in COLORS for component in rgb]
    image.putpalette(flat)
    return image


def draw_tiles() -> Image.Image:
    sheet = pal_image((6 * TILE, TILE))
    d = ImageDraw.Draw(sheet)
    # 0: air remains transparent.
    x = TILE
    d.rectangle((x, 0, x + 15, 15), fill=9)
    d.rectangle((x, 0, x + 15, 3), fill=7)
    d.rectangle((x, 4, x + 15, 6), fill=6)
    for px, py in ((2, 9), (8, 7), (13, 12)):
        d.rectangle((x + px, py, x + px + 2, py + 1), fill=10)
    x = 2 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=9)
    for px, py in ((1, 2), (9, 1), (5, 8), (12, 11), (2, 14)):
        d.rectangle((x + px, py, x + px + 2, py + 1), fill=8)
    x = 3 * TILE
    for i in range(4):
        bx = x + i * 4
        d.polygon((bx, 15, bx + 2, 5, bx + 4, 15), fill=23)
        d.line((bx + 2, 7, bx + 2, 14), fill=22)
    x = 4 * TILE
    d.rectangle((x, 3, x + 15, 8), fill=21)
    d.rectangle((x, 1, x + 15, 3), fill=7)
    d.rectangle((x, 8, x + 15, 10), fill=8)
    for px in (2, 8, 13):
        d.point((x + px, 5), fill=11)
    x = 5 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=5)
    d.ellipse((x + 2, 2, x + 13, 13), fill=6)
    d.rectangle((x + 6, 0, x + 8, 15), fill=21)
    return sheet


def draw_kolobok(d: ImageDraw.ImageDraw, ox: int, frame: int) -> None:
    d.ellipse((ox + 1, 1, ox + 14, 14), fill=1)
    d.ellipse((ox + 2, 2, ox + 13, 13), fill=13)
    d.ellipse((ox + 4, 3, ox + 10, 6), fill=14)
    eye_y = 7 + (frame == 2)
    d.point((ox + 5, eye_y), fill=1)
    d.point((ox + 10, eye_y), fill=1)
    d.line((ox + 6, 11, ox + 9, 11), fill=16)
    if frame == 0:
        d.point((ox + 2, 12), fill=12); d.point((ox + 13, 11), fill=12)
    elif frame == 1:
        d.point((ox + 3, 14), fill=12); d.point((ox + 12, 13), fill=12)
    else:
        d.point((ox + 2, 10), fill=12); d.point((ox + 13, 13), fill=12)


def draw_sprites() -> Image.Image:
    sheet = pal_image((8 * TILE, TILE))
    d = ImageDraw.Draw(sheet)
    for frame in range(3):
        draw_kolobok(d, frame * TILE, frame)
    x = 3 * TILE
    d.ellipse((x + 3, 4, x + 12, 13), fill=16)
    d.ellipse((x + 4, 4, x + 11, 12), fill=17)
    d.rectangle((x + 7, 1, x + 8, 5), fill=5)
    d.line((x + 8, 2, x + 11, 1), fill=6)
    d.point((x + 5, 6), fill=18)
    x = 4 * TILE
    d.ellipse((x + 2, 5, x + 13, 14), fill=23)
    d.ellipse((x + 4, 3, x + 11, 10), fill=23)
    d.polygon((x + 5, 5, x + 5, 0, x + 8, 4), fill=23)
    d.polygon((x + 9, 4, x + 11, 0, x + 11, 6), fill=23)
    d.point((x + 7, 6), fill=1); d.point((x + 10, 6), fill=1)
    d.rectangle((x + 11, 8, x + 14, 9), fill=15)
    d.line((x + 3, 14, x + 12, 14), fill=1)
    x = 5 * TILE
    d.ellipse((x + 2, 6, x + 12, 14), fill=19)
    d.polygon((x + 4, 8, x + 3, 2, x + 8, 6), fill=19)
    d.polygon((x + 9, 6, x + 11, 1, x + 13, 8), fill=19)
    d.polygon((x + 11, 9, x + 15, 11, x + 11, 12), fill=20)
    d.point((x + 9, 7), fill=1)
    d.line((x + 2, 14, x + 11, 14), fill=1)
    x = 6 * TILE
    d.rectangle((x + 7, 3, x + 8, 15), fill=21)
    d.rectangle((x + 2, 2, x + 13, 9), fill=17)
    d.polygon((x + 10, 2, x + 14, 5, x + 10, 8), fill=18)
    d.rectangle((x + 4, 4, x + 9, 6), fill=15)
    x = 7 * TILE
    for px, py in ((8, 1), (3, 5), (12, 7), (7, 12)):
        d.line((x + px - 1, py, x + px + 1, py), fill=31)
        d.line((x + px, py - 1, x + px, py + 1), fill=31)
    return sheet


def load_level(path: Path) -> tuple[dict, bytearray]:
    data = json.loads(path.read_text())
    width, height = data["width"], data["height"]
    assert width == 80 and height == 11
    cells = bytearray(width * height)

    def put(x: int, y: int, tile: int) -> None:
        assert 0 <= x < width and 0 <= y < height
        cells[y * width + x] = tile

    ground_y = data["ground_y"]
    pits = {x for first, last in data["pits"] for x in range(first, last + 1)}
    for x in range(width):
        if x in pits:
            put(x, ground_y + 1, 3)
        else:
            put(x, ground_y, 1)
            put(x, ground_y + 1, 2)
    for group in data["platforms"]:
        for first, last in group["ranges"]:
            for x in range(first, last + 1):
                put(x, group["y"], 4)
    return data, cells


def validate(level: dict, cells: bytearray) -> None:
    width, height = level["width"], level["height"]
    assert level["ground_y"] + 1 < height
    assert len(level["berries"]) == 8
    assert len({tuple(p) for p in level["berries"]}) == 8
    for x, y in level["berries"]:
        assert 0 <= x < width and 0 <= y < height
        assert cells[y * width + x] == 0, f"berry at {x},{y} is inside terrain"
    assert level["home"][0] < 5
    assert max(last - first + 1 for first, last in level["pits"]) <= 2
    assert any(group["y"] <= 5 for group in level["platforms"])
    assert any(group["y"] >= 8 for group in level["platforms"])
    assert {group["y"] for group in level["platforms"]} >= {5, 6, 7, 8}
    for enemy in level["enemies"]:
        assert enemy["type"] in {"hare", "fox"}
        assert 0 <= enemy["min_x"] <= enemy["x"] <= enemy["max_x"] < width
        assert 0 <= enemy["y"] < height
        assert cells[(enemy["y"] + 1) * width + enemy["x"]] in {1, 4}


def write_pack(out: Path, tiles: Image.Image, sprites: Image.Image, level: dict, cells: bytearray) -> None:
    payload = bytearray(b"KOLODAT1")
    payload.extend(struct.pack("<9H", 1, level["width"], level["height"], 6, 8,
                               len(level["berries"]), len(level["enemies"]), TILE, TILE))
    for r, g, b in COLORS:
        payload.extend((r >> 2, g >> 2, b >> 2))
    for index in range(6):
        payload.extend(tiles.crop((index * TILE, 0, (index + 1) * TILE, TILE)).tobytes())
    for index in range(8):
        payload.extend(sprites.crop((index * TILE, 0, (index + 1) * TILE, TILE)).tobytes())
    payload.extend(cells)
    for x, y in level["berries"]:
        payload.extend(struct.pack("<HH", x * TILE + 4, y * TILE + 2))
    for enemy in level["enemies"]:
        kind = 0 if enemy["type"] == "hare" else 1
        payload.extend(struct.pack("<BHHHH", kind, enemy["x"] * TILE,
                                   enemy["y"] * TILE, enemy["min_x"] * TILE,
                                   enemy["max_x"] * TILE))
    for key in ("checkpoint", "home"):
        x, y = level[key]
        payload.extend(struct.pack("<HH", x * TILE, y * TILE))
    payload.extend(struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF))
    assert len(payload) < 48 * 1024, "asset pack exceeds the MVP memory budget"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "KOLOBOK.DAT")
    args = parser.parse_args()
    level, cells = load_level(ROOT / "assets" / "level.json")
    validate(level, cells)
    tiles, sprites = draw_tiles(), draw_sprites()
    if args.check:
        print("assets: PASS (8 berries, loop platforms, indexed palette)")
        return
    source_dir = ROOT / "assets" / "generated"
    source_dir.mkdir(parents=True, exist_ok=True)
    tiles.save(source_dir / "tiles.png", optimize=False)
    sprites.save(source_dir / "sprites.png", optimize=False)
    palette = pal_image((256, 1))
    palette.putdata(range(256))
    palette.save(source_dir / "palette.png", optimize=False)
    write_pack(args.out, tiles, sprites, level, cells)
    print(f"assets: wrote {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
