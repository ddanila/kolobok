#ifndef KOLO_TRACE_H
#define KOLO_TRACE_H

/* Ring-buffer diagnostics that vanish unless the build defines KOLO_TRACE.
 *
 * Records land in a fixed ring and are printed only when something fails, so an
 * enabled build performs no I/O during a run and a disabled build costs nothing
 * at all: every entry point becomes a void expression and no symbol is emitted.
 * That matters here because the same source feeds the 386DX-40 performance gate.
 *
 * Call it with double parentheses, because the 16-bit Watcom target predates
 * variadic macros:
 *
 *     KOLO_LOG(("stuck x=%d y=%d", x, y));
 *
 * One hazard worth remembering: a variable that is read only inside KOLO_LOG
 * becomes unused when tracing is off, and the host test build compiles with
 * -Werror. Log values the surrounding code already uses, or the release build
 * stops compiling.
 */

#ifdef KOLO_TRACE

void kolo_trace_reset(void);
void kolo_trace_record(const char *format, ...);
void kolo_trace_dump(const char *reason);

/* Non-zero when DOSBox-X's integration device answered its handshake, in which
 * case every record is also streamed to the emulator log as it happens. That
 * history is unbounded and survives a guest that hangs before it can dump. */
int kolo_trace_host_channel(void);

#define KOLO_LOG(args) kolo_trace_record args

#else

#define kolo_trace_reset() ((void)0)
#define kolo_trace_dump(reason) ((void)0)
#define kolo_trace_host_channel() (0)
#define KOLO_LOG(args) ((void)0)

#endif

#endif
