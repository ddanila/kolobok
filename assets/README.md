# Designer asset sources

Everything intended for hand editing lives below this directory. Files under
`generated/` are previews produced by the build and must not be edited.

## Layout

- `art/manifest.json` is the shared palette, stable tile/sprite index table,
  collision metadata, surface metadata, and character-sheet layout.
- `art/tiles.png` is an indexed 11-frame, 16×16 horizontal tile sheet.
- `art/sprites.png` is an indexed 15-frame, 16×16 horizontal sprite sheet.
- `art/grandparents.png` is an indexed four-frame character sheet ordered as
  Grandma poses 0/1 followed by Grandpa poses 0/1. Each frame is 28×40; the
  animation anchor is recorded in the manifest.
- `levels/*.json` are the reviewable level sources compiled to KLV4.
- `generated/*.png` are normalized build previews for quick inspection.

All PNG sources use palette mode (`P`) and must contain the exact 256-entry
palette described by `manifest.json`: the 32 named colors followed by black
reserved entries. Keep palette indices stable; the DOS renderer and level tile
IDs refer to them directly. `make assets` and `make host-test` reject RGB/RGBA
files, altered palettes, wrong sheet dimensions, unexpected indices, duplicate
names, or non-contiguous IDs.

The grandparents used to be rectangles hard-coded in `src/video.cpp`. They now
come from `art/grandparents.png`; the build converts its pixels into coalesced
palette-indexed rectangles in `build/generated/grandparents.inc`. This retains
the inexpensive Mode X rectangle renderer while making the actual artwork
designer-editable.

Clouds, trees, the cottage, oven, font, and UI panels are still intentional
procedural primitives in `src/video.cpp`; they do not currently have editable
raster sheets. Dialogue strings and music data also remain programmer-authored
resources. The manifest and this document make that boundary explicit so a
designer does not need to hunt for missing files.

After editing art or level JSON, run:

```sh
make assets
make host-test
make screenshot
```

The normal designer handoff is simply to commit and push the source changes.
GitHub Actions validates the palette, sheets, metadata and levels, performs a
fresh Open Watcom build, and attaches the resulting DOS game directory to that
workflow run. No release publishing or checked-in executable is involved.

Use `python3 tools/levels.py export LEVEL.KLV assets/levels/level.json` to bring
a DOS-editor level back into the reviewable JSON workflow.
