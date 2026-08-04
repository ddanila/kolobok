# Kolobok: Expanded Adventure

![Kolobok in the Garden](docs/screenshot.png)

Kolobok is a 16-bit real-mode DOS platform adventure based on the Slavic
folktale. Its campaign runs from an animated cottage intro through the Garden,
Small Forest, and Deep Forest to a homecoming ending. Each level requires its
red berries and a dialogue puzzle with its guardian.

The game targets a 386DX-40, renders directly to planar 320×200×256 Mode X,
and updates at 30 Hz. It is cross-compiled on Linux with Open Watcom; DOSBox-X
only runs and verifies the completed DOS binaries.

## Playing

- Left/Right or `A`/`D`: roll
- Space or Up: jump; hold for a higher jump
- Enter: talk to a marked nearby animal
- `1`–`3`, or arrows and Enter: choose dialogue answers
- `M`: toggle detected AdLib music
- `S`: toggle PC-speaker effects
- Escape: skip the intro or pause

The title menu offers New Game, Codeword, and Quit. `REPKA` starts the Garden
without its intro, `TEREMOK` starts the Small Forest, and `MOROZKO` starts the
Deep Forest. Codeword games begin with 100 HP and three lives; sequential play
carries HP and lives between levels.

Rabbit, fox, wolf, and bear contact causes 10, 25, 40, and 50 damage. Rabbit
and fox damage cannot reduce HP below one; stronger animals can take a life.
Pits take a life immediately. Respawning restores 100 HP at the latest
checkpoint while preserving pickups and solved encounters.

Red berries open the exit. Blue berries give a refreshable ten-second speed
boost. Small pies heal and restore up to three lives only when useful. Big pies
heal, refill to three lives, or grant the fourth bonus life. Stomping an animal
gives the boosted bounce and freezes it for three seconds.

Command-line options are `-nosound`, `-nomusic`, `-selftest`, `-benchmark`, and
`-capture [intro|garden|forest|deep|dialogue|frozen|gameover|home|credits]`.
`-playtest` is the deterministic target-side campaign driver used by the test
suite.

## Build and test

Install GNU Make, Bash, curl, unzip, Python 3 with Pillow, GCC, and DOSBox-X,
then run:

```sh
make
make test
make dist
```

The first build installs a pinned Linux-hosted Open Watcom toolchain under
`.tools/`. `make test` runs host gameplay and state-machine tests, campaign
balance checks, KLV/archive JSON round-trips and CRC rejection, the AdLib mock
register sink, DOS-native game/editor self-tests, a complete deterministic
three-level playthrough, and the 7,350-cycle Deep Forest performance gate. The
gate requires at least 50 raw frames/s and 29.5 paced frames/s.

`make screenshot` captures nine deterministic DOS-native states and records
their VRAM CRCs. `make dist` creates a redistributable directory containing
`KOLOBOK.EXE`, `KOLOEDIT.EXE`, `KOLOBOK.DAT`, and all three KLV files.

## Level editor

Run `KOLOEDIT.EXE LEVEL.KLV`. If no filename is supplied, it prompts for an
8.3 name; a missing file is initialized as an 80×11 level.

- Arrows move the cursor and scroll
- Tab changes Tile/Object/Marker layer
- Page Up/Down changes the selected tool
- Space paints or places; Delete erases
- Enter opens a property form for the selected object. It covers subtype,
  flags, reward/dialogue data, patrol bounds, tree type and climb association
- `1`, `2`, `3` place start, checkpoint, and exit
- F2 saves atomically through `LEVEL.TMP`; F3 validates; F4 edits level theme,
  required-red count, and cloud seed
- Escape opens the save/discard/cancel confirmation

`KOLOEDIT.EXE -selftest` performs a create/edit/save/reload/delete cycle entirely
inside DOSBox-X without GUI automation.

## Data layout

`KOLOBOK.DAT` version 4 is an indexed archive containing visually distinct
`INTRO`, `GARDEN`, `FOREST`, and `DEEP` banks. The archive exceeds 64 KiB, but
every bank is checked to remain below 60 KiB. DOS allocates only the active bank
in a paragraph-aligned far-memory segment; the tile and sprite blitters consume
far sources directly instead of copying the bank into near data.

`GARDEN.KLV`, `SFOREST.KLV`, and `DFOREST.KLV` are little-endian version-4 level
files with CRC-32 protection, fixed 11-row maps, material tiles, markers,
pickups, animal patrol/climb data, trees, and encounters. Reviewable sources are
under `assets/`. Convert DOS editor output back to canonical JSON with:

```sh
python3 tools/levels.py export LEVEL.KLV level.json
python3 tools/levels.py import level.json LEVEL.KLV
```

See [formats.md](docs/formats.md), [music.md](docs/music.md), and
[performance.md](docs/performance.md) for the binary contracts, music
provenance, and benchmark method.

## License

Code, original graphics, and the original arrangement are MIT licensed. See
[LICENSE](LICENSE). The underlying 1869 folk-song melody is public domain.
