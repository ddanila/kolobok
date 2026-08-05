# Tracing

`src/trace.h` provides a ring-buffer log that is compiled out of the shipping
build. It exists because the DOS-native failures in this project are reported as
a single line of final state, and reconstructing how the game arrived there by
reasoning alone has repeatedly been wrong.

## Using it

```sh
make dos-debug      # same executables, built with -dKOLO_TRACE
make playtest       # a failing stage now prints its trace
make dos            # back to a shipping build
```

`make dos-debug` overwrites `build/KOLOBOK.EXE`, so rebuild with `make dos`
before measuring performance or packaging a release.

## Writing records

```c
KOLO_LOG(("stuck x=%d y=%d", x, y));
```

The double parentheses are not a style choice. The 16-bit Watcom target predates
variadic macros, so the argument list has to arrive as a single macro argument.

Two rules keep the shipping build working:

- Log values the surrounding code already uses. A variable read *only* inside
  `KOLO_LOG` becomes unused once tracing is off, and the host tests compile with
  `-Werror`, so the release build stops compiling.
- Keep a record under 64 characters. Longer text is truncated, not overflowed;
  `vsnprintf` bounds every write.

## Why a ring rather than printing

Printing each event as it happens would put DOS console I/O inside the frame
loop, which is slow under emulation and would bury a failure in thousands of
lines. Records instead go into a 64-entry ring, and `kolo_trace_dump` prints it
only when something fails. The interesting history is the last minute before the
failure, which is exactly what the ring holds. The dump reports how many older
entries were overwritten so a truncated history is never mistaken for a complete
one.

## What is instrumented

The campaign playtest emits a heartbeat every 30 frames with position, ground
contact, current target, berries and HP, plus discrete records when it answers a
guardian and when it gives up on a pickup. `game_lose_life` records where a life
was lost. A failing stage dumps the ring right after its `PLAYTEST FAIL` line.

A real dump, from a run whose frame budget was cut short deliberately:

```
KOLOBOK TRACE playtest stage failed: 34 entries
  stage 0 start w=96 need=6 hp=100 lv=3
  f=0 x=48 y=130 g=1 to=324 red=0 hp=100
  f=54 talk enc=0 answer=1
  f=120 x=318 y=116 g=0 to=356 red=1 hp=100
  f=300 x=378 y=108 g=0 to=356 red=2 hp=90
  death x=401 y=146 lv=3
  f=330 x=51 y=130 g=1 to=356 red=2 hp=100
  f=594 give up 6 at=(22,8)
```

The death at x=401 lands inside the Garden pit at px 400..415, and the give-up
names the exact pickup — both facts that previously took a hand-written printf
and another emulator run to establish.

## Cost when disabled

None. `KOLO_LOG`, `kolo_trace_reset` and `kolo_trace_dump` become void
expressions, no symbols are emitted, and `trace.c` collapses to a single typedef
because ISO C forbids an empty translation unit. Building the same source before
and after this feature produced byte-identical executables, so the 386DX-40
performance gate is measuring the same code it always was.
