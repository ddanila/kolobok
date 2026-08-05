#include "platform.h"

#include <conio.h>
#include <dos.h>
#include <string.h>

static volatile unsigned char keys[128];
static volatile unsigned char edges[128];
static void (__interrupt __far *old_keyboard)(void);
static bool keyboard_active;
static bool sound_enabled;
static unsigned sound_frames;

static void __interrupt __far keyboard_handler(void)
{
    unsigned char code = (unsigned char)inp(0x60);
    unsigned char scan = code & 0x7f;
    unsigned char control;
    if (scan < 128) {
        if (code & 0x80) {
            keys[scan] = 0;
        } else {
            if (!keys[scan]) edges[scan] = 1;
            keys[scan] = 1;
        }
    }
    control = (unsigned char)inp(0x61);
    outp(0x61, control | 0x80);
    outp(0x61, control);
    outp(0x20, 0x20);
}

bool keyboard_install(void)
{
    memset((void *)keys, 0, sizeof(keys));
    memset((void *)edges, 0, sizeof(edges));
    old_keyboard = _dos_getvect(9);
    if (old_keyboard == NULL) return false;
    _disable();
    _dos_setvect(9, keyboard_handler);
    _enable();
    keyboard_active = true;
    return true;
}

void keyboard_remove(void)
{
    if (!keyboard_active) return;
    _disable();
    _dos_setvect(9, old_keyboard);
    _enable();
    keyboard_active = false;
}

bool key_down(unsigned scan)
{
    return scan < 128 ? keys[scan] != 0 : false;
}

bool key_pressed(unsigned scan)
{
    bool result;
    if (scan >= 128) return false;
    _disable();
    result = edges[scan] != 0;
    edges[scan] = 0;
    _enable();
    return result;
}

void keyboard_clear_edges(void)
{
    _disable();
    memset((void *)edges, 0, sizeof(edges));
    _enable();
}

void speaker_init(bool enabled)
{
    sound_enabled = enabled;
    sound_frames = 0;
}

void speaker_play(unsigned frequency, unsigned frames)
{
    unsigned divisor;
    unsigned char control;
    if (!sound_enabled || frequency == 0) return;
    divisor = (unsigned)(1193180UL / frequency);
    outp(0x43, 0xb6);
    outp(0x42, divisor & 0xff);
    outp(0x42, divisor >> 8);
    control = (unsigned char)inp(0x61);
    outp(0x61, control | 3);
    sound_frames = frames;
}

void speaker_tick(void)
{
    if (sound_frames && --sound_frames == 0)
        outp(0x61, inp(0x61) & 0xfc);
}

void speaker_shutdown(void)
{
    outp(0x61, inp(0x61) & 0xfc);
    sound_frames = 0;
}

void platform_profile_timer_init(void)
{
    outp(0x61, (inp(0x61) & 0xfd) | 1);
    outp(0x43, 0xb4);
    outp(0x42, 0);
    outp(0x42, 0);
}

u16 platform_profile_timer_read(void)
{
    unsigned count;
    outp(0x43, 0x80);
    count = (unsigned)inp(0x42);
    count |= (unsigned)inp(0x42) << 8;
    return (u16)count;
}
