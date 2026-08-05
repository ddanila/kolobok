# Level JSON sources

These files are the hand-editable campaign sources. `make levels` compiles them
to CRC-protected KLV4 files in `build/`; do not commit edited build output as the
source of truth.

Every level is exactly 11 rows high and 32–256 tiles wide. The high-level JSON
form supports:

- `theme`, `cloud_seed`, `required_red_berries`, `ground_y`, pits and material
  surface ranges;
- platforms, start/home/exit markers and checkpoints;
- red/blue berries and small/big pies;
- rabbits, foxes, wolves and bears with patrol bounds, dialogue IDs, flags and
  optional climb-tree associations;
- fir, birch and oak trees;
- required or optional encounters, answers, rewards and retry delays.

Tile IDs and names are stable and documented in `../art/manifest.json`. Validate
all three source levels with `make host-test`. To round-trip a file saved by
`KOLOEDIT.EXE`, export it to canonical JSON:

```sh
python3 tools/levels.py export LEVEL.KLV assets/levels/level.json
python3 tools/levels.py validate LEVEL.KLV
```

The canonical export contains an explicit flattened `tiles` array. It is valid
input to the same importer even though the campaign files use the more concise
ground/platform representation.
