#ifndef KOLOBOK_MUSIC_H
#define KOLOBOK_MUSIC_H

#include "assets.h"

typedef void (*MusicRegisterSink)(u8 reg, u8 value, void *context);

enum MusicTrack { MUSIC_TITLE, MUSIC_GARDEN, MUSIC_FOREST, MUSIC_DEEP, MUSIC_HOME };

int music_init(int enabled);
void music_shutdown(void);
void music_set_enabled(int enabled);
int music_is_enabled(void);
int music_is_detected(void);
void music_play(unsigned track);
void music_tick(void);
void music_set_sink(MusicRegisterSink sink, void *context);
u32 music_debug_ticks(void);
u32 music_debug_events(void);
u8 music_debug_voice_mask(void);

#endif
