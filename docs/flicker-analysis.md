# Residual Mode X flicker: candidate causes

Flicker is still visible under DOSBox-X after commit 1f82862 ("Eliminate Mode X
visible-page flicker"), which made the draw page derive from `display_base`
instead of a free-running toggle.

This document records candidate explanations and how to test each one.

## Observation of 2026-08-05

On the DOSBox-X title screen the title artwork is stable while the menu box and
its three items flicker. That single observation is strong evidence for
hypothesis 1 below, and it invalidates the discriminating test this document
originally proposed.

Note first that `video_render_title` is declared in `video.h` but never called;
`UI_TITLE` renders the menu on every tick (`src/main.c:185`). "Title screen" and
"menu screen" are therefore the same frame, and the report distinguishes two
regions of it rather than two screens.

Each menu frame starts with `latch_copy(TITLE_PAGE, draw_base, PAGE_SIZE)` in
`begin_title_frame` (`src/video.c:709`), which stamps the cached template over
the whole page. In the title-artwork region that copy writes *the same pixel
values that are already there*, so it is idempotent and invisible even if it
lands on the page currently being scanned. That region is structurally incapable
of revealing a page-flip fault.

The menu region is the only part that changes. There the same copy erases last
frame's menu back to bare template, then `fill_rect` repaints the box and
`draw_text` repaints the items. Seen on the displayed page, that reads as
menu → bare template → box → text, i.e. exactly the observed blink.

So the drawing *is* landing on the visible page. The original suggestion that a
stable title screen would exonerate hypothesis 1 was wrong, because it assumed
the title region could show the fault.

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
- `display_base` is updated immediately, so during that late refresh
  `begin_hidden_frame` (`src/video.c:681`) would select the page the CRTC is
  *still scanning*. This only bites when the next frame starts drawing inside
  the late window; see hypothesis 3 for why that happens on some frames.
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

This is not purely an emulator artifact. Real VGA hardware also latches the
start address at the onset of vertical retrace, so writing it after a retrace
poll misses the same latch on a real 386. Reordering is therefore the correct
fix on hardware as well, not a DOSBox-X workaround.

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

Hypotheses 1 and 3 are not independent. Hypothesis 1 alone would mostly hide
itself at 30 Hz: the game frame period is 33.3 ms while the Mode X refresh
period is 14.3 ms (70 Hz, set by the 25.175 MHz clock select in `set_mode_x`),
so a flip that lands one refresh late has still taken effect by the time the
next frame starts drawing, and the nominally hidden page really is hidden. A
second mechanism is needed to explain why the draw sometimes begins before the
late flip completes.

`wait_for_frame` (`src/main.c:16`) supplies it. It paces on `clock()`, and the
Watcom DOS headers define `CLOCKS_PER_SEC` as 1000 while the DOS runtime derives
the value from the 18.2 Hz BIOS tick. The unit is milliseconds but the real
resolution is about 55 ms, so the counter advances in 55 ms jumps against a
33.3 ms deadline. Deadlines are therefore overshot in a repeating pattern and
roughly one frame in three is released immediately with no wait, which still
averages to the 30.3 fps the benchmark reports.

A frame released immediately begins drawing at the same retrace where the
previous `video_present` programmed the flip — before that flip takes effect
under hypothesis 1 — so it draws into the page still being scanned. That
predicts flicker on about 10 frames per second, intermittent rather than
constant, which matches the observation.

The 55 ms figure is the one link in this chain not yet verified on target;
`CLOCKS_PER_SEC == 1000` is confirmed from the pinned toolchain headers, but the
underlying tick source has been inferred rather than measured.

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

1. ~~Check whether the title screen flickers.~~ Done, and the test as written
   was invalid: the title region is redrawn idempotently and cannot show the
   fault. See the observation above. Its useful replacement is the pause-screen
   comparison in step 2.
2. **Compare the pause screen against the menu.** `video_render_pause`
   (`src/video.c:768`) returns early when `render_state` is already
   `RENDER_PAUSE`, so it draws once and then stops queueing flips entirely,
   whereas the menu redraws and flips every tick. Both are static screens, so if
   pause is rock stable while the menu blinks, the only remaining difference is
   whether anything is being drawn at all — which places the fault in drawing
   reaching the visible page rather than in the flip mechanics themselves.
3. **Invert the order in `video_present`:** write the start address first, then
   `wait_vblank`, then the pel pan. If flicker disappears, hypothesis 1 is
   confirmed and hypothesis 2 is resolved by the same change.
4. **Instrument the pacing.** Log the delta between successive `wait_for_frame`
   releases to confirm or reject the 55 ms burst pattern of hypothesis 3, which
   is the one unverified link in the chain.
5. **Try `output=opengl` with host vsync** to separate a DOSBox-X presentation
   artifact from a guest-side page-flip artifact.
6. **Skip redundant UI flips** by tracking whether the menu content actually
   changed before queueing a flip. This would mask menu flicker without fixing
   gameplay flicker, so it is a cleanup rather than a fix, and it should not be
   applied before step 3 or it will hide the evidence.

Steps 2 and 5 are observation or configuration only. Step 3 is the single change
most likely to resolve the issue and should be measured with `make perf-test`
afterwards, since moving the retrace wait changes where the frame budget is
spent.
