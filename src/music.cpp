#include "music.h"

#ifdef __WATCOMC__
#include <conio.h>
#endif

#define REST 255
#define STEPS 16
#define VOICES 6
#define SCORE_COUNT 4

/* OPL2 register file. Operators are addressed by an offset table rather than
 * linearly, because channel n's two operators are not adjacent. */
#define OPL_TEST 0x01
#define OPL_TIMER1 0x02
#define OPL_TIMER_CONTROL 0x04
#define OPL_PERCUSSION 0x08
#define OPL_CHARACTERISTIC 0x20
#define OPL_LEVEL 0x40
#define OPL_ATTACK_DECAY 0x60
#define OPL_SUSTAIN_RELEASE 0x80
#define OPL_FNUM_LOW 0xa0
#define OPL_BLOCK_KEYON 0xb0
#define OPL_FEEDBACK 0xc0
#define OPL_WAVEFORM 0xe0
#define OPL_CARRIER_OFFSET 3
#define OPL_KEY_ON 0x20
#define OPL_BASE_OCTAVE 2
#define OPL_STATUS_PORT 0x388
#define OPL_DATA_PORT 0x389

/* The OPL2 needs settling time between writes: roughly 3.3us after an address
 * and 23us after data. Reading the status port is the usual portable delay. */
#define OPL_ADDRESS_DELAY_READS 6
#define OPL_DATA_DELAY_READS 35

typedef struct MusicStep { u8 ticks; u8 note[VOICES]; } MusicStep;
typedef struct OplInstrument {
    u8 mod_char, car_char, mod_level, car_level, attack, decay, wave, feedback;
} OplInstrument;

/* The lead contours derive from public-domain song 45 in Tchaikovsky's 1869
 * collection. Counterpoint, chords, bass, rhythm, form and timbres are original. */
static const MusicStep garden_score[STEPS] = {
 {4,{31,35,24,28,31,12}},{4,{33,36,24,28,31,12}},{4,{35,38,26,29,33,14}},{8,{36,40,24,28,31,12}},
 {4,{35,38,23,26,31,11}},{4,{33,36,21,24,28,9}},{8,{31,35,19,23,26,7}},{4,{28,31,21,24,28,9}},
 {4,{31,35,24,28,31,12}},{8,{33,36,26,29,33,14}},{8,{31,35,24,28,31,12}},{4,{28,31,21,24,28,9}},
 {4,{26,29,19,23,26,7}},{8,{24,28,17,21,24,5}},{4,{28,31,21,24,28,9}},{8,{24,28,17,21,24,0}}
};

static const MusicStep forest_score[STEPS] = {
 {6,{31,REST,24,28,REST,12}},{3,{REST,35,24,28,31,12}},{6,{33,REST,26,29,REST,14}},{3,{REST,36,26,29,33,14}},
 {6,{35,REST,23,26,31,11}},{3,{REST,38,23,26,REST,11}},{9,{36,33,21,24,28,9}},{6,{33,31,19,23,26,7}},
 {6,{31,REST,24,28,31,12}},{3,{REST,35,24,28,REST,12}},{6,{28,31,21,24,28,9}},{6,{31,33,23,26,31,11}},
 {6,{29,33,22,26,29,10}},{6,{26,29,19,23,26,7}},{6,{28,31,21,24,28,9}},{9,{24,28,17,21,24,0}}
};

static const MusicStep deep_score[STEPS] = {
 {6,{19,22,12,15,19,0}},{6,{21,24,14,17,21,2}},{6,{22,26,10,14,17,REST}},{10,{24,27,12,15,19,0}},
 {6,{22,26,11,14,19,REST}},{6,{21,24,9,12,16,REST}},{10,{19,22,7,10,14,REST}},{6,{16,19,9,12,16,REST}},
 {6,{19,22,12,15,19,0}},{8,{21,24,14,17,21,2}},{8,{19,22,11,14,19,REST}},{6,{16,19,9,12,16,REST}},
 {6,{14,17,7,10,14,REST}},{10,{12,15,5,8,12,REST}},{6,{11,14,4,7,11,REST}},{12,{12,19,5,8,12,0}}
};

static const MusicStep home_score[STEPS] = {
 {3,{31,43,24,28,31,12}},{3,{33,45,26,29,33,14}},{3,{35,47,28,31,35,16}},{6,{36,48,29,33,36,17}},
 {3,{38,47,31,35,38,19}},{3,{36,45,29,33,36,17}},{6,{35,43,28,31,35,16}},{3,{31,40,24,28,31,12}},
 {3,{33,45,26,29,33,14}},{6,{35,47,28,31,35,16}},{6,{36,48,29,33,36,17}},{3,{35,47,28,31,35,16}},
 {3,{33,45,26,29,33,14}},{6,{31,43,24,28,31,12}},{3,{35,47,28,31,35,16}},{9,{36,48,24,28,36,12}}
};

static const OplInstrument instruments[SCORE_COUNT][VOICES] = {
 {{0x21,0x01,0x16,0x03,0xf3,0x45,0,2},{0x21,0x01,0x20,0x08,0xe3,0x45,0,2},{0x01,0x01,0x28,0x0c,0xd4,0x56,0,4},{0x01,0x01,0x2c,0x10,0xd4,0x56,0,4},{0x01,0x01,0x30,0x12,0xc4,0x67,0,4},{0x21,0x01,0x24,0x08,0xf2,0x45,0,6}},
 {{0x61,0x21,0x1c,0x06,0xd4,0x56,1,2},{0x21,0x21,0x24,0x0c,0xc4,0x67,1,2},{0x01,0x01,0x2c,0x10,0xb5,0x78,0,4},{0x01,0x01,0x30,0x14,0xb5,0x78,0,4},{0x21,0x01,0x34,0x18,0xa5,0x89,0,4},{0x21,0x01,0x28,0x0a,0xd3,0x56,0,6}},
 {{0x21,0x01,0x12,0x04,0xf2,0x67,2,6},{0x21,0x01,0x1c,0x0a,0xe2,0x67,2,6},{0x01,0x01,0x24,0x0c,0xc3,0x89,0,4},{0x01,0x01,0x28,0x10,0xc3,0x89,0,4},{0x01,0x01,0x2c,0x14,0xb3,0x9a,0,4},{0x21,0x01,0x18,0x04,0xf1,0x67,0,7}},
 {{0x61,0x21,0x10,0x02,0xf4,0x34,1,2},{0x61,0x21,0x18,0x05,0xf4,0x34,1,2},{0x21,0x01,0x20,0x08,0xe4,0x45,0,4},{0x21,0x01,0x24,0x0a,0xe4,0x45,0,4},{0x21,0x01,0x28,0x0c,0xd4,0x56,0,4},{0x21,0x01,0x18,0x04,0xf3,0x34,0,6}}
};

/* Indexed by MUSIC_* track, so a track maps straight to its score and timbres. */
static const MusicStep *const scores[SCORE_COUNT] = {
    garden_score, forest_score, deep_score, home_score
};

static const u8 operator_offsets[VOICES] = {0, 1, 2, 8, 9, 10};

/* F-numbers for one chromatic octave at the default 49716 Hz sample rate; the
 * block field in OPL_BLOCK_KEYON transposes them by octave. */
static const u16 fnums[12] = {343, 363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647};

static MusicRegisterSink register_sink;
static void *sink_context;
static u8 enabled, detected, track_id, score_id, step_index, step_ticks;
static u32 debug_ticks, debug_events;
static u8 debug_voices;

static void write_reg(u8 reg, u8 value)
{
    if (register_sink) {
        register_sink(reg, value, sink_context);
        return;
    }
#ifdef __WATCOMC__
    {
        unsigned i;
        outp(OPL_STATUS_PORT, reg);
        for (i = 0; i < OPL_ADDRESS_DELAY_READS; ++i) (void)inp(OPL_STATUS_PORT);
        outp(OPL_DATA_PORT, value);
        for (i = 0; i < OPL_DATA_DELAY_READS; ++i) (void)inp(OPL_STATUS_PORT);
    }
#else
    (void)reg;
    (void)value;
#endif
}

static void write_operator(u8 base, u8 offset, u8 value)
{
    write_reg((u8)(base + offset), value);
}

/* Both operators of a channel share a setting for attack, decay and waveform. */
static void write_operator_pair(u8 base, u8 offset, u8 value)
{
    write_operator(base, offset, value);
    write_operator((u8)(base + OPL_CARRIER_OFFSET), offset, value);
}

/* Standard OPL2 presence test: reset the timers, latch the status register, then
 * start timer 1 and confirm the overflow bits appear only after it has run. Absent
 * hardware floats the status port and the two reads will not match this pattern. */
static bool detect_opl(void)
{
#ifdef __WATCOMC__
    unsigned i, before, after;
    write_reg(OPL_TIMER_CONTROL, 0x60);
    write_reg(OPL_TIMER_CONTROL, 0x80);
    before = inp(OPL_STATUS_PORT) & 0xe0;
    write_reg(OPL_TIMER1, 0xff);
    write_reg(OPL_TIMER_CONTROL, 0x21);
    for (i = 0; i < 100; ++i) (void)inp(OPL_STATUS_PORT);
    after = inp(OPL_STATUS_PORT) & 0xe0;
    write_reg(OPL_TIMER_CONTROL, 0x60);
    write_reg(OPL_TIMER_CONTROL, 0x80);
    return before == 0 && after == 0xc0;
#else
    /* The host test build has no ports; a registered sink stands in for hardware. */
    return register_sink != 0;
#endif
}

static void silence_voices(void)
{
    unsigned voice;
    for (voice = 0; voice < VOICES; ++voice)
        write_reg((u8)(OPL_BLOCK_KEYON + voice), 0);
}

static void program_instruments(unsigned score)
{
    unsigned voice;
    for (voice = 0; voice < VOICES; ++voice) {
        u8 offset = operator_offsets[voice];
        const OplInstrument *instrument = &instruments[score][voice];
        write_operator(OPL_CHARACTERISTIC, offset, instrument->mod_char);
        write_operator((u8)(OPL_CHARACTERISTIC + OPL_CARRIER_OFFSET), offset,
                       instrument->car_char);
        write_operator(OPL_LEVEL, offset, instrument->mod_level);
        write_operator((u8)(OPL_LEVEL + OPL_CARRIER_OFFSET), offset,
                       instrument->car_level);
        write_operator_pair(OPL_ATTACK_DECAY, offset, instrument->attack);
        write_operator_pair(OPL_SUSTAIN_RELEASE, offset, instrument->decay);
        write_operator_pair(OPL_WAVEFORM, offset, instrument->wave);
        write_reg((u8)(OPL_FEEDBACK + voice), instrument->feedback);
    }
}

static void note_on(unsigned voice, unsigned note)
{
    unsigned octave = OPL_BASE_OCTAVE + note / 12;
    u16 fnum = fnums[note % 12];
    write_reg((u8)(OPL_FNUM_LOW + voice), (u8)fnum);
    write_reg((u8)(OPL_BLOCK_KEYON + voice),
              (u8)(OPL_KEY_ON | ((octave & 7) << 2) | (fnum >> 8)));
    ++debug_events;
    debug_voices |= (u8)(1U << voice);
}

void music_set_sink(MusicRegisterSink sink, void *context)
{
    register_sink = sink;
    sink_context = context;
}

bool music_init(bool requested)
{
    detected = (u8)(requested && detect_opl());
    enabled = detected;
    if (!detected) return false;
    write_reg(OPL_TEST, 0x20);
    write_reg(OPL_PERCUSSION, 0);
    music_play(Track::TITLE);
    return true;
}

void music_shutdown(void)
{
    if (detected) silence_voices();
    enabled = 0;
}

void music_set_enabled(bool value)
{
    enabled = (u8)(value && detected);
    if (!enabled) silence_voices();
    else music_play(track_id);
}

bool music_is_enabled(void) { return enabled != 0; }
bool music_is_detected(void) { return detected != 0; }

static u8 score_for_track(unsigned track)
{
    if (track == Track::FOREST) return 1;
    if (track == Track::DEEP) return 2;
    if (track == Track::HOME) return 3;
    return 0;
}

void music_play(unsigned track)
{
    track_id = (u8)track;
    score_id = score_for_track(track);
    step_index = step_ticks = 0;
    debug_ticks = debug_events = 0;
    debug_voices = 0;
    if (enabled) {
        silence_voices();
        program_instruments(score_id);
    }
}

void music_tick(void)
{
    const MusicStep *step;
    unsigned voice;
    if (!enabled) return;
    ++debug_ticks;
    if (step_ticks) {
        --step_ticks;
        return;
    }
    step = &scores[score_id][step_index];
    silence_voices();
    for (voice = 0; voice < VOICES; ++voice)
        if (step->note[voice] != REST) note_on(voice, step->note[voice]);
    step_ticks = (u8)(step->ticks - 1);
    step_index = (u8)((step_index + 1) % STEPS);
}

u32 music_debug_ticks(void) { return debug_ticks; }
u32 music_debug_events(void) { return debug_events; }
u8 music_debug_voice_mask(void) { return debug_voices; }
