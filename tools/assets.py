#!/usr/bin/env python3
"""Build deterministic indexed VGA art and the indexed KOLOBOK.DAT v4 archive."""

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
SPRITE_COUNT = 15
TILE_COUNT = 11

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
    sheet = pal_image((TILE_COUNT * TILE, TILE))
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
    # 3 is the grass platform and 4 is the spike hazard. The order is fixed by
    # MATERIALS in tools/levels.py, which encodes grass as (top, body, platform)
    # = (1, 2, 3) and fills pit floors with 4, and by the flag and material
    # tables below. Drawing these in the wrong slots swaps them in game.
    x = 3 * TILE
    d.rectangle((x, 3, x + 15, 8), fill=21)
    d.rectangle((x, 1, x + 15, 3), fill=7)
    d.rectangle((x, 8, x + 15, 10), fill=8)
    for px in (2, 8, 13):
        d.point((x + px, 5), fill=11)
    x = 4 * TILE
    for i in range(4):
        bx = x + i * 4
        d.polygon((bx, 15, bx + 2, 5, bx + 4, 15), fill=23)
        d.line((bx + 2, 7, bx + 2, 14), fill=22)
    x = 5 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=5)
    d.ellipse((x + 2, 2, x + 13, 13), fill=6)
    d.rectangle((x + 6, 0, x + 8, 15), fill=21)
    # 5..7 sand top, body and platform.
    x = 5 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=11)
    d.rectangle((x, 0, x + 15, 2), fill=31)
    for px, py in ((2, 6), (10, 5), (6, 12), (14, 10)):
        d.point((x + px, py), fill=20)
    x = 6 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=26)
    for px, py in ((1, 3), (8, 2), (4, 9), (12, 13)):
        d.line((x + px, py, x + px + 2, py), fill=11)
    x = 7 * TILE
    d.rectangle((x, 3, x + 15, 9), fill=26)
    d.rectangle((x, 1, x + 15, 3), fill=31)
    d.rectangle((x, 9, x + 15, 11), fill=21)
    # 8..10 ice top, body and platform.
    x = 8 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=27)
    d.rectangle((x, 0, x + 15, 3), fill=28)
    d.line((x + 2, 7, x + 9, 12), fill=2)
    d.line((x + 9, 12, x + 14, 8), fill=2)
    x = 9 * TILE
    d.rectangle((x, 0, x + 15, 15), fill=3)
    d.line((x + 1, 4, x + 7, 10), fill=28)
    d.line((x + 7, 10, x + 14, 3), fill=2)
    x = 10 * TILE
    d.rectangle((x, 3, x + 15, 9), fill=27)
    d.rectangle((x, 1, x + 15, 3), fill=28)
    d.rectangle((x, 9, x + 15, 11), fill=3)
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
    # 9 wolf, 10 bear.
    x = 9 * TILE
    d.ellipse((x + 1, 6, x + 13, 14), fill=22)
    d.polygon((x + 3, 7, x + 2, 1, x + 7, 6), fill=22)
    d.polygon((x + 9, 6, x + 12, 1, x + 13, 8), fill=22)
    d.polygon((x + 11, 8, x + 15, 10, x + 11, 12), fill=23)
    d.point((x + 9, 7), fill=16)
    d.line((x + 2, 14, x + 12, 14), fill=1)
    x = 10 * TILE
    d.ellipse((x + 1, 4, x + 14, 15), fill=9)
    d.ellipse((x + 3, 1, x + 7, 6), fill=10)
    d.ellipse((x + 9, 1, x + 13, 6), fill=10)
    d.ellipse((x + 5, 6, x + 12, 12), fill=10)
    d.point((x + 6, 7), fill=1); d.point((x + 11, 7), fill=1)
    # 11 blue berry, 12 small pie, 13 big pie, 14 freeze snowflake.
    x = 11 * TILE
    d.ellipse((x + 3, 4, x + 12, 13), fill=27)
    d.ellipse((x + 5, 5, x + 10, 11), fill=28)
    d.line((x + 8, 4, x + 10, 1), fill=6)
    x = 12 * TILE
    d.polygon((x + 2, 11, x + 5, 5, x + 11, 5, x + 14, 11), fill=20)
    d.rectangle((x + 3, 10, x + 13, 14), fill=13)
    d.line((x + 5, 7, x + 11, 7), fill=31)
    x = 13 * TILE
    d.ellipse((x + 1, 3, x + 14, 15), fill=13)
    d.ellipse((x + 3, 5, x + 12, 12), fill=20)
    d.line((x + 4, 7, x + 11, 7), fill=31)
    x = 14 * TILE
    for angle in ((8, 1, 8, 14), (2, 4, 13, 11), (2, 11, 13, 4)):
        d.line(tuple(x + v if i % 2 == 0 else v for i, v in enumerate(angle)), fill=28)
    d.ellipse((x + 6, 6, x + 9, 9), fill=31)
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
    for index in range(TILE_COUNT):
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


def themed_art(tiles: Image.Image, sprites: Image.Image, bank_name: str) -> tuple[Image.Image, Image.Image]:
    """Produce bank-specific indexed art without changing transparency."""
    tile_copy, sprite_copy = tiles.copy(), sprites.copy()
    if bank_name == "INTRO":
        sprite_copy = sprite_copy.point(lambda c: 31 if c == 14 else c)
    elif bank_name == "FOREST":
        tile_copy = tile_copy.point(lambda c: 6 if c == 7 else c)
        sprite_copy = sprite_copy.point(lambda c: 26 if c == 20 else c)
    elif bank_name == "DEEP":
        tile_copy = tile_copy.point(lambda c: 5 if c in (6, 7) else c)
        sprite_copy = sprite_copy.point(lambda c: 23 if c == 31 else (24 if c == 22 else c))
    return tile_copy, sprite_copy


def bank_palette(bank_name: str) -> list[tuple[int, int, int]]:
    if bank_name == "GARDEN":
        return COLORS
    palette = []
    for index, (r, g, b) in enumerate(COLORS):
        if bank_name == "INTRO":
            rgb = (min(255, r + 14), min(255, g + 5), max(0, b - 6))
        elif bank_name == "FOREST":
            rgb = (r * 4 // 5, min(255, g * 9 // 10 + (8 if index in (5, 6, 7) else 0)), b * 4 // 5)
        else:
            rgb = (r * 11 // 20, g * 3 // 5, min(255, b * 7 // 10 + (8 if index in (1, 2, 3, 27, 28) else 0)))
        palette.append(rgb if index else (0, 0, 0))
    return palette


def make_bank(tiles: Image.Image, sprites: Image.Image, theme: int, bank_name: str) -> bytes:
    tiles, sprites = themed_art(tiles, sprites, bank_name)
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
    payload = bytearray(b"KBANK4\0\0")
    payload.extend(struct.pack("<6H", 4, theme, TILE_COUNT,
                               SPRITE_COUNT, TILE, TILE))
    for r, g, b in bank_palette(bank_name):
        payload.extend((r >> 2, g >> 2, b >> 2))
    for index in range(TILE_COUNT):
        tile = tiles.crop((index * TILE, 0, (index + 1) * TILE, TILE))
        payload.extend(encode_planar_tile(tile))
    # Per-tile collision flags and surface material (grass/sand/ice/air).
    payload.extend((0, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1))
    payload.extend((3, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2))
    payload.extend(struct.pack("<H", len(span_blob)))
    payload.extend(span_blob)
    payload.extend(struct.pack("<H", len(planar_span_blob)))
    payload.extend(planar_span_blob)
    payload.extend(struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF))
    assert len(payload) < 60 * 1024, "resource bank exceeds the 60 KiB limit"
    return bytes(payload)


def write_pack(out: Path, tiles: Image.Image, sprites: Image.Image) -> None:
    names = (("INTRO", 0), ("GARDEN", 0), ("FOREST", 1), ("DEEP", 2))
    banks = [(name, make_bank(tiles, sprites, theme, name)) for name, theme in names]
    header_size = 12 + len(banks) * 16
    offset = header_size
    payload = bytearray(b"KOLODAT4")
    payload.extend(struct.pack("<HH", 4, len(banks)))
    for name, bank in banks:
        payload.extend(name.encode("ascii").ljust(8, b"\0"))
        payload.extend(struct.pack("<II", offset, len(bank)))
        offset += len(bank)
    for _, bank in banks:
        payload.extend(bank)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "KOLOBOK.DAT")
    args = parser.parse_args()
    tiles, sprites = draw_tiles(), draw_sprites()
    validate_raster_encodings(tiles, sprites)
    if args.check:
        print("assets: PASS (v4 banks, palette, planar tiles, sprite spans)")
        return
    source_dir = ROOT / "assets" / "generated"
    source_dir.mkdir(parents=True, exist_ok=True)
    tiles.save(source_dir / "tiles.png", optimize=False)
    sprites.save(source_dir / "sprites.png", optimize=False)
    palette = pal_image((256, 1))
    palette.putdata(range(256))
    palette.save(source_dir / "palette.png", optimize=False)
    write_pack(args.out, tiles, sprites)
    print(f"assets: wrote {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
