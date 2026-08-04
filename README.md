# Kolobok

![Kolobok running in planar VGA Mode X](docs/screenshot.png)

Kolobok is a small MS-DOS platform game inspired by the Slavic folktale. Roll out
from Grandmother and Grandfather's cottage, collect eight forest berries, avoid or
bounce on the animals, and bring the berries home.

The MVP is a real-mode DOS program written mostly in C. It uses unchained planar
VGA Mode X at 320×200 with 256 colours, targets a 386DX-40, and runs at a fixed
30 Hz gameplay update rate.

## Play

- Left/Right arrows or `A`/`D`: roll
- Space or Up arrow: jump; hold for a higher jump
- `S`: toggle PC-speaker sound
- Escape: pause, then Escape again to quit
- Enter: start, resume, or return to the title after winning

There are no lives. Hazards return Kolobok to the latest checkpoint while keeping
collected berries. Movement accelerates and coasts instead of starting and stopping
instantly. Hares patrol, foxes chase when nearby, and landing on either animal
gives a boosted bounce.

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
- deterministic level, palette, planar-tile, and sprite-span validation;
- a Linux-hosted Open Watcom cross-build for the DOS target;
- a target-side render benchmark at the DOSBox-X 386DX-40 profile, gated at
  50 fps raw throughput and 29.5 fps while paced at the game's 30 Hz rate;
- target-side PIT profiling for background, tiles, sprites, HUD, and VGA page
  presentation, with every stage required to report data;
- a headless DOSBox-X target self-test covering asset loading, gameplay completion,
  alternating-page identity, VGA display/panning register state, title dirty
  updates, cached pause frames, Mode X rendering and presentation, and pinned
  logical-frame/VRAM reference CRCs;
- regression tests for movement inertia, four-frame rolling, left-edge clamping,
  enemy-bounce boosting, and the cottage's platform-free footprint.

The DOS-only self-test may also be run manually as
`KOLOBOK.EXE -selftest`. A passing build prints
`KOLOBOK SELFTEST PASS CRC=8EF18BDB VRAM=8EF18BDB` and returns exit code zero.

Run only the 386 performance regression with:

```sh
make perf-test
```

The current build measures about 68.2 raw rendered frames/s and 30.3 paced
frames/s at 7,350 cycles. See [the performance methodology](docs/performance.md)
for calibration, baseline, regression thresholds, and limitations.

Regenerate the repository screenshot from the real DOS renderer without GUI input
automation:

```sh
make screenshot
```

This runs `KOLOBOK.EXE -capture` in DOSBox-X, dumps the displayed Mode X page,
and converts it to `docs/screenshot.png` on Linux.

## Project layout

- `src/` — portable game state plus DOS input, VGA, and PC-speaker backends
- `assets/level.json` — editable one-loop forest level
- `assets/generated/` — deterministic indexed sprite and tile sheets
- `tools/assets.py` — palette, sprite, tile, validation, and `KOLOBOK.DAT` packer
- `tests/` — host-side gameplay and data-integrity tests
- `dosbox-x.conf` — 386-oriented emulator configuration
- `docs/performance.md` — 386DX-40 benchmark method and measured results
- `docs/art-direction.png` — folklore art-direction reference

The simulation uses 24.8 fixed-point arithmetic and has no floating-point runtime
dependency. Rendering goes directly to two VGA pages and flips them through the
CRTC; fine horizontal scrolling uses the VGA pel-panning register. VGA latches
copy cached planar tiles and HUD rows, sprites use alignment-specific opaque-span
streams, and the persistent title page updates only its blinking prompt. A
clock-based frame scheduler preserves the 30 Hz update rate. The compact data pack
contains a 256-entry 6-bit DAC palette, pre-planar 16×16 tiles, generic and planar
sprite spans, the map, collectibles, enemies, checkpoint, and a CRC-32 checksum.

## License

MIT — see [LICENSE](LICENSE).
