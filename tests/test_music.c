#include "music.h"

#include <assert.h>
#include <stdio.h>

#define TRACK_COUNT 4
#define TICKS_PER_TRACK 192
#define ALL_SIX_VOICES 0x3f
#define BLOCK_KEYON_FIRST 0xb0
#define BLOCK_KEYON_LAST 0xb5
#define KEY_ON_BIT 0x20

#define FNV_OFFSET 2166136261UL
#define FNV_PRIME 16777619UL

/* Stands in for the OPL2 so the sequencer can be exercised on the host. The hash
 * fingerprints the whole register stream, which is what distinguishes one
 * arrangement from another without pinning down individual writes. */
typedef struct MockOPL {
    unsigned writes, key_on, key_off;
    u32 hash;
    u8 voices;
} MockOPL;

static void sink(u8 reg, u8 value, void *context)
{
    MockOPL *mock = (MockOPL *)context;
    ++mock->writes;
    mock->hash = (mock->hash ^ reg) * FNV_PRIME;
    mock->hash = (mock->hash ^ value) * FNV_PRIME;
    if (reg < BLOCK_KEYON_FIRST || reg > BLOCK_KEYON_LAST) return;
    if (value & KEY_ON_BIT) {
        ++mock->key_on;
        mock->voices |= (u8)(1U << (reg - BLOCK_KEYON_FIRST));
    } else {
        ++mock->key_off;
    }
}

static void reset(MockOPL *mock)
{
    mock->writes = mock->key_on = mock->key_off = 0;
    mock->hash = FNV_OFFSET;
    mock->voices = 0;
}

/* Plays one track for long enough to loop its 16 steps several times and returns
 * the register-stream fingerprint. */
static u32 trace_track(MockOPL *mock, unsigned track)
{
    unsigned i;
    reset(mock);
    music_play(track);
    for (i = 0; i < TICKS_PER_TRACK; ++i) music_tick();
    assert(music_debug_ticks() == TICKS_PER_TRACK);
    assert(music_debug_events() > 40);
    assert(music_debug_voice_mask() == ALL_SIX_VOICES);
    assert(mock->writes > 200);
    assert(mock->key_on > 40);
    assert(mock->key_off > 40);
    assert(mock->voices == ALL_SIX_VOICES);
    return mock->hash;
}

static void test_every_track_is_distinct(MockOPL *mock)
{
    static const unsigned tracks[TRACK_COUNT] = {
        MUSIC_GARDEN, MUSIC_FOREST, MUSIC_DEEP, MUSIC_HOME
    };
    u32 hashes[TRACK_COUNT];
    unsigned i, j;
    for (i = 0; i < TRACK_COUNT; ++i) hashes[i] = trace_track(mock, tracks[i]);
    for (i = 0; i < TRACK_COUNT; ++i)
        for (j = 0; j < i; ++j) assert(hashes[i] != hashes[j]);
}

static void test_muting_stops_the_sequencer(void)
{
    music_set_enabled(0);
    assert(!music_is_enabled());
    music_tick();
    assert(music_debug_ticks() == TICKS_PER_TRACK);
    music_set_enabled(1);
    assert(music_is_enabled());
}

int main(void)
{
    MockOPL mock;
    reset(&mock);
    music_set_sink(sink, &mock);
    assert(music_init(1));
    assert(music_is_detected() && music_is_enabled());
    test_every_track_is_distinct(&mock);
    test_muting_stops_the_sequencer();
    music_shutdown();
    puts("host AdLib arrangement and sequencer tests: PASS");
    return 0;
}
