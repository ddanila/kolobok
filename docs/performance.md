# 386DX-40 performance target

Kolobok's performance regression runs inside the finished 16-bit DOS executable.
The Linux host cross-compiles it first; DOSBox-X only executes the result.

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
frames whose camera positions traverse the complete level. Every frame performs
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
| Planar Mode X direct renderer, 386DX-40 profile | 68.2 fps | 30.3 fps |

The final renderer is about 4.01 times as fast as the original and 50% faster than
the previous optimized Mode 13h path while preserving reference frame CRC
`8EF18BDB`.

The representative 60-frame profile measured 442,717 background ticks, 449,332
tile ticks, 107,565 sprite ticks, 44,675 HUD ticks, and 998 presentation ticks.
That averages about 6.18 ms, 6.28 ms, 1.50 ms, 0.62 ms, and 0.014 ms per frame,
respectively. Game simulation, loop overhead, and profiler reads are outside or
between those buckets.

## Optimized paths

- Unchained 320×200×256 Mode X with two game pages and a persistent title page.
- Direct planar VRAM rendering; there is no whole-frame copy from conventional
  memory.
- CRTC display-start page flipping plus 0–3 pixel attribute-controller panning.
- Pel-panning writes retain the attribute controller's display-enable bit, avoiding
  a transient blank during every page flip.
- VGA-resident planar tile patterns copied with write-mode-1 latches.
- Generic opaque-span sprites for clipped edges and 16 alignment/plane-specific
  RLE span streams per sprite for the common fully visible path.
- Four cached, panning-aware static HUD variants; only the collected count is
  drawn dynamically.
- A cached title screen whose blinking prompt is restored and redrawn as a dirty
  region; unchanged pause and victory frames are retained.
- Precomputed tree/cloud origins and tree variants remove division and modulo
  from the per-frame background loop.
- Plane masks combine unaligned rectangle edge pixels into one vertical pass.
- Millisecond-clock frame scheduling instead of counting retraces that may have
  elapsed while a frame was rendering.

Mode X organization and latch-copy behavior follow Michael Abrash's discussion
of unchained VGA and page flipping in the
[Graphics Programming Black Book](https://www.phatcode.net/res/224/files/html/ch47/47-02.html).

## Memory use

The current data pack is 13,355 bytes and the DOS executable is about 27 KiB. The
renderer allocates one 64,000-byte conventional-memory scratch image for CRC test
readback; normal drawing does not use it as a back buffer. Together with the
loaded program, assets, game state, and stack, active conventional-memory data is
well below 150 KiB. Future caches may deliberately use more conventional memory
when profiling shows a useful gain; 400 KiB is not treated as a hard ceiling.

VGA memory is the tighter fixed resource. The two 16,400-address game pages,
title page, four HUD variants, prompt backup, and tile patterns occupy 58,016
addresses in each of four planes, or 232,064 of the VGA's 262,144 bytes. The
remaining VGA space is intentionally left available for small future caches.
