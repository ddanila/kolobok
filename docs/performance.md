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

Measured with DOSBox-X 2026.01.02 and the pinned Open Watcom v2 2026-08-01 daily
build:

| Renderer and profile | Raw throughput | 30 Hz paced |
| --- | ---: | ---: |
| Original C renderer, 386DX-40 profile | 17.0 fps | not sustainable |
| Mode 13h optimized framebuffer, 386DX-40 profile | 45.4–45.5 fps | 30.3 fps |
| MVP planar Mode X renderer | 68.2 fps | 30.3 fps |
| Expanded Deep Forest worst case | 57.4 fps | 30.3 fps |

The expanded worst-case workload remains 91% above its 30 Hz frame deadline and
14.8% above the raw regression floor while drawing the larger enemy set, three
tree types, expanded HUD, and material art.

The representative 60-frame expanded profile measured 454,274 background ticks,
411,536 tile ticks, 175,859 sprite ticks, 160,722 HUD ticks, and 997 presentation
ticks. That averages about 6.35 ms, 5.75 ms, 2.46 ms, 2.25 ms, and 0.014 ms per frame,
respectively. Game simulation, loop overhead, and profiler reads are outside or
between those buckets.

## Optimized paths

- Unchained 320×200×256 Mode X with two game pages and a title template page.
- Direct planar VRAM rendering; there is no whole-frame copy from conventional
  memory.
- CRTC display-start page flipping plus 0–3 pixel attribute-controller panning.
- Pel-panning writes retain the attribute controller's display-enable bit, avoiding
  a transient blank during every page flip.
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

## Memory use

The v4 archive is about 88 KiB, but each indexed active bank is about 22 KiB and
stays below the enforced 60 KiB limit. The game and editor executables are about
47 KiB and 49 KiB. The
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
