#!/usr/bin/env python3
"""Compile indexed cutscene character sheets into DOS rectangle tables."""

from __future__ import annotations

import argparse
from pathlib import Path

import assets as art_assets


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "art" / "grandparents.png"
PREVIEW = ROOT / "assets" / "generated" / "grandparents.png"
DEFAULT_OUT = ROOT / "build" / "generated" / "grandparents.inc"


def coalesce_frame(pixels: bytes, width: int, height: int) -> list[tuple[int, int, int, int, int]]:
    used = bytearray(width * height)
    rectangles = []
    for y in range(height):
        for x in range(width):
            at = y * width + x
            color = pixels[at]
            if color == 0 or used[at]:
                continue
            run_width = 1
            while x + run_width < width:
                candidate = at + run_width
                if used[candidate] or pixels[candidate] != color:
                    break
                run_width += 1
            run_height = 1
            while y + run_height < height:
                row = (y + run_height) * width + x
                if any(used[row + dx] or pixels[row + dx] != color for dx in range(run_width)):
                    break
                run_height += 1
            for dy in range(run_height):
                row = (y + dy) * width + x
                used[row:row + run_width] = b"\1" * run_width
            rectangles.append((x, y, run_width, run_height, color))
    rebuilt = bytearray(width * height)
    for x, y, rect_width, rect_height, color in rectangles:
        for dy in range(rect_height):
            start = (y + dy) * width + x
            rebuilt[start:start + rect_width] = bytes((color,)) * rect_width
    if bytes(rebuilt) != pixels:
        raise ValueError("grandparent rectangle coalescing changed pixels")
    return rectangles


def compile_source() -> tuple[str, object]:
    manifest = art_assets.load_manifest()
    colors = art_assets.full_palette(manifest)
    metadata = manifest["grandparents"]
    frame_width = metadata["frame_width"]
    frame_height = metadata["frame_height"]
    frame_names = metadata["frames"]
    image = art_assets.load_indexed_sheet(
        SOURCE, (frame_width * len(frame_names), frame_height), manifest, colors
    )
    all_rectangles = []
    offsets = [0]
    for frame in range(len(frame_names)):
        crop = image.crop((frame * frame_width, 0, (frame + 1) * frame_width, frame_height))
        rectangles = coalesce_frame(crop.tobytes(), frame_width, frame_height)
        all_rectangles.extend(rectangles)
        offsets.append(len(all_rectangles))
    anchor_x = metadata["anchor_x"]
    anchor_y = metadata["anchor_y"]
    lines = [
        "/* Generated from assets/art/grandparents.png. Do not edit. */",
        "typedef struct KoloArtRect { s8 x, y; u8 width, height, color; } KoloArtRect;",
        "static const KoloArtRect kolo_grandparent_rects[] = {",
    ]
    for x, y, width, height, color in all_rectangles:
        lines.append(f"    {{{x - anchor_x}, {y - anchor_y}, {width}, {height}, {color}}},")
    lines.extend((
        "};",
        "static const u16 kolo_grandparent_offsets[] = {" +
        ", ".join(str(offset) for offset in offsets) + "};",
        ""
    ))
    return "\n".join(lines), image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated, image = compile_source()
    if args.check:
        if not args.out.exists() or args.out.read_text() != generated:
            raise SystemExit("characters: generated table is stale; run make assets")
        if not art_assets.preview_matches(PREVIEW, image):
            raise SystemExit("characters: generated preview is stale; run make assets")
        print("characters: PASS (indexed palette, frames, generated rectangles)")
        return
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(generated)
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    art_assets.save_preview(PREVIEW, image)
    print(f"characters: wrote {args.out}")


if __name__ == "__main__":
    main()
