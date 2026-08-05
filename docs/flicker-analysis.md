# Residual Mode X flicker: candidate causes

Flicker is still visible under DOSBox-X after commit 1f82862 ("Eliminate Mode X
visible-page flicker"), which made the draw page derive from `display_base`
instead of a free-running toggle.

This document records candidate explanations and how to test each one. Nothing
here is confirmed; it is a starting point for the next debugging session rather
than a changelog of fixes.

## 1. The flip is programmed after the CRTC latch, not before

`video_present` (`src/video.c:664`) waits for retrace and *then* writes the new
display start:

```
wait_vblank();
set_display_start(pending_base, pending_pan);
```

`wait_vblank` (`src/video.c:205`) returns on the *leading* edge of vertical
retrace — it spins until bit 3 of port 0x3DA goes high. DOSBox-X latches the
CRTC display-start address on the same event that raises that bit, so by the
time the poll observes it the latch has already happened. The write to CRTC
0x0C/0x0D therefore lands just after the latch and does not take effect until
the *following* retrace.

If that is what happens, the consequence is deterministic rather than a race:

- The flip becomes visible one full refresh (~14.3 ms at 70 Hz) later than the
  code assumes.
- `display_base` is updated immediately, so the next `begin_hidden_frame`
  (`src/video.c:681`) selects the page the CRTC is *still scanning* during that
  late refresh.
- A frame takes roughly 17 ms to draw (6.35 ms background, 5.75 ms tiles,
  2.46 ms sprites, 2.25 ms HUD — see `performance.md`), so the background fill,
  terrain, and sprites reach the visible page in stages. Sky flashing through
  before tiles arrive is the expected symptom.

Commit 1f82862 corrected the software's bookkeeping, but the emulated hardware
flip would still be one frame behind that bookkeeping, so the nominally hidden
page is the on-screen page for the duration of the draw.

The ordering in Abrash's page-flipping discussion is the reverse: program the
new start address as soon as the frame is finished (it is latched at the next
retrace regardless of when it is written during active display), *then* wait for
retrace to confirm the flip completed, *then* write pel pan and begin the next
frame.

## 2. Start address and pel pan take effect on different refreshes

`set_display_start` (`src/video.c:196`) writes the CRTC start address and the
attribute-controller pel pan back to back. Under hypothesis 1 the start address
applies at the next retrace while the AC pel-pan write applies immediately, so
for one refresh the screen shows the old page base combined with the new pan.

Every time the camera crosses a 4-pixel boundary the pan wraps 3→0 as the base
advances, which would produce a one-frame 4-pixel horizontal jump. That is
separate from hypothesis 1 and would read as scrolling jitter or shimmer rather
than as element flicker.

## 3. Coarse `clock()` granularity makes pacing bursty

`wait_for_frame` (`src/main.c:16`) paces frames on `clock()`. Under the Watcom
DOS runtime `clock()` derives from the 18.2 Hz BIOS tick, so its real resolution
is about 55 ms even though `CLOCKS_PER_SEC` implies finer granularity. The
scheduler would then release frames in bursts — roughly two back-to-back per
BIOS tick, averaging to the 30.3 fps the benchmark reports.

On a burst frame, rendering starts immediately after `video_present` returns,
i.e. during the refresh where the stale page is still being scanned, which
maximizes how much of the draw is visible. On other frames the draw may start
late enough to stay mostly hidden. This does not cause flicker on its own but
would modulate hypothesis 1 into the intermittent, irregular flicker observed.

## 4. Why the selftest does not catch any of this

`video_vram_crc` (`src/video.c:984`) reconstructs the page named by the
`display_base` *variable* — the software's belief about what is on screen, not
what the emulated CRTC is actually scanning. The visible-page assertions in
`selftest` (`src/main.c:65`) therefore validate the bookkeeping that 1f82862
already fixed and are blind to a late hardware latch.

A test that could catch it needs to read the CRTC start address back from the
hardware after a flip and confirm it changed within the same refresh, or compare
against a DOSBox-X frame capture rather than against reconstructed VRAM.

## 5. Minor contributors

- `output=surface` in `dosbox-x.conf` gives no host-side vsync, so DOSBox-X's
  own blit to the host window can tear independently of anything the game does.
  This produces a thin horizontal tear line, not element flicker. Worth ruling
  out by trying `output=opengl` with `vsync=true`.
- The title and menu paths re-render and flip on every tick even when nothing
  changed (`src/main.c:185`). Under hypothesis 1 that makes even a static menu
  shimmer.
- `machine=svga_s3` handles the Mode X CRTC tweaks correctly and is not
  suspected.

## Suggested order of evaluation

1. **Check whether the title screen flickers.** It has no scrolling and no
   camera panning, so flicker there points at hypothesis 1 and rules out
   hypothesis 2 as the primary cause. This is the cheapest discriminating test.
2. **Invert the order in `video_present`:** write the start address first, then
   `wait_vblank`, then the pel pan. If flicker disappears, hypothesis 1 is
   confirmed and hypothesis 2 is resolved by the same change.
3. **Instrument the pacing.** Log the delta between successive `wait_for_frame`
   releases to confirm or reject the 55 ms burst pattern of hypothesis 3.
4. **Try `output=opengl` with host vsync** to separate a DOSBox-X presentation
   artifact from a guest-side page-flip artifact.
5. **Skip redundant UI flips** by tracking whether the menu or title content
   actually changed before queueing a flip.

Steps 1 and 4 are configuration or observation only. Step 2 is the single change
most likely to resolve the issue and should be measured with `make perf-test`
afterwards, since moving the retrace wait changes where the frame budget is
spent.
