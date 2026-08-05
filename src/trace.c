#include "trace.h"

#ifdef KOLO_TRACE

#include <stdarg.h>
#include <stdio.h>

/* Sixty-four entries is about a minute of history at one heartbeat per second,
 * which comfortably covers the run-up to a failure. The ring is 4 KiB, and it
 * only exists in a traced build, so the small-model DGROUP of the shipping
 * executable is untouched. */
#define TRACE_ENTRIES 64
#define TRACE_TEXT 64

static char ring[TRACE_ENTRIES][TRACE_TEXT];
static unsigned next_slot;
static unsigned used;
static unsigned dropped;

void kolo_trace_reset(void)
{
    next_slot = 0;
    used = 0;
    dropped = 0;
}

void kolo_trace_record(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(ring[next_slot], TRACE_TEXT, format, args);
    va_end(args);
    ring[next_slot][TRACE_TEXT - 1] = '\0';
    next_slot = (next_slot + 1) % TRACE_ENTRIES;
    if (used < TRACE_ENTRIES) ++used;
    else ++dropped;
}

void kolo_trace_dump(const char *reason)
{
    unsigned i;
    unsigned start = (next_slot + TRACE_ENTRIES - used) % TRACE_ENTRIES;
    printf("KOLOBOK TRACE %s: %u entries", reason ? reason : "dump", used);
    if (dropped) printf(", %u older dropped", dropped);
    printf("\n");
    for (i = 0; i < used; ++i)
        printf("  %s\n", ring[(start + i) % TRACE_ENTRIES]);
}

#else

/* ISO C forbids an empty translation unit, and this file is compiled into every
 * target so that enabling the trace never requires touching a build script. */
typedef int kolo_trace_disabled;

#endif
