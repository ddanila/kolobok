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
PLAYER_FRAMES = 4
SPRITE_COUNT = 9

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


def draw_kolobok() -> Image.Image:
    sprite = pal_image((TILE, TILE))
    d = ImageDraw.Draw(sprite)
    d.ellipse((1, 1, 14, 14), fill=1)
    d.ellipse((2, 2, 13, 13), fill=13)
    d.ellipse((4, 3, 10, 6), fill=14)
    d.point((5, 7), fill=1)
    d.point((10, 7), fill=1)
    d.line((6, 11, 9, 11), fill=16)
    d.point((2, 12), fill=12)
    d.point((13, 11), fill=12)
    return sprite


def draw_sprites() -> Image.Image:
    sheet = pal_image((SPRITE_COUNT * TILE, TILE))
    kolobok = draw_kolobok()
    rotations = (
        kolobok,
        kolobok.transpose(Image.Transpose.ROTATE_270),
        kolobok.transpose(Image.Transpose.ROTATE_180),
        kolobok.transpose(Image.Transpose.ROTATE_90),
    )
    for frame, sprite in enumerate(rotations):
        sheet.paste(sprite, (frame * TILE, 0))
    d = ImageDraw.Draw(sheet)
    x = 4 * TILE
    d.ellipse((x + 3, 4, x + 12, 13), fill=16)
    d.ellipse((x + 4, 4, x + 11, 12), fill=17)
    d.rectangle((x + 7, 1, x + 8, 5), fill=5)
    d.line((x + 8, 2, x + 11, 1), fill=6)
    d.point((x + 5, 6), fill=18)
    x = 5 * TILE
    d.ellipse((x + 2, 5, x + 13, 14), fill=23)
    d.ellipse((x + 4, 3, x + 11, 10), fill=23)
    d.polygon((x + 5, 5, x + 5, 0, x + 8, 4), fill=23)
    d.polygon((x + 9, 4, x + 11, 0, x + 11, 6), fill=23)
    d.point((x + 7, 6), fill=1); d.point((x + 10, 6), fill=1)
    d.rectangle((x + 11, 8, x + 14, 9), fill=15)
    d.line((x + 3, 14, x + 12, 14), fill=1)
    x = 6 * TILE
    d.ellipse((x + 2, 6, x + 12, 14), fill=19)
    d.polygon((x + 4, 8, x + 3, 2, x + 8, 6), fill=19)
    d.polygon((x + 9, 6, x + 11, 1, x + 13, 8), fill=19)
    d.polygon((x + 11, 9, x + 15, 11, x + 11, 12), fill=20)
    d.point((x + 9, 7), fill=1)
    d.line((x + 2, 14, x + 11, 14), fill=1)
    x = 7 * TILE
    d.rectangle((x + 7, 3, x + 8, 15), fill=21)
    d.rectangle((x + 2, 2, x + 13, 9), fill=17)
    d.polygon((x + 10, 2, x + 14, 5, x + 10, 8), fill=18)
    d.rectangle((x + 4, 4, x + 9, 6), fill=15)
    x = 8 * TILE
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
    home_x = level["home"][0]
    for y in range(5, level["ground_y"]):
        for x in range(max(0, home_x - 1), home_x + 4):
            assert cells[y * width + x] == 0, \
                f"platform at {x},{y} overlaps the cottage"
    assert max(last - first + 1 for first, last in level["pits"]) <= 2
    assert any(group["y"] <= 5 for group in level["platforms"])
    assert any(group["y"] >= 8 for group in level["platforms"])
    assert {group["y"] for group in level["platforms"]} >= {5, 6, 7, 8}
    for enemy in level["enemies"]:
        assert enemy["type"] in {"hare", "fox"}
        assert 0 <= enemy["min_x"] <= enemy["x"] <= enemy["max_x"] < width
        assert 0 <= enemy["y"] < height
        assert cells[(enemy["y"] + 1) * width + enemy["x"]] in {1, 4}


def encode_sprite_spans(sprite: Image.Image) -> bytes:
    pixels = sprite.tobytes()
    encoded = bytearray()
    for y in range(TILE):
        runs: list[tuple[int, bytes]] = []
        x = 0
        while x < TILE:
            while x < TILE and pixels[y * TILE + x] == TRANSPARENT:
                x += 1
            if x == TILE:
                break
            start = x
            while x < TILE and pixels[y * TILE + x] != TRANSPARENT:
                x += 1
            runs.append((start, pixels[y * TILE + start:y * TILE + x]))
        encoded.append(len(runs))
        for start, colors in runs:
            encoded.extend((start, len(colors)))
            encoded.extend(colors)
    return bytes(encoded)


def decode_sprite_spans(encoded: bytes) -> bytes:
    decoded = bytearray(TILE * TILE)
    cursor = 0
    for y in range(TILE):
        run_count = encoded[cursor]
        cursor += 1
        for _ in range(run_count):
            x, length = encoded[cursor:cursor + 2]
            cursor += 2
            assert x < TILE and length > 0 and x + length <= TILE
            decoded[y * TILE + x:y * TILE + x + length] = encoded[cursor:cursor + length]
            cursor += length
    assert cursor == len(encoded)
    return bytes(decoded)


def encode_planar_tile(tile: Image.Image) -> bytes:
    pixels = tile.tobytes()
    encoded = bytearray()
    for plane in range(4):
        for y in range(TILE):
            for x in range(plane, TILE, 4):
                encoded.append(pixels[y * TILE + x])
    return bytes(encoded)


def decode_planar_tile(encoded: bytes) -> bytes:
    assert len(encoded) == TILE * TILE
    decoded = bytearray(TILE * TILE)
    cursor = 0
    for plane in range(4):
        for y in range(TILE):
            for x in range(plane, TILE, 4):
                decoded[y * TILE + x] = encoded[cursor]
                cursor += 1
    return bytes(decoded)


def encode_planar_sprite_spans(sprite: Image.Image, alignment: int, plane: int) -> bytes:
    pixels = sprite.tobytes()
    encoded = bytearray()
    for y in range(TILE):
        samples = []
        for x in range(TILE):
            if (alignment + x) % 4 == plane and pixels[y * TILE + x] != TRANSPARENT:
                samples.append(((alignment + x) // 4, pixels[y * TILE + x]))
        runs: list[tuple[int, bytes]] = []
        cursor = 0
        while cursor < len(samples):
            start = samples[cursor][0]
            colors = bytearray((samples[cursor][1],))
            cursor += 1
            while cursor < len(samples) and samples[cursor][0] == start + len(colors):
                colors.append(samples[cursor][1])
                cursor += 1
            runs.append((start, bytes(colors)))
        encoded.append(len(runs))
        for start, colors in runs:
            encoded.extend((start, len(colors)))
            encoded.extend(colors)
    return bytes(encoded)


def decode_planar_sprite_spans(encoded: bytes, alignment: int, plane: int) -> bytes:
    decoded = bytearray(TILE * TILE)
    cursor = 0
    for y in range(TILE):
        run_count = encoded[cursor]
        cursor += 1
        for _ in range(run_count):
            start, length = encoded[cursor:cursor + 2]
            cursor += 2
            assert length > 0 and start + length <= 5
            for sample in range(length):
                x = (start + sample) * 4 + plane - alignment
                assert 0 <= x < TILE and (alignment + x) % 4 == plane
                decoded[y * TILE + x] = encoded[cursor + sample]
            cursor += length
    assert cursor == len(encoded)
    return bytes(decoded)


def validate_raster_encodings(tiles: Image.Image, sprites: Image.Image) -> None:
    for index in range(6):
        tile = tiles.crop((index * TILE, 0, (index + 1) * TILE, TILE))
        assert decode_planar_tile(encode_planar_tile(tile)) == tile.tobytes()
    for index in range(SPRITE_COUNT):
        sprite = sprites.crop((index * TILE, 0, (index + 1) * TILE, TILE))
        pixels = sprite.tobytes()
        assert decode_sprite_spans(encode_sprite_spans(sprite)) == pixels
        for alignment in range(4):
            for plane in range(4):
                encoded = encode_planar_sprite_spans(sprite, alignment, plane)
                decoded = decode_planar_sprite_spans(encoded, alignment, plane)
                expected = bytes(
                    color if (alignment + x) % 4 == plane else TRANSPARENT
                    for y in range(TILE)
                    for x, color in enumerate(pixels[y * TILE:(y + 1) * TILE])
                )
                assert decoded == expected
    player_frames = {
        sprites.crop((index * TILE, 0, (index + 1) * TILE, TILE)).tobytes()
        for index in range(PLAYER_FRAMES)
    }
    assert len(player_frames) == PLAYER_FRAMES


def write_pack(out: Path, tiles: Image.Image, sprites: Image.Image, level: dict, cells: bytearray) -> None:
    sprite_spans = []
    planar_sprite_spans = []
    for index in range(SPRITE_COUNT):
        sprite = sprites.crop((index * TILE, 0, (index + 1) * TILE, TILE))
        encoded = encode_sprite_spans(sprite)
        assert decode_sprite_spans(encoded) == sprite.tobytes()
        sprite_spans.append(encoded)
        for alignment in range(4):
            for plane in range(4):
                planar_sprite_spans.append(
                    encode_planar_sprite_spans(sprite, alignment, plane)
                )
    span_blob = b"".join(sprite_spans)
    planar_span_blob = b"".join(planar_sprite_spans)
    payload = bytearray(b"KOLODAT1")
    payload.extend(struct.pack("<9H", 3, level["width"], level["height"], 6,
                               SPRITE_COUNT, len(level["berries"]),
                               len(level["enemies"]), TILE, TILE))
    for r, g, b in COLORS:
        payload.extend((r >> 2, g >> 2, b >> 2))
    for index in range(6):
        tile = tiles.crop((index * TILE, 0, (index + 1) * TILE, TILE))
        payload.extend(encode_planar_tile(tile))
    payload.extend(struct.pack("<H", len(span_blob)))
    payload.extend(span_blob)
    payload.extend(struct.pack("<H", len(planar_span_blob)))
    payload.extend(planar_span_blob)
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
    validate_raster_encodings(tiles, sprites)
    if args.check:
        print("assets: PASS (level, palette, planar tiles, sprite spans)")
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
