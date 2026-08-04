#ifndef KOLOBOK_PLATFORM_H
#define KOLOBOK_PLATFORM_H

#include "game.h"

enum KoloKey {
    KEY_ESCAPE = 0x01,
    KEY_ENTER = 0x1c,
    KEY_1 = 0x02,
    KEY_2 = 0x03,
    KEY_3 = 0x04,
    KEY_M = 0x32,
    KEY_A = 0x1e,
    KEY_E = 0x12,
    KEY_K = 0x25,
    KEY_O = 0x18,
    KEY_P = 0x19,
    KEY_R = 0x13,
    KEY_T = 0x14,
    KEY_Z = 0x2c,
    KEY_BACKSPACE = 0x0e,
    KEY_S = 0x1f,
    KEY_D = 0x20,
    KEY_SPACE = 0x39,
    KEY_TAB = 0x0f,
    KEY_PAGE_UP = 0x49,
    KEY_PAGE_DOWN = 0x51,
    KEY_DELETE = 0x53,
    KEY_F1 = 0x3b,
    KEY_F2 = 0x3c,
    KEY_F3 = 0x3d,
    KEY_F4 = 0x3e,
    KEY_DOWN = 0x50,
    KEY_UP = 0x48,
    KEY_LEFT = 0x4b,
    KEY_RIGHT = 0x4d
};

#define KOLO_PROFILE_TIMER_HZ 1193182UL

int keyboard_install(void);
void keyboard_remove(void);
int key_down(unsigned scan);
int key_pressed(unsigned scan);
void keyboard_clear_edges(void);

void speaker_init(int enabled);
void speaker_play(unsigned frequency, unsigned frames);
void speaker_tick(void);
void speaker_shutdown(void);

void platform_profile_timer_init(void);
u16 platform_profile_timer_read(void);

#endif
