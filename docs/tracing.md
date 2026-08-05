# Tracing

`src/trace.h` provides a ring-buffer log that is compiled out of the shipping
build. It exists because the DOS-native failures in this project are reported as
a single line of final state, and reconstructing how the game arrived there by
reasoning alone has repeatedly been wrong.

## Using it

```sh
make dos-debug        # same executables, built with -dKOLO_TRACE
make playtest         # a failing stage now prints its trace
make trace-playtest   # also streams every record to the emulator log
make dos              # back to a shipping build
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

## Streaming to the emulator

DOSBox-X exposes an integration I/O device at ports 0x28..0x2B, a guest-to-host
channel whose register set includes `DOSBOX_ID_REG_DEBUG_OUT` (0xDEB0). When it
is present, every record is also pushed there as it happens and appears in the
emulator log as `Client debug message: ...`.

```sh
make trace-playtest     # traced build + dosbox-x-debug.conf
grep "Client debug message" build/PLAY-EMU.LOG
```

This is not about speed — the ring already performs no I/O during a run. It buys
two things the ring cannot:

- **History that survives a hang.** The ring is only printed if execution
  reaches the code that dumps it. A guest stuck in a loop, or killed by the
  emulator's `-time-limit`, takes its ring with it. Streamed records are already
  in the host log.
- **No cap.** A full three-level playthrough streams around 270 records; the
  ring holds the last 64.

Three details worth knowing:

- The device is disabled by default, which is why `dosbox-x-debug.conf` exists
  and `dosbox-x.conf` is left alone. The performance gate must keep measuring an
  unmodified machine, and DOSBox-X's own configuration calls this device
  experimental.
- Records end with a newline because the host accumulates characters and only
  emits a log line when it sees one. Without it the log still works, but records
  run together and break mid-token wherever the host buffer filled.
- Nothing is written until the documented reset-and-identify handshake succeeds,
  because on real hardware those ports are something else entirely. If the
  handshake fails the trace silently falls back to ring-only, which is what
  happens when a traced build is run against `dosbox-x.conf`.

## Why a ring rather than printing

Printing each event as it happens would put DOS console I/O inside the frame
loop, which is slow under emulation and would bury a failure in thousands of
lines. Records instead go into a 64-entry ring, and `trace_dump` prints it
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

None. `KOLO_LOG`, `trace_reset` and `trace_dump` become void
expressions, no symbols are emitted, and `trace.cpp` collapses to a single typedef
because ISO C forbids an empty translation unit. Building the same source before
and after this feature produced byte-identical executables, so the 386DX-40
performance gate is measuring the same code it always was.
