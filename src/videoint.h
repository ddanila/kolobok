#ifndef KOLOBOK_VIDEOINT_H
#define KOLOBOK_VIDEOINT_H

#include "assets.h"

/* Shared internals of the Mode X renderer, for src/video.cpp and the overlay
 * files built on top of it. Not public API: everything the rest of the game
 * calls is declared in video.h. This header exists so the editor's overlays can
 * live in their own translation unit and stay out of KOLOBOK.EXE, which never
 * draws them, while still reaching the primitives video.cpp already has. */

/* The visible page is 320 wide inside a 328-pixel logical line; the spare eight
 * pixels are what pel panning scrolls through. */
#define LOGICAL_W 328
#define HUD_H 24

/* What the front buffer currently holds. Overlays consult it to avoid redrawing
 * a scene that is already on screen, and stamp it once they have drawn. */
#define RENDER_NONE 0
#define RENDER_GAME 1
#define RENDER_TITLE 2
#define RENDER_PAUSE 3
#define RENDER_WIN 4

/* Names for the bank palette that tools/assets.py writes, in its order. */
enum Color {
    COLOR_BLACK, COLOR_NIGHT, COLOR_SKY, COLOR_SKY_DEEP,
    COLOR_CREAM, COLOR_PINE, COLOR_FOLIAGE, COLOR_LEAF,
    COLOR_BARK_DARK, COLOR_BARK, COLOR_BARK_LIGHT, COLOR_GOLD,
    COLOR_EMBER_DARK, COLOR_EMBER, COLOR_YELLOW, COLOR_WHITE,
    COLOR_BLOOD, COLOR_RED, COLOR_RED_BRIGHT, COLOR_FLAME,
    COLOR_SKIN, COLOR_WOOD, COLOR_GREY, COLOR_GREY_LIGHT,
    COLOR_SOOT, COLOR_RUST, COLOR_TAN, COLOR_BLUE,
    COLOR_CYAN, COLOR_PURPLE, COLOR_PINK, COLOR_LEMON
};

/* Pel pan of the frame being drawn. Every overlay coordinate is offset by it,
 * because the world underneath has already been scrolled by that much. */
extern unsigned char draw_pan;
extern int render_state;

void fill_rect(int x, int y, int w, int h, unsigned char color);
void draw_text(int x, int y, const char *text, unsigned char color, int scale);
void draw_number(int x, int y, unsigned value, unsigned char color);
void overlay_box(unsigned char color);

#endif
