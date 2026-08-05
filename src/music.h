#ifndef KOLOBOK_MUSIC_H
#define KOLOBOK_MUSIC_H

#include "assets.h"

typedef void (*MusicRegisterSink)(u8 reg, u8 value, void *context);

struct Track { enum Enum { TITLE, GARDEN, FOREST, DEEP, HOME }; };

bool music_init(bool enabled);
void music_shutdown(void);
void music_set_enabled(bool enabled);
bool music_is_enabled(void);
bool music_is_detected(void);
void music_play(unsigned track);
void music_tick(void);
void music_set_sink(MusicRegisterSink sink, void *context);
u32 music_debug_ticks(void);
u32 music_debug_events(void);
u8 music_debug_voice_mask(void);

#endif
