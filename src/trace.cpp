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

#if defined(__DOS__)
#include <conio.h>

/* DOSBox-X's integration device, a guest-to-host channel documented in the
 * emulator's include/iglib.h and implemented client-side in DOSLIB's
 * hw/dosboxid. Streaming each record to it has two advantages over the ring:
 * the history is unbounded, and it survives a guest that hangs or dies without
 * ever reaching the code that would have dumped the ring.
 *
 * The device is absent unless dosbox-x.conf sets "integration device = true",
 * and on real hardware ports 0x28-0x2B are something else entirely, so nothing
 * is written until the documented reset-and-identify handshake succeeds. This
 * code only exists in a traced build, which is never shipped. */
#define ID_BASE                 0x28U
#define ID_INDEX                (ID_BASE + 0U)
#define ID_DATA                 (ID_BASE + 1U)
#define ID_COMMAND              (ID_BASE + 2U)

#define ID_CMD_RESET_LATCH      0x00U
#define ID_CMD_FLUSH_WRITE      0x01U
#define ID_CMD_RESET_INTERFACE  0xFFU

#define ID_RESET_DATA_CODE      0x0D05B0C5UL
#define ID_RESET_INDEX_CODE     0xAA55BB66UL
#define ID_IDENTIFICATION       0xD05B0740UL
#define ID_REG_IDENTIFY         0x00000000UL
#define ID_REG_DEBUG_OUT        0x0000DEB0UL

/* -1 not probed yet, 0 absent, 1 present. */
static int id_state = -1;

static void id_reset_latch(void)
{
    outp(ID_COMMAND, ID_CMD_RESET_LATCH);
}

/* The 16-bit target has no 32-bit port access, so every dword is two words. */
static unsigned long id_read_dword(unsigned port)
{
    unsigned long value = (unsigned long)inpw(port);
    value |= (unsigned long)inpw(port) << 16UL;
    return value;
}

static void id_write_regsel(unsigned long reg)
{
    id_reset_latch();
    outpw(ID_INDEX, (unsigned)(reg & 0xffffUL));
    outpw(ID_INDEX, (unsigned)(reg >> 16UL));
}

static int id_probe(void)
{
    unsigned long data, index;
    outp(ID_COMMAND, ID_CMD_RESET_INTERFACE);
    id_reset_latch();
    data = id_read_dword(ID_DATA);
    id_reset_latch();
    index = id_read_dword(ID_INDEX);
    if (data != ID_RESET_DATA_CODE || index != ID_RESET_INDEX_CODE) return 0;
    id_write_regsel(ID_REG_IDENTIFY);
    id_reset_latch();
    return id_read_dword(ID_DATA) == ID_IDENTIFICATION;
}

static void id_debug_message(const char *text)
{
    if (id_state < 0) id_state = id_probe();
    if (id_state <= 0) return;
    id_write_regsel(ID_REG_DEBUG_OUT);
    id_reset_latch();
    while (*text != '\0') outp(ID_DATA, (unsigned char)*text++);
    /* The host accumulates characters and only emits a log line when it sees a
     * newline, so without this every record runs into the next one and the log
     * breaks mid-token wherever the host buffer happened to fill. */
    outp(ID_DATA, (unsigned char)'\n');
    outp(ID_COMMAND, ID_CMD_FLUSH_WRITE);
}

int trace_host_channel(void)
{
    if (id_state < 0) id_state = id_probe();
    return id_state > 0;
}

#else /* the host test build has no I/O ports; the ring is the only sink */

static void id_debug_message(const char *text) { (void)text; }

int trace_host_channel(void) { return 0; }

#endif

void trace_reset(void)
{
    next_slot = 0;
    used = 0;
    dropped = 0;
}

void trace_record(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(ring[next_slot], TRACE_TEXT, format, args);
    va_end(args);
    ring[next_slot][TRACE_TEXT - 1] = '\0';
    id_debug_message(ring[next_slot]);
    next_slot = (next_slot + 1) % TRACE_ENTRIES;
    if (used < TRACE_ENTRIES) ++used;
    else ++dropped;
}

void trace_dump(const char *reason)
{
    unsigned i;
    unsigned start = (next_slot + TRACE_ENTRIES - used) % TRACE_ENTRIES;
    printf("KOLOBOK TRACE %s: %u entries", reason ? reason : "dump", used);
    if (dropped) printf(", %u older dropped", dropped);
    if (trace_host_channel()) printf(", also streamed to the emulator log");
    printf("\n");
    for (i = 0; i < used; ++i)
        printf("  %s\n", ring[(start + i) % TRACE_ENTRIES]);
}

#else

/* ISO C forbids an empty translation unit, and this file is compiled into every
 * target so that enabling the trace never requires touching a build script. */
typedef int trace_disabled;

#endif
