# 386DX-40 performance target

Kolobok's performance regression runs inside the finished 16-bit DOS executable.
The Linux or macOS host cross-compiles it first; DOSBox-X only executes the
result.

## Calibration

The test fixes DOSBox-X to `core=normal`, `cputype=386`, and `cycles=fixed 7350`.
DOSBox-X defines cycles as approximate emulated instructions per millisecond. Its
[CPU settings guide](https://dosbox-x.com/wiki/Guide%3ACPU-settings-in-DOSBox%E2%80%90X)
lists 6,075 cycles for a 386DX-33. Scaling that published value to 40 MHz gives
7,364; the profile rounds slightly down to 7,350.

DOSBox-X is not cycle-accurate, so this is a repeatable regression profile rather
than a substitute for testing every combination of real 386 motherboard and VGA
card. The threshold deliberately leaves rendering headroom below the measured
profile result.

## Target-side workload

`KOLOBOK.EXE -benchmark` uses the DOS runtime clock to measure 60 representative
Deep Forest frames whose camera positions traverse the complete 160-tile level. Every frame performs
a gameplay update, direct-to-VRAM background and HUD rendering, visible terrain
and entity rasterization, and a hardware page flip.

It reports two timing measurements:

- raw throughput, which exposes rendering regressions;
- the same workload under the game's fixed 30 Hz scheduler, which detects missed
  frame deadlines and pacing regressions.

It then uses PIT channel 2 as a 1,193,182 Hz target-side profiler and reports
separate totals for background/cottage, terrain tiles, sprites, HUD, and VGA
presentation. This avoids the DOS runtime clock's coarse tick granularity for
individual renderer stages.

`make perf-test` fails below 50.0 raw frames/s or 29.5 paced frames/s, or if any
profile stage is absent or zero. The raw gate leaves substantial headroom above
the 30 Hz gameplay deadline without baking one host's exact result into the test.

## Measured results

The Deep Forest worst case measures 57.4 fps raw and 30.3 fps paced with
DOSBox-X 2026.01.02 and the pinned Open Watcom v2 2026-08-01 daily build. That
is 91% above the 30 Hz frame deadline and 14.8% above the raw regression floor
while drawing the larger enemy set, three tree types, expanded HUD, and material
art.

CI reproduces the 57.4 fps figure exactly on `ubuntu-latest`, even though the
runner installs an older DOSBox-X than the figure was recorded with. The same
binary measures about 54.5 fps on an Apple Silicon host with a Homebrew
DOSBox-X, so treat differences of a few frames per second between hosts as
emulator variance rather than as a rendering change; the per-stage profile
counters are the reliable comparison.

The representative 60-frame expanded profile, measured on the macOS arm64 host
against the current renderer, reports 454,318 background ticks, 417,439 tile
ticks, 175,844 sprite ticks, 160,707 HUD ticks, and 1,051 presentation ticks.
That averages about 6.35 ms, 5.83 ms, 2.46 ms, 2.25 ms, and 0.015 ms per frame,
respectively. Game simulation, loop overhead, and profiler reads are outside or
between those buckets.

## Optimized paths

- Unchained 320×200×256 Mode X with two game pages and a title template page.
- Direct planar VRAM rendering; there is no whole-frame copy from conventional
  memory.
- CRTC display-start page flipping plus 0–3 pixel attribute-controller panning.
- Pel-panning writes retain the attribute controller's display-enable bit, avoiding
  a transient blank during every page flip.
- The start address is armed before the retrace that latches it and the retrace
  wait follows, so `video_present` returns only once the new page is on screen
  and the page it vacated is safe to draw into. Pel pan is written inside that
  same blanking interval, so base and pan always take effect on one frame.
- VGA-resident planar tile patterns copied with write-mode-1 latches.
- Generic opaque-span sprites for clipped edges and 16 alignment/plane-specific
  RLE span streams per sprite for the common fully visible path.
- Paragraph-aligned far-memory resource banks and segment-aware assembly
  blitters keep the active bank out of the program's near-data segment.
- Four cached, panning-aware static HUD variants; HP, lives, red count and blue
  boost time are drawn dynamically.
- A VGA-resident title template copied to the hidden game page before menu or
  codeword overlays are drawn; all UI updates reach the screen through the same
  vblank-synchronized page flip as gameplay.
- Precomputed tree/cloud origins and tree variants remove division and modulo
  from the per-frame background loop.
- Plane masks combine unaligned rectangle edge pixels into one vertical pass.
- Millisecond-clock frame scheduling instead of counting retraces that may have
  elapsed while a frame was rendering.

Mode X organization and latch-copy behavior follow Michael Abrash's discussion
of unchained VGA and page flipping in the
[Graphics Programming Black Book](https://www.phatcode.net/res/224/files/html/ch47/47-02.html).

## Presentation timing caveats

Three known limitations sit around the presentation path. None of them currently
produce a visible artifact, but each will mislead anyone measuring or debugging
frame timing.

- Frame pacing is coarser than it looks. `wait_for_frame` (`src/main.cpp:45`)
  paces on `clock()`, and the Watcom DOS headers define `CLOCKS_PER_SEC` as 1000
  while the DOS runtime derives the value from the 18.2 Hz BIOS tick. The unit is
  milliseconds but the real resolution is about 55 ms against a 33.3 ms deadline,
  so deadlines are overshot in a repeating pattern and roughly one frame in three
  is released with no wait. It still averages to the 30.3 fps the benchmark
  reports. The 55 ms figure is inferred from the tick source, not measured on
  target.
- The visible-page assertions cannot catch a late CRTC latch. `video_vram_crc`
  (`src/video.cpp`) reconstructs the page named by the `display_base` variable —
  the software's belief about what is on screen, not what the CRTC is scanning.
  Catching a late latch needs a read-back of the CRTC start address after a flip,
  or a comparison against a DOSBox-X frame capture.
- `output=surface` in `dosbox-x.conf` gives no host-side vsync, so DOSBox-X's own
  blit to the host window can tear independently of anything the game does. That
  shows up as a thin horizontal tear line rather than as element flicker;
  `output=opengl` with `vsync=true` rules it out.

## Memory use

The v4 archive is about 88 KiB, but each indexed active bank is about 22 KiB and
stays below the enforced 60 KiB limit. The game and editor executables are about
48 KiB and 49 KiB. The
renderer allocates one 64,000-byte conventional-memory scratch image for CRC test
readback; normal drawing does not use it as a back buffer. Together with the
loaded program, assets, game state, and stack, active conventional-memory data is
well below 200 KiB. The game uses an explicit 8 KiB stack and the editor a
16 KiB stack after expansion of the level and campaign state. Future caches may
deliberately use more conventional memory
when profiling shows a useful gain; 400 KiB is not treated as a hard ceiling.

VGA memory is the tighter fixed resource. The two 16,400-address game pages,
title template, four HUD variants, and eleven tile patterns occupy 57,776
addresses in each of four planes, or 231,104 of the VGA's 262,144 bytes. The
remaining VGA space is intentionally left available for small future caches.
