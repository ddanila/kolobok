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
