#!/usr/bin/env python3
"""Compile designer-owned indexed PNG art into KOLOBOK.DAT v4."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ART_DIR = ROOT / "assets" / "art"
GENERATED_DIR = ROOT / "assets" / "generated"
TILE = 16
TRANSPARENT = 0
PLAYER_FRAMES = 4
SPRITE_COUNT = 15
TILE_COUNT = 11
SOURCE_SHEETS = (
    ("tiles.png", (TILE_COUNT * TILE, TILE)),
    ("sprites.png", (SPRITE_COUNT * TILE, TILE)),
    ("grandparents.png", (4 * 28, 40)),
)


def load_manifest() -> dict:
    manifest = json.loads((ART_DIR / "manifest.json").read_text())
    if manifest.get("format") != "KOLOBOK_ART_V1":
        raise ValueError("unsupported art manifest")
    if manifest.get("tile_size") != TILE or manifest.get("transparent_index") != TRANSPARENT:
        raise ValueError("art manifest tile size or transparency changed")
    for key, count in (("tiles", TILE_COUNT), ("sprites", SPRITE_COUNT)):
        entries = manifest.get(key, [])
        if len(entries) != count or [entry.get("id") for entry in entries] != list(range(count)):
            raise ValueError(f"{key} IDs must be contiguous from zero")
        names = [entry.get("name") for entry in entries]
        if any(not isinstance(name, str) or not name for name in names) or len(set(names)) != count:
            raise ValueError(f"{key} names must be unique and non-empty")
    colors = manifest.get("palette", [])
    if not 1 <= len(colors) <= 256 or any(
        not isinstance(rgb, list) or len(rgb) != 3 or
        any(not isinstance(value, int) or not 0 <= value <= 255 for value in rgb)
        for rgb in colors
    ):
        raise ValueError("manifest palette must contain RGB byte triples")
    collision_names = {"none", "solid", "hazard"}
    material_names = {"grass", "sand", "ice", "air"}
    for tile in manifest["tiles"]:
        if tile.get("collision") not in collision_names or tile.get("material") not in material_names:
            raise ValueError(f"invalid metadata for tile {tile['id']}")
    grandparents = manifest.get("grandparents", {})
    if grandparents.get("frame_width") != 28 or grandparents.get("frame_height") != 40 or \
       grandparents.get("anchor_x") != 3 or grandparents.get("anchor_y") != 2 or \
       grandparents.get("frames") != ["grandma_0", "grandma_1", "grandpa_0", "grandpa_1"]:
        raise ValueError("invalid grandparents sheet metadata")
    return manifest


def full_palette(manifest: dict) -> list[tuple[int, int, int]]:
    colors = [tuple(rgb) for rgb in manifest["palette"]]
    return colors + [(0, 0, 0)] * (256 - len(colors))


def palette_bytes(colors: list[tuple[int, int, int]]) -> list[int]:
    return [component for rgb in colors for component in rgb]


def replace_manifest_palette(text: str, colors: list[tuple[int, int, int]]) -> str:
    """Replace only the palette array, keeping the hand-formatted metadata intact."""
    key = text.index('"palette"')
    start = text.index("[", key)
    depth = 0
    end = start
    for end in range(start, len(text)):
        if text[end] == "[":
            depth += 1
        elif text[end] == "]":
            depth -= 1
            if depth == 0:
                break
    else:
        raise ValueError("unterminated palette in art manifest")
    replacement = "[\n" + ",\n".join(
        "    " + json.dumps(list(rgb)) for rgb in colors
    ) + "\n  ]"
    return text[:start] + replacement + text[end + 1:]


def normalize_sources() -> None:
    """Import true-color editor output into the shared indexed art palette."""
    manifest_path = ART_DIR / "manifest.json"
    manifest = load_manifest()
    colors = [tuple(rgb) for rgb in manifest["palette"]]
    source_pixels = []
    new_colors = set()

    for name, expected_size in SOURCE_SHEETS:
        path = ART_DIR / name
        image = Image.open(path).convert("RGBA")
        image.load()
        if image.size != expected_size:
            raise ValueError(f"{path} must be {expected_size[0]}x{expected_size[1]}")
        raw_pixels = image.tobytes()
        pixels = [tuple(raw_pixels[offset:offset + 4])
                  for offset in range(0, len(raw_pixels), 4)]
        partial_alpha = sorted({alpha for _, _, _, alpha in pixels if alpha not in (0, 255)})
        if partial_alpha:
            raise ValueError(f"{path} uses partial alpha values: {partial_alpha}")
        source_pixels.append((path, image.size, pixels))
        new_colors.update(
            tuple(rgb) for *rgb, alpha in pixels
            if alpha == 255 and tuple(rgb) not in colors
        )

    appended = sorted(new_colors)
    colors.extend(appended)
    if len(colors) > 256:
        raise ValueError(
            f"art sources require {len(colors)} colors; indexed VGA art supports at most 256"
        )

    color_indices = {rgb: index for index, rgb in enumerate(colors)}
    full_colors = colors + [(0, 0, 0)] * (256 - len(colors))
    for path, size, pixels in source_pixels:
        normalized = Image.new("P", size, TRANSPARENT)
        normalized.putpalette(palette_bytes(full_colors))
        normalized.putdata([
            TRANSPARENT if alpha == 0 else color_indices[tuple(rgb)]
            for *rgb, alpha in pixels
        ])
        save_preview(path, normalized)

    if appended:
        manifest_path.write_text(replace_manifest_palette(manifest_path.read_text(), colors))
    print(f"art import: normalized {len(source_pixels)} sheets; appended {len(appended)} colors")


def load_indexed_sheet(path: Path, size: tuple[int, int], manifest: dict,
                       colors: list[tuple[int, int, int]]) -> Image.Image:
    image = Image.open(path)
    image.load()
    if image.mode != "P":
        raise ValueError(f"{path} must be an indexed palette PNG")
    if image.size != size:
        raise ValueError(f"{path} must be {size[0]}x{size[1]}")
    if image.getpalette() != palette_bytes(colors):
        raise ValueError(f"{path} palette differs from art/manifest.json")
    if max(image.tobytes()) >= len(manifest["palette"]):
        raise ValueError(f"{path} uses a reserved palette index")
    return image


def load_art_sources() -> tuple[dict, list[tuple[int, int, int]], Image.Image, Image.Image]:
    manifest = load_manifest()
    colors = full_palette(manifest)
    tiles = load_indexed_sheet(ART_DIR / "tiles.png", (TILE_COUNT * TILE, TILE), manifest, colors)
    sprites = load_indexed_sheet(ART_DIR / "sprites.png", (SPRITE_COUNT * TILE, TILE), manifest, colors)
    return manifest, colors, tiles, sprites


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
        for start, run_colors in runs:
            encoded.extend((start, len(run_colors)))
            encoded.extend(run_colors)
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
    return bytes(pixels[y * TILE + x]
                 for plane in range(4) for y in range(TILE)
                 for x in range(plane, TILE, 4))


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
        samples = [((alignment + x) // 4, pixels[y * TILE + x]) for x in range(TILE)
                   if (alignment + x) % 4 == plane and pixels[y * TILE + x] != TRANSPARENT]
        runs: list[tuple[int, bytes]] = []
        cursor = 0
        while cursor < len(samples):
            start = samples[cursor][0]
            run_colors = bytearray((samples[cursor][1],))
            cursor += 1
            while cursor < len(samples) and samples[cursor][0] == start + len(run_colors):
                run_colors.append(samples[cursor][1])
                cursor += 1
            runs.append((start, bytes(run_colors)))
        encoded.append(len(runs))
        for start, run_colors in runs:
            encoded.extend((start, len(run_colors)))
            encoded.extend(run_colors)
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
                expected = bytes(color if (alignment + x) % 4 == plane else TRANSPARENT
                                 for y in range(TILE)
                                 for x, color in enumerate(pixels[y * TILE:(y + 1) * TILE]))
                assert decoded == expected
    frames = {sprites.crop((index * TILE, 0, (index + 1) * TILE, TILE)).tobytes()
              for index in range(PLAYER_FRAMES)}
    assert len(frames) == PLAYER_FRAMES


def themed_art(tiles: Image.Image, sprites: Image.Image, bank_name: str) -> tuple[Image.Image, Image.Image]:
    tile_copy, sprite_copy = tiles.copy(), sprites.copy()
    if bank_name == "INTRO":
        sprite_copy = sprite_copy.point(lambda color: 31 if color == 14 else color)
    elif bank_name == "FOREST":
        tile_copy = tile_copy.point(lambda color: 6 if color == 7 else color)
        sprite_copy = sprite_copy.point(lambda color: 26 if color == 20 else color)
    elif bank_name == "DEEP":
        tile_copy = tile_copy.point(lambda color: 5 if color in (6, 7) else color)
        # Keep the wolf's main gray (22): remapping it to soot (24) made its
        # body identical to the Deep Forest horizon. Only the lemon accent is
        # subdued for the night bank.
        sprite_copy = sprite_copy.point(lambda color: 23 if color == 31 else color)
    return tile_copy, sprite_copy


def bank_palette(bank_name: str, colors: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    if bank_name == "GARDEN":
        return colors
    palette = []
    for index, (r, g, b) in enumerate(colors):
        if bank_name == "INTRO":
            rgb = (min(255, r + 14), min(255, g + 5), max(0, b - 6))
        elif bank_name == "FOREST":
            rgb = (r * 4 // 5, min(255, g * 9 // 10 + (8 if index in (5, 6, 7) else 0)), b * 4 // 5)
        else:
            rgb = (r * 11 // 20, g * 3 // 5,
                   min(255, b * 7 // 10 + (8 if index in (1, 2, 3, 27, 28) else 0)))
        palette.append(rgb if index else (0, 0, 0))
    return palette


def make_bank(tiles: Image.Image, sprites: Image.Image, manifest: dict,
              colors: list[tuple[int, int, int]], theme: int, bank_name: str) -> bytes:
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
                planar_sprite_spans.append(encode_planar_sprite_spans(sprite, alignment, plane))
    span_blob = b"".join(sprite_spans)
    planar_span_blob = b"".join(planar_sprite_spans)
    payload = bytearray(b"KBANK4\0\0")
    payload.extend(struct.pack("<6H", 4, theme, TILE_COUNT, SPRITE_COUNT, TILE, TILE))
    for r, g, b in bank_palette(bank_name, colors):
        payload.extend((r >> 2, g >> 2, b >> 2))
    for index in range(TILE_COUNT):
        payload.extend(encode_planar_tile(tiles.crop((index * TILE, 0, (index + 1) * TILE, TILE))))
    collision_codes = {"none": 0, "solid": 1, "hazard": 2}
    material_codes = {"grass": 0, "sand": 1, "ice": 2, "air": 3}
    payload.extend(collision_codes[tile["collision"]] for tile in manifest["tiles"])
    payload.extend(material_codes[tile["material"]] for tile in manifest["tiles"])
    payload.extend(struct.pack("<H", len(span_blob)))
    payload.extend(span_blob)
    payload.extend(struct.pack("<H", len(planar_span_blob)))
    payload.extend(planar_span_blob)
    payload.extend(struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF))
    assert len(payload) < 60 * 1024, "resource bank exceeds the 60 KiB limit"
    return bytes(payload)


def write_pack(out: Path, tiles: Image.Image, sprites: Image.Image, manifest: dict,
               colors: list[tuple[int, int, int]]) -> None:
    names = (("INTRO", 0), ("GARDEN", 0), ("FOREST", 1), ("DEEP", 2))
    banks = [(name, make_bank(tiles, sprites, manifest, colors, theme, name))
             for name, theme in names]
    offset = 12 + len(banks) * 16
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


def preview_matches(path: Path, expected: Image.Image) -> bool:
    if not path.exists():
        return False
    actual = Image.open(path)
    actual.load()
    return actual.mode == "P" and actual.size == expected.size and \
        actual.getpalette() == expected.getpalette() and actual.tobytes() == expected.tobytes()


def save_preview(path: Path, image: Image.Image) -> None:
    """Write a preview only when its indexed raster actually changed.

    Pillow versions can encode identical PNG pixels differently. Skipping an
    unchanged file keeps build-only compression differences out of commits.
    """
    if preview_matches(path, image):
        return
    image.save(path, optimize=False, compress_level=9)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--normalize-sources", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "KOLOBOK.DAT")
    args = parser.parse_args()
    if args.normalize_sources:
        normalize_sources()
        return
    manifest, colors, tiles, sprites = load_art_sources()
    validate_raster_encodings(tiles, sprites)
    palette = Image.new("P", (256, 1), TRANSPARENT)
    palette.putpalette(palette_bytes(colors))
    palette.putdata(range(256))
    if args.check:
        if not preview_matches(GENERATED_DIR / "tiles.png", tiles) or \
           not preview_matches(GENERATED_DIR / "sprites.png", sprites) or \
           not preview_matches(GENERATED_DIR / "palette.png", palette):
            raise SystemExit("assets: generated previews are stale; run make assets")
        print("assets: PASS (indexed sources, palette, metadata, v4 encodings)")
        return
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    save_preview(GENERATED_DIR / "tiles.png", tiles)
    save_preview(GENERATED_DIR / "sprites.png", sprites)
    save_preview(GENERATED_DIR / "palette.png", palette)
    write_pack(args.out, tiles, sprites, manifest, colors)
    print(f"assets: wrote {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
