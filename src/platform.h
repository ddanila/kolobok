#ifndef KOLOBOK_PLATFORM_H
#define KOLOBOK_PLATFORM_H

#include "game.h"

enum KoloKey {
    KEY_ESCAPE = 0x01,
    KEY_ENTER = 0x1c,
    KEY_A = 0x1e,
    KEY_S = 0x1f,
    KEY_D = 0x20,
    KEY_SPACE = 0x39,
    KEY_UP = 0x48,
    KEY_LEFT = 0x4b,
    KEY_RIGHT = 0x4d
};

int keyboard_install(void);
void keyboard_remove(void);
int key_down(unsigned scan);
int key_pressed(unsigned scan);
void keyboard_clear_edges(void);

void speaker_init(int enabled);
void speaker_play(unsigned frequency, unsigned frames);
void speaker_tick(void);
void speaker_shutdown(void);

#endif
