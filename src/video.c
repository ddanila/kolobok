#include "video.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define HUD_H 24
#define TRANSPARENT 0

static unsigned char __far *framebuffer;

static const unsigned char font[36][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
};

static void set_mode(unsigned mode)
{
    union REGS regs;
    regs.w.ax = mode;
    int86(0x10, &regs, &regs);
}

static void put_pixel(int x, int y, unsigned char color)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        framebuffer[(unsigned)y * SCREEN_W + (unsigned)x] = color;
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    int row;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; ++row)
        _fmemset(framebuffer + (unsigned)(y + row) * SCREEN_W + x, color, w);
}

static int glyph_index(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return -1;
}

static void draw_char(int x, int y, char c, unsigned char color, int scale)
{
    int index = glyph_index(c);
    int row, col;
    if (index < 0) return;
    for (row = 0; row < 7; ++row)
        for (col = 0; col < 5; ++col)
            if (font[index][row] & (16 >> col))
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
}

static void draw_text(int x, int y, const char *text, unsigned char color, int scale)
{
    while (*text) {
        if (*text != ' ') draw_char(x, y, *text, color, scale);
        x += 6 * scale;
        ++text;
    }
}

static void draw_number(int x, int y, unsigned value, unsigned char color)
{
    char text[8];
    unsigned pos = sizeof(text) - 1;
    text[pos] = '\0';
    do {
        text[--pos] = (char)('0' + value % 10);
        value /= 10;
    } while (value && pos);
    draw_text(x, y, text + pos, color, 1);
}

static void blit(const unsigned char *source, int x, int y, int masked)
{
    int sx, sy;
    for (sy = 0; sy < 16; ++sy) {
        int dy = y + sy;
        if ((unsigned)dy >= SCREEN_H) continue;
        for (sx = 0; sx < 16; ++sx) {
            int dx = x + sx;
            unsigned char color = source[sy * 16 + sx];
            if ((unsigned)dx < SCREEN_W && (!masked || color != TRANSPARENT))
                framebuffer[(unsigned)dy * SCREEN_W + (unsigned)dx] = color;
        }
    }
}

static void draw_background(int camera)
{
    int x, base;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 2);
    fill_rect(0, 112, SCREEN_W, 88, 3);
    for (x = -40 - ((camera / 5) % 48); x < SCREEN_W + 48; x += 48) {
        int h = 42 + ((x / 48) & 1) * 12;
        fill_rect(x + 20, 112 - h, 7, h, 5);
        for (base = 0; base < 4; ++base)
            fill_rect(x + 8 + base * 4, 78 - base * 7, 31 - base * 8, 8, 5);
    }
    for (x = 18 - ((camera / 9) % 90); x < SCREEN_W; x += 90) {
        fill_rect(x, 36, 25, 4, 4);
        fill_rect(x + 6, 32, 14, 4, 4);
    }
}

static void draw_cottage(int camera)
{
    int x = 10 - camera;
    int y = HUD_H + 92;
    fill_rect(x, y, 48, 44, 21);
    fill_rect(x + 4, y + 4, 40, 40, 26);
    fill_rect(x + 17, y + 20, 15, 24, 8);
    fill_rect(x + 20, y + 23, 9, 21, 21);
    fill_rect(x + 5, y + 13, 10, 10, 1);
    fill_rect(x + 7, y + 15, 6, 6, 28);
    fill_rect(x + 34, y + 13, 9, 10, 1);
    fill_rect(x + 36, y + 15, 5, 6, 28);
    fill_rect(x - 4, y - 5, 56, 8, 25);
    fill_rect(x + 2, y - 11, 44, 8, 25);
    fill_rect(x + 8, y - 17, 32, 8, 25);
}

static void draw_world(const GameState *game)
{
    const AssetPack *assets = game->assets;
    int camera = (int)(game->camera_x >> KOLO_FP_SHIFT);
    int first = camera / 16;
    int last = (camera + SCREEN_W) / 16 + 1;
    int tx, ty;
    unsigned i;
    draw_background(camera);
    draw_cottage(camera);
    if (first < 0) first = 0;
    if (last > assets->map_w) last = assets->map_w;
    for (ty = 0; ty < assets->map_h; ++ty) {
        for (tx = first; tx < last; ++tx) {
            unsigned tile = assets->map[ty * assets->map_w + tx];
            if (tile && tile < assets->tile_count)
                blit(assets->tiles + tile * 256, tx * 16 - camera, HUD_H + ty * 16, 0);
        }
    }
    for (i = 0; i < assets->berry_count; ++i)
        if (!game->berry_taken[i])
            blit(assets->sprites + 3 * 256, assets->berries[i].x - camera,
                 HUD_H + assets->berries[i].y, 1);
    blit(assets->sprites + 6 * 256, assets->checkpoint.x - camera,
         HUD_H + assets->checkpoint.y, 1);
    for (i = 0; i < assets->enemy_count; ++i)
        blit(assets->sprites + (4 + game->enemies[i].type) * 256,
             (int)(game->enemies[i].x >> KOLO_FP_SHIFT) - camera,
             HUD_H + (int)(game->enemies[i].y >> KOLO_FP_SHIFT), 1);
    blit(assets->sprites + game->player.animation * 256,
         (int)(game->player.x >> KOLO_FP_SHIFT) - camera,
         HUD_H + (int)(game->player.y >> KOLO_FP_SHIFT), 1);
}

static void draw_hud(const GameState *game)
{
    fill_rect(0, 0, SCREEN_W, HUD_H, 1);
    fill_rect(0, HUD_H - 2, SCREEN_W, 2, 11);
    blit(game->assets->sprites + 3 * 256, 8, 4, 1);
    draw_number(28, 8, game->berries_collected, 15);
    draw_text(40, 8, "OF", 23, 1);
    draw_number(58, 8, game->assets->berry_count, 15);
    draw_text(104, 8, "HOME", 11, 1);
    draw_text(245, 8, "S SOUND", 23, 1);
}

int video_init(const AssetPack *assets)
{
    unsigned i;
    framebuffer = (unsigned char __far *)_fmalloc(64000UL);
    if (framebuffer == NULL) return 0;
    set_mode(0x13);
    outp(0x3c8, 0);
    for (i = 0; i < 768; ++i) outp(0x3c9, assets->palette[i]);
    return 1;
}

void video_shutdown(void)
{
    set_mode(0x03);
    if (framebuffer != NULL) _ffree(framebuffer);
    framebuffer = NULL;
}

void video_present(void)
{
    _fmemcpy((void __far *)MK_FP(0xa000, 0), framebuffer, 64000UL);
}

void video_render_game(const GameState *game)
{
    draw_world(game);
    draw_hud(game);
}

void video_render_title(const AssetPack *assets, u32 ticks)
{
    GameState preview;
    game_init(&preview, assets);
    preview.camera_x = 0;
    draw_world(&preview);
    fill_rect(32, 26, 256, 92, 1);
    fill_rect(36, 30, 248, 84, 5);
    draw_text(70, 42, "KOLOBOK", 14, 3);
    draw_text(67, 72, "FOREST BERRIES", 15, 1);
    draw_text(55, 88, "ARROWS OR A D TO ROLL", 23, 1);
    draw_text(67, 99, "SPACE OR UP TO JUMP", 23, 1);
    if ((ticks / 24) & 1) draw_text(94, 128, "PRESS ENTER", 31, 1);
}

static void overlay_box(unsigned char color)
{
    fill_rect(54, 61, 212, 78, 1);
    fill_rect(58, 65, 204, 70, color);
}

void video_render_pause(const GameState *game)
{
    video_render_game(game);
    overlay_box(5);
    draw_text(124, 76, "PAUSED", 15, 1);
    draw_text(82, 96, "ENTER TO CONTINUE", 23, 1);
    draw_text(94, 112, "ESC TO QUIT", 31, 1);
}

void video_render_win(const GameState *game)
{
    video_render_game(game);
    overlay_box(21);
    draw_text(91, 73, "BERRIES ARE HOME", 14, 1);
    draw_text(103, 92, "WELL DONE", 15, 2);
    draw_text(88, 119, "ENTER TO PLAY AGAIN", 23, 1);
}

u32 video_frame_crc(void)
{
    u32 crc = 0xffffffffUL;
    u32 i;
    int bit;
    for (i = 0; i < 64000UL; ++i) {
        crc ^= framebuffer[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
    return crc ^ 0xffffffffUL;
}
