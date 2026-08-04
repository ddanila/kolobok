# Kolobok

![Kolobok running in VGA Mode 13h](docs/screenshot.png)

Kolobok is a small MS-DOS platform game inspired by the Slavic folktale. Roll out
from Grandmother and Grandfather's cottage, collect eight forest berries, avoid or
bounce on the animals, and bring the berries home.

The MVP is a real-mode DOS program written mostly in C. It renders directly into
VGA mode `13h` (320×200, 256 colours), targets a 386-class machine, and runs at a
fixed 30 Hz gameplay update rate.

## Play

- Left/Right arrows or `A`/`D`: roll
- Space or Up arrow: jump; hold for a higher jump
- `S`: toggle PC-speaker sound
- Escape: pause, then Escape again to quit
- Enter: start, resume, or return to the title after winning

There are no lives. Hazards return Kolobok to the latest checkpoint while keeping
collected berries. Hares patrol, foxes chase when nearby, and landing on either
animal gives a harmless bounce.

## Build on Linux for DOS

The build uses the **Linux-hosted Open Watcom v2 compiler to cross-compile for
16-bit DOS**. DOSBox-X is only used to execute and test the completed DOS binary;
it is never used as a compiler environment.

Required host tools are GNU Make, Bash, curl, unzip, Python 3 with Pillow, GCC,
and DOSBox-X. On Debian-family systems the non-Watcom dependencies can usually be
installed with:

```sh
sudo apt install build-essential curl unzip python3 python3-pil dosbox-x
```

Build the game:

```sh
make
```

The first build downloads the Open Watcom v2 **2026-08-01 daily build**, Linux
x64 host package, and verifies SHA-256
`e1bc4e88fa47191118f29e53731e7f4542803c7f4c503e15d72f1f571ac0832f`.
It is installed locally under `.tools/` and is not committed.

Run in DOSBox-X:

```sh
make run
```

Create the redistributable directory:

```sh
make dist
```

This produces `dist/KOLOBOK.EXE`, `dist/KOLOBOK.DAT`, `dist/README.TXT`, and
`dist/LICENSE`. Copy the EXE and DAT together; the game loads `KOLOBOK.DAT` from
its current DOS directory. Run `KOLOBOK.EXE -nosound` to disable speaker effects.

## Test

```sh
make test
```

The test suite performs:

- native Linux unit tests for movement, variable jumping, collection, checkpoint
  persistence, and rejection of a corrupted data pack;
- deterministic level and asset-budget validation;
- a Linux-hosted Open Watcom cross-build for the DOS target;
- a headless DOSBox-X target self-test covering asset loading, gameplay completion,
  VGA rendering, and a pinned reference-frame CRC.

The DOS-only self-test may also be run manually as
`KOLOBOK.EXE -selftest`. A passing build prints
`KOLOBOK SELFTEST PASS CRC=5BD3ECB5` and returns exit code zero.

## Project layout

- `src/` — portable game state plus DOS input, VGA, and PC-speaker backends
- `assets/level.json` — editable one-loop forest level
- `assets/generated/` — deterministic indexed sprite and tile sheets
- `tools/assets.py` — palette, sprite, tile, validation, and `KOLOBOK.DAT` packer
- `tests/` — host-side gameplay and data-integrity tests
- `dosbox-x.conf` — 386-oriented emulator configuration
- `docs/art-direction.png` — folklore art-direction reference

The simulation uses 24.8 fixed-point arithmetic and has no floating-point runtime
dependency. Rendering uses a 64 KiB back buffer copied to VGA memory each frame.
The compact data pack contains a 256-entry 6-bit DAC palette, 16×16 indexed tiles
and sprites, the map, collectibles, enemies, checkpoint, and a CRC-32 checksum.

## License

MIT — see [LICENSE](LICENSE).
