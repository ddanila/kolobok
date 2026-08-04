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
a gameplay update, background and HUD rendering, visible terrain and entity
rasterization, and the complete 64 KiB transfer to VGA memory.

It reports two measurements:

- raw throughput, which exposes rendering regressions;
- the same workload under the game's fixed 30 Hz scheduler, which detects missed
  frame deadlines and pacing regressions.

`make perf-test` fails below 36.0 raw frames/s or 29.5 paced frames/s. The raw gate
leaves 20% headroom above the 30 Hz gameplay deadline.

## Measured results

Measured with DOSBox-X 2026.01.02 and the pinned Open Watcom v2 2026-08-01 daily
build:

| Renderer and profile | Raw throughput | 30 Hz paced |
| --- | ---: | ---: |
| Original C renderer, 386DX-40 profile | 17.0 fps | not sustainable |
| Optimized renderer, 386DX-40 profile | 45.4–45.5 fps | 30.3 fps |
| Optimized renderer, 386DX-33 profile (6,075 cycles) | 39.0 fps | 30.3 fps |

The optimized renderer is about 2.68 times as fast while preserving reference
frame CRC `5BD3ECB5`.

## Optimized paths

- Paragraph-aligned DOS framebuffer with an offset of zero.
- `rep stosd` screen clears and assembly rectangle spans.
- Four `movsd` operations per opaque 16-pixel tile row.
- A compact transparent-sprite loop without per-pixel screen-bound checks.
- Immediate rejection of fully off-screen objects.
- `rep movsd` for the 64 KiB framebuffer-to-Mode-13h transfer.
- Millisecond-clock frame scheduling instead of counting retraces that may have
  elapsed while a frame was rendering.
