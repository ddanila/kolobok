# Version 4 data formats

All integers are little-endian. Loaders reject earlier versions, bad CRCs,
truncation, extra payload bytes, invalid dimensions, unknown tile/object types,
out-of-bounds objects, broken associations, and counts beyond runtime limits.

## KOLOBOK.DAT

The archive header is `KOLODAT4`, version and bank count (`u16`), followed by
16-byte entries: an eight-byte NUL-padded ASCII name, `u32` file offset, and
`u32` size. Banks are `KBANK4`, contain version/theme/count metadata, a 768-byte
VGA DAC palette, planar 16×16 tiles, generic sprite spans, alignment-specific
Mode X spans, and a trailing CRC-32. The build rejects banks of 60 KiB or more.
The complete archive may exceed 64 KiB. On DOS, the selected bank is read in
chunks into its own paragraph-aligned far-memory segment. Resource pointers and
the optimized tile/sprite paths retain the source segment, so no near-data copy
or monolithic archive allocation is required.

## Tile indices

Three places agree on what each tile index means, and nothing checks them
against each other automatically:

| Index | Meaning | Flags | Material |
| --- | --- | --- | --- |
| 0 | air | – | air |
| 1, 2 | grass top, dirt body | solid | grass |
| 3 | grass platform | solid | grass |
| 4 | spikes, used for pit floors | hazard | air |
| 5, 6, 7 | sand top, body, platform | solid | sand |
| 8, 9, 10 | ice top, body, platform | solid | ice |

`draw_tiles` in `tools/assets.py` paints the art in index order, the flag and
material tables at the bottom of `make_bank` assign behaviour by index, and
`MATERIALS` in `tools/levels.py` maps each surface to its `(top, body, platform)`
triple while pit floors are hard-coded to 4. Painting art into the wrong slot
swaps two tiles in game without failing any test, which is how 3 and 4 came to
be drawn as each other's graphics: platforms rendered as floating spikes and pit
floors as inviting ledges.

## KLV4

The 32-byte header contains `KLV4`, version 4, header size, CRC-32 of the entire
payload, width, fixed height 11, theme, required-red count, cloud seed, and five
record counts. The payload order is tile map; start/exit/home; checkpoints;
pickups; animals; trees; encounters.

Records use tile coordinates. Animals include stable ID, subtype, flags, patrol
bounds, climb-tree association, climb limits, and dialogue ID. Encounters map a
stable animal ID to a dialogue, required flag, correct choice, optional reward,
and retry delay. This makes DOS-editor rewrites deterministic and independent of
record ordering or runtime pointers.

`tools/levels.py export` emits canonical JSON with the exact tile array, so an
export followed by import reproduces the KLV byte-for-byte, including its CRC.
