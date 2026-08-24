#ifndef KOLOBOK_PLATFORM_H
#define KOLOBOK_PLATFORM_H

#include "game.h"

struct Key {
    enum Enum {
        ESCAPE = 0x01,
        ENTER = 0x1c,
        DIGIT_1 = 0x02,
        DIGIT_2 = 0x03,
        DIGIT_3 = 0x04,
        M = 0x32,
        A = 0x1e,
        C = 0x2e,
        E = 0x12,
        I = 0x17,
        K = 0x25,
        O = 0x18,
        P = 0x19,
        R = 0x13,
        T = 0x14,
        Z = 0x2c,
        BACKSPACE = 0x0e,
        S = 0x1f,
        D = 0x20,
        SPACE = 0x39,
        TAB = 0x0f,
        PAGE_UP = 0x49,
        PAGE_DOWN = 0x51,
        DELETE = 0x53,
        F1 = 0x3b,
        F2 = 0x3c,
        F3 = 0x3d,
        F4 = 0x3e,
        DOWN = 0x50,
        UP = 0x48,
        LEFT = 0x4b,
        RIGHT = 0x4d
    };
};

#define PROFILE_TIMER_HZ 1193182UL

bool keyboard_install(void);
void keyboard_remove(void);
bool key_down(unsigned scan);
bool key_pressed(unsigned scan);
void keyboard_clear_edges(void);

void speaker_init(bool enabled);
void speaker_play(unsigned frequency, unsigned frames);
void speaker_tick(void);
void speaker_shutdown(void);

void platform_profile_timer_init(void);
u16 platform_profile_timer_read(void);

#endif
