#include "video.h"
#include "platform.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define LOGICAL_W 328
#define PITCH 82
#define HUD_H 24
#define PAGE_SIZE (PITCH * SCREEN_H)
#define GAME_PAGE_0 0
#define GAME_PAGE_1 PAGE_SIZE
#define TITLE_PAGE (PAGE_SIZE * 2)
#define HUD_CACHE_BYTES (PITCH * HUD_H)
#define HUD_CACHE_BASE (PAGE_SIZE * 3)
#define PROMPT_BACKUP_BASE (HUD_CACHE_BASE + HUD_CACHE_BYTES * 4)
#define PROMPT_X_BYTE 22
#define PROMPT_Y 124
#define PROMPT_W_BYTES 35
#define PROMPT_H 16
#define TILE_CACHE_BASE (PROMPT_BACKUP_BASE + PROMPT_W_BYTES * PROMPT_H)
#define TILE_CACHE_BYTES 64
#define MAX_CAMERA 960
#define RENDER_NONE 0
#define RENDER_GAME 1
#define RENDER_TITLE 2
#define RENDER_PAUSE 3
#define RENDER_WIN 4
#define COTTAGE_W 48
#define COTTAGE_H 44

static unsigned char __far *scratch;
static unsigned scratch_segment;
static unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
static unsigned draw_base;
static unsigned display_base;
static unsigned pending_base;
static unsigned char draw_pan;
static unsigned char display_pan;
static unsigned char pending_pan;
static unsigned char next_game_page;
static unsigned char current_map_mask = 0xff;
static int flip_pending;
static int vsync_enabled = 1;
static int profile_enabled;
static int render_state;
static int title_cache_valid;
static int hud_cache_valid;
static int title_blink = -1;
static short tree_origin[MAX_CAMERA + 1];
static short cloud_origin[MAX_CAMERA + 1];
static unsigned char tree_tall[MAX_CAMERA + 1];
static unsigned char capture_row[SCREEN_W * 3];
static u32 crc_table[256];
static VideoProfile profile;

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

static void fill_bytes_386(unsigned offset, unsigned width, unsigned height,
                           unsigned char color);
#pragma aux fill_bytes_386 = \
    "mov dl,al" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "mov al,dl" \
    "cld" \
    "mov dx,82" \
    "sub dx,si" \
    "mx_fill_row:" \
    "mov cx,si" \
    "rep stosb" \
    "add di,dx" \
    "dec bx" \
    "jnz mx_fill_row" \
    parm [di] [si] [bx] [al] modify [ax bx cx dx si di es];

static void clear_vram_386(unsigned char color);
#pragma aux clear_vram_386 = \
    "mov dl,al" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "mov al,dl" \
    "mov ah,al" \
    "mov bx,ax" \
    "shl eax,16" \
    "mov ax,bx" \
    "xor di,di" \
    "mov cx,16384" \
    "cld" \
    "rep stosd" \
    parm [al] modify [ax bx cx dx di es];

static void latch_copy_386(unsigned source, unsigned target, unsigned count);
#pragma aux latch_copy_386 = \
    "push ds" \
    "mov ax,0a000h" \
    "mov ds,ax" \
    "mov es,ax" \
    "cld" \
    "rep movsb" \
    "pop ds" \
    parm [si] [di] [cx] modify [ax cx si di es];

static void blit_tile_plane_386(const unsigned char *source, unsigned target);
#pragma aux blit_tile_plane_386 = \
    "mov ax,0a000h" \
    "mov es,ax" \
    "mov cx,16" \
    "cld" \
    "mx_tile_row:" \
    "movsd" \
    "add di,78" \
    "dec cx" \
    "jnz mx_tile_row" \
    parm [si] [di] modify [ax cx si di es];

static void copy_to_vram_386(const unsigned char *source, unsigned target,
                             unsigned count);
#pragma aux copy_to_vram_386 = \
    "mov ax,0a000h" \
    "mov es,ax" \
    "cld" \
    "rep movsb" \
    parm [si] [di] [cx] modify [ax cx si di es];

static void latch_tile_386(unsigned source, unsigned target);
#pragma aux latch_tile_386 = \
    "push ds" \
    "mov ax,0a000h" \
    "mov ds,ax" \
    "mov es,ax" \
    "mov cx,16" \
    "cld" \
    "mx_latch_tile_row:" \
    "movsb" \
    "movsb" \
    "movsb" \
    "movsb" \
    "add di,78" \
    "dec cx" \
    "jnz mx_latch_tile_row" \
    "pop ds" \
    parm [si] [di] modify [ax cx si di es];

static u32 profile_elapsed(u16 started)
{
    return (u16)(started - platform_profile_timer_read());
}

static void set_mode(unsigned mode)
{
    union REGS regs;
    regs.w.ax = mode;
    int86(0x10, &regs, &regs);
}

static void set_map_mask(unsigned char mask)
{
    if (mask == current_map_mask) return;
    outpw(0x3c4, ((unsigned)mask << 8) | 2);
    current_map_mask = mask;
}

static void set_mode_x(void)
{
    set_mode(0x13);
    outpw(0x3c4, 0x0100);
    outpw(0x3c4, 0x0604);
    outp(0x3c2, 0x63);
    outpw(0x3c4, 0x0300);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3d4, 0x2913);
    current_map_mask = 0xff;
    set_map_mask(0x0f);
    clear_vram_386(0);
}

static void set_display_start(unsigned base, unsigned char pan)
{
    outpw(0x3d4, 0x0c | (base & 0xff00));
    outpw(0x3d4, 0x0d | ((base & 0x00ff) << 8));
    (void)inp(0x3da);
    outp(0x3c0, 0x33);
    outp(0x3c0, (unsigned char)(pan * 2));
}

static void wait_vblank(void)
{
    while (inp(0x3da) & 8) { }
    while (!(inp(0x3da) & 8)) { }
}

static void latch_copy(unsigned source, unsigned target, unsigned count)
{
    outpw(0x3ce, 0x4105);
    set_map_mask(0x0f);
    latch_copy_386(source, target, count);
    outpw(0x3ce, 0x4005);
}

static void begin_latch_writes(void)
{
    outpw(0x3ce, 0x4105);
    set_map_mask(0x0f);
}

static void end_latch_writes(void)
{
    outpw(0x3ce, 0x4005);
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    unsigned offset;
    unsigned edge;
    unsigned groups;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LOGICAL_W) w = LOGICAL_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    offset = draw_base + (unsigned)y * PITCH + ((unsigned)x >> 2);
    if (x & 3) {
        edge = 4U - ((unsigned)x & 3U);
        if (edge > (unsigned)w) edge = (unsigned)w;
        set_map_mask((unsigned char)(((1U << edge) - 1U) << (x & 3)));
        fill_bytes_386(offset, 1, h, color);
        ++offset;
        w -= edge;
    }
    groups = (unsigned)w >> 2;
    if (groups != 0) {
        set_map_mask(0x0f);
        fill_bytes_386(offset, groups, h, color);
        offset += groups;
    }
    edge = (unsigned)w & 3U;
    if (edge != 0) {
        set_map_mask((unsigned char)((1U << edge) - 1U));
        fill_bytes_386(offset, 1, h, color);
    }
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
                fill_rect(x + col * scale, y + row * scale,
                          scale, scale, color);
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

static void blit_tile_plane(const unsigned char *source, int x, int y,
                            unsigned plane)
{
    int sy;
    int relative = ((int)plane - (x & 3)) & 3;
    const unsigned char *plane_source = source + relative * 64;
    if (x >= 0 && x <= LOGICAL_W - 16 && y >= 0 && y <= SCREEN_H - 16) {
        blit_tile_plane_386(plane_source,
            draw_base + (unsigned)y * PITCH + ((x + relative) >> 2));
        return;
    }
    for (sy = 0; sy < 16; ++sy) {
        int pixel;
        int dy = y + sy;
        if ((unsigned)dy >= SCREEN_H) continue;
        for (pixel = 0; pixel < 4; ++pixel) {
            int dx = x + relative + pixel * 4;
            if ((unsigned)dx < LOGICAL_W)
                vram[draw_base + (unsigned)dy * PITCH + (dx >> 2)] =
                    plane_source[sy * 4 + pixel];
        }
    }
}

static void blit_sprite_plane(const AssetPack *assets, unsigned sprite,
                              int x, int y, unsigned plane)
{
    const unsigned char *source;
    int sy;
    if (sprite >= assets->sprite_count || x <= -16 || x >= LOGICAL_W ||
        y <= -16 || y >= SCREEN_H) return;
    if (x >= 0 && x <= LOGICAL_W - 16 && y >= 0 && y <= SCREEN_H - 16) {
        source = assets->sprite_planar_spans[sprite][(x & 3) * 4 + plane];
        for (sy = 0; sy < 16; ++sy) {
            unsigned run, run_count = *source++;
            for (run = 0; run < run_count; ++run) {
                unsigned start = *source++;
                unsigned length = *source++;
                copy_to_vram_386(source,
                    draw_base + (unsigned)(y + sy) * PITCH + (x >> 2) + start,
                    length);
                source += length;
            }
        }
        return;
    }
    source = assets->sprite_spans[sprite];
    for (sy = 0; sy < 16; ++sy) {
        unsigned run, run_count = *source++;
        for (run = 0; run < run_count; ++run) {
            int start = *source++;
            int length = *source++;
            int pixel;
            for (pixel = 0; pixel < length; ++pixel) {
                int dx = x + start + pixel;
                int dy = y + sy;
                if ((unsigned)dx < LOGICAL_W && (unsigned)dy < SCREEN_H &&
                    (unsigned)(dx & 3) == plane)
                    vram[draw_base + (unsigned)dy * PITCH + (dx >> 2)] =
                        source[pixel];
            }
            source += length;
        }
    }
}

static void blit_sprite(const AssetPack *assets, unsigned sprite, int x, int y)
{
    unsigned plane;
    for (plane = 0; plane < 4; ++plane) {
        set_map_mask((unsigned char)(1 << plane));
        blit_sprite_plane(assets, sprite, x, y, plane);
    }
}

static void draw_background(int camera, int pan, int preserve_hud)
{
    int x, base, tall;
    fill_rect(0, preserve_hud ? HUD_H : 0, LOGICAL_W,
              SCREEN_H - (preserve_hud ? HUD_H : 0), 2);
    fill_rect(0, 112, LOGICAL_W, 88, 3);
    if (camera < 0) camera = 0;
    if (camera > MAX_CAMERA) camera = MAX_CAMERA;
    x = tree_origin[camera];
    tall = tree_tall[camera];
    while (x < SCREEN_W + 48) {
        int h = 42 + tall * 12;
        fill_rect(x + pan + 20, 112 - h, 7, h, 5);
        for (base = 0; base < 4; ++base)
            fill_rect(x + pan + 8 + base * 4, 78 - base * 7,
                      31 - base * 8, 8, 5);
        if (!(x > -48 && x < 0)) tall ^= 1;
        x += 48;
    }
    for (x = cloud_origin[camera]; x < SCREEN_W; x += 90) {
        fill_rect(x + pan, 36, 25, 4, 4);
        fill_rect(x + pan + 6, 32, 14, 4, 4);
    }
}

static void draw_cottage(const AssetPack *assets, int camera)
{
    int x = assets->home.x - camera;
    int ground = assets->home.y + KOLO_TILE_SIZE;
    int y = HUD_H + ground - COTTAGE_H;
    fill_rect(x, y, COTTAGE_W, COTTAGE_H, 21);
    fill_rect(x + 4, y + 4, COTTAGE_W - 8, COTTAGE_H - 4, 26);
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

static void draw_entity_plane(const GameState *game, int camera, unsigned plane)
{
    const AssetPack *assets = game->assets;
    unsigned i;
    for (i = 0; i < assets->berry_count; ++i)
        if (!game->berry_taken[i])
            blit_sprite_plane(assets, 4, assets->berries[i].x - camera,
                              HUD_H + assets->berries[i].y, plane);
    blit_sprite_plane(assets, 7, assets->checkpoint.x - camera,
                      HUD_H + assets->checkpoint.y, plane);
    for (i = 0; i < assets->enemy_count; ++i)
        blit_sprite_plane(assets, 5 + game->enemies[i].type,
                          (int)(game->enemies[i].x >> KOLO_FP_SHIFT) - camera,
                          HUD_H + (int)(game->enemies[i].y >> KOLO_FP_SHIFT), plane);
    blit_sprite_plane(assets, game->player.animation,
                      (int)(game->player.x >> KOLO_FP_SHIFT) - camera,
                      HUD_H + (int)(game->player.y >> KOLO_FP_SHIFT), plane);
}

static void build_tile_cache(const AssetPack *assets)
{
    unsigned tile, plane;
    for (tile = 0; tile < assets->tile_count; ++tile)
        for (plane = 0; plane < 4; ++plane) {
            set_map_mask((unsigned char)(1 << plane));
            copy_to_vram_386(assets->tiles + tile * 256 + plane * 64,
                TILE_CACHE_BASE + tile * TILE_CACHE_BYTES, TILE_CACHE_BYTES);
        }
}

static void draw_tiles(const AssetPack *assets, int camera, int first, int last)
{
    int tx, ty;
    unsigned plane;
    begin_latch_writes();
    for (ty = 0; ty < assets->map_h; ++ty)
        for (tx = first; tx < last; ++tx) {
            unsigned tile = assets->map[ty * assets->map_w + tx];
            int x = tx * 16 - camera;
            int y = HUD_H + ty * 16;
            if (tile && tile < assets->tile_count && x >= 0 &&
                x <= LOGICAL_W - 16 && y >= 0 && y <= SCREEN_H - 16)
                latch_tile_386(TILE_CACHE_BASE + tile * TILE_CACHE_BYTES,
                    draw_base + (unsigned)y * PITCH + (x >> 2));
        }
    end_latch_writes();
    for (plane = 0; plane < 4; ++plane) {
        set_map_mask((unsigned char)(1 << plane));
        for (ty = 0; ty < assets->map_h; ++ty)
            for (tx = first; tx < last; ++tx) {
                unsigned tile = assets->map[ty * assets->map_w + tx];
                int x = tx * 16 - camera;
                int y = HUD_H + ty * 16;
                if (tile && tile < assets->tile_count &&
                    !(x >= 0 && x <= LOGICAL_W - 16 && y >= 0 &&
                      y <= SCREEN_H - 16))
                    blit_tile_plane(assets->tiles + tile * 256, x, y, plane);
            }
    }
}

static void draw_world(const GameState *game, int preserve_hud)
{
    const AssetPack *assets = game->assets;
    int actual_camera = (int)(game->camera_x >> KOLO_FP_SHIFT);
    int pan = preserve_hud ? (actual_camera & 3) : 0;
    int camera = actual_camera - pan;
    int first = camera >> 4;
    int last = (camera + SCREEN_W + 3) / 16 + 1;
    unsigned plane;
    u16 stage;
    draw_pan = (unsigned char)pan;
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_background(actual_camera, pan, preserve_hud);
    draw_cottage(assets, camera);
    if (profile_enabled) profile.background_ticks += profile_elapsed(stage);
    if (first < 0) first = 0;
    if (last > assets->map_w) last = assets->map_w;
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_tiles(assets, camera, first, last);
    if (profile_enabled) profile.tile_ticks += profile_elapsed(stage);
    if (profile_enabled) stage = platform_profile_timer_read();
    for (plane = 0; plane < 4; ++plane) {
        set_map_mask((unsigned char)(1 << plane));
        draw_entity_plane(game, camera, plane);
    }
    if (profile_enabled) profile.sprite_ticks += profile_elapsed(stage);
}

static void draw_hud_static(const AssetPack *assets, int pan)
{
    fill_rect(0, 0, LOGICAL_W, HUD_H, 1);
    fill_rect(0, HUD_H - 2, LOGICAL_W, 2, 11);
    blit_sprite(assets, 4, 8 + pan, 4);
    draw_text(40 + pan, 8, "OF", 23, 1);
    draw_number(58 + pan, 8, assets->berry_count, 15);
    draw_text(104 + pan, 8, "HOME", 11, 1);
    draw_text(245 + pan, 8, "S SOUND", 23, 1);
}

static void build_hud_cache(const AssetPack *assets)
{
    unsigned saved_base = draw_base;
    unsigned char saved_pan = draw_pan;
    int pan;
    for (pan = 0; pan < 4; ++pan) {
        draw_base = HUD_CACHE_BASE + pan * HUD_CACHE_BYTES;
        draw_pan = (unsigned char)pan;
        draw_hud_static(assets, pan);
    }
    draw_base = saved_base;
    draw_pan = saved_pan;
    hud_cache_valid = 1;
}

static void draw_hud(const GameState *game)
{
    if (!hud_cache_valid) build_hud_cache(game->assets);
    latch_copy(HUD_CACHE_BASE + draw_pan * HUD_CACHE_BYTES,
               draw_base, HUD_CACHE_BYTES);
    draw_number(28 + draw_pan, 8, game->berries_collected, 15);
}

static void backup_prompt(void)
{
    int row;
    for (row = 0; row < PROMPT_H; ++row)
        latch_copy(TITLE_PAGE + (PROMPT_Y + row) * PITCH + PROMPT_X_BYTE,
                   PROMPT_BACKUP_BASE + row * PROMPT_W_BYTES,
                   PROMPT_W_BYTES);
}

static void restore_prompt(void)
{
    int row;
    for (row = 0; row < PROMPT_H; ++row)
        latch_copy(PROMPT_BACKUP_BASE + row * PROMPT_W_BYTES,
                   TITLE_PAGE + (PROMPT_Y + row) * PITCH + PROMPT_X_BYTE,
                   PROMPT_W_BYTES);
}

int video_init(const AssetPack *assets)
{
    unsigned i;
    if (_dos_allocmem(4000, &scratch_segment) != 0) return 0;
    scratch = (unsigned char __far *)MK_FP(scratch_segment, 0);
    set_mode_x();
    for (i = 0; i <= MAX_CAMERA; ++i) {
        tree_origin[i] = (short)(-40 - ((i / 5) % 48));
        cloud_origin[i] = (short)(18 - ((i / 9) % 90));
        tree_tall[i] = (unsigned char)((tree_origin[i] / 48) & 1);
    }
    for (i = 0; i < 256; ++i) {
        u32 value = i;
        unsigned bit;
        for (bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^
                (0xedb88320UL & (0UL - (value & 1UL)));
        crc_table[i] = value;
    }
    outp(0x3c8, 0);
    for (i = 0; i < 768; ++i) outp(0x3c9, assets->palette[i]);
    build_tile_cache(assets);
    draw_base = display_base = GAME_PAGE_0;
    draw_pan = display_pan = 0;
    set_display_start(display_base, display_pan);
    return 1;
}

void video_shutdown(void)
{
    set_mode(0x03);
    if (scratch_segment != 0) _dos_freemem(scratch_segment);
    scratch = NULL;
    scratch_segment = 0;
    title_cache_valid = hud_cache_valid = 0;
    title_blink = -1;
    render_state = RENDER_NONE;
    current_map_mask = 0xff;
}

void video_vsync_enable(int enabled)
{
    vsync_enabled = enabled;
}

void video_present(void)
{
    u16 stage;
    if (profile_enabled) stage = platform_profile_timer_read();
    if (flip_pending) {
        if (vsync_enabled) wait_vblank();
        set_display_start(pending_base, pending_pan);
        display_base = pending_base;
        display_pan = pending_pan;
        flip_pending = 0;
    }
    if (profile_enabled) profile.present_ticks += profile_elapsed(stage);
}

void video_render_game(const GameState *game)
{
    u16 stage;
    draw_base = next_game_page ? GAME_PAGE_1 : GAME_PAGE_0;
    next_game_page ^= 1;
    draw_world(game, 1);
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_hud(game);
    if (profile_enabled) {
        profile.hud_ticks += profile_elapsed(stage);
        ++profile.frames;
    }
    pending_base = draw_base;
    pending_pan = draw_pan;
    flip_pending = 1;
    render_state = RENDER_GAME;
}

void video_render_title(const AssetPack *assets, u32 ticks)
{
    GameState preview;
    int blink = (int)((ticks / 24) & 1);
    draw_base = TITLE_PAGE;
    draw_pan = 0;
    if (!title_cache_valid) {
        game_init(&preview, assets);
        preview.camera_x = 0;
        draw_world(&preview, 0);
        fill_rect(32, 26, 256, 92, 1);
        fill_rect(36, 30, 248, 84, 5);
        draw_text(70, 42, "KOLOBOK", 14, 3);
        draw_text(67, 72, "FOREST BERRIES", 15, 1);
        draw_text(55, 88, "ARROWS OR A D TO ROLL", 23, 1);
        draw_text(67, 99, "SPACE OR UP TO JUMP", 23, 1);
        backup_prompt();
        title_cache_valid = 1;
    }
    if (blink != title_blink) {
        restore_prompt();
        if (blink) draw_text(94, 128, "PRESS ENTER", 31, 1);
        title_blink = blink;
    }
    if (render_state != RENDER_TITLE) {
        pending_base = TITLE_PAGE;
        pending_pan = 0;
        flip_pending = 1;
    }
    render_state = RENDER_TITLE;
}

static void overlay_box(unsigned char color)
{
    fill_rect(54 + draw_pan, 61, 212, 78, 1);
    fill_rect(58 + draw_pan, 65, 204, 70, color);
}

void video_render_pause(const GameState *game)
{
    if (render_state == RENDER_PAUSE) return;
    video_render_game(game);
    overlay_box(5);
    draw_text(124 + draw_pan, 76, "PAUSED", 15, 1);
    draw_text(82 + draw_pan, 96, "ENTER TO CONTINUE", 23, 1);
    draw_text(94 + draw_pan, 112, "ESC TO QUIT", 31, 1);
    render_state = RENDER_PAUSE;
}

void video_render_win(const GameState *game)
{
    if (render_state == RENDER_WIN) return;
    video_render_game(game);
    overlay_box(21);
    draw_text(91 + draw_pan, 73, "BERRIES ARE HOME", 14, 1);
    draw_text(103 + draw_pan, 92, "WELL DONE", 15, 2);
    draw_text(88 + draw_pan, 119, "ENTER TO PLAY AGAIN", 23, 1);
    render_state = RENDER_WIN;
}

static void reconstruct_page(unsigned base, unsigned char pan)
{
    unsigned plane;
    int y;
    for (plane = 0; plane < 4; ++plane) {
        int first_x = ((int)plane - pan) & 3;
        outpw(0x3ce, ((unsigned)plane << 8) | 4);
        for (y = 0; y < SCREEN_H; ++y) {
            int x;
            for (x = first_x; x < SCREEN_W; x += 4)
                scratch[(unsigned)y * SCREEN_W + x] =
                    vram[base + (unsigned)y * PITCH + ((x + pan) >> 2)];
        }
    }
    outpw(0x3ce, 0x0004);
}

static u32 scratch_crc(void)
{
    u32 crc = 0xffffffffUL;
    u32 i;
    for (i = 0; i < 64000UL; ++i)
        crc = (crc >> 8) ^ crc_table[(unsigned char)(crc ^ scratch[i])];
    return crc ^ 0xffffffffUL;
}

u32 video_frame_crc(void)
{
    reconstruct_page(draw_base, draw_pan);
    return scratch_crc();
}

u32 video_vram_crc(void)
{
    reconstruct_page(display_base, display_pan);
    return scratch_crc();
}

void video_profile_enable(int enabled)
{
    if (enabled) platform_profile_timer_init();
    profile_enabled = enabled;
}

void video_profile_reset(void)
{
    memset(&profile, 0, sizeof(profile));
}

void video_profile_get(VideoProfile *result)
{
    *result = profile;
}

int video_display_state_valid(void)
{
    unsigned start;
    unsigned char pan;
    if (!(inp(0x3c0) & 0x20)) return 0;
    outp(0x3d4, 0x0c);
    start = (unsigned)inp(0x3d5) << 8;
    outp(0x3d4, 0x0d);
    start |= inp(0x3d5);
    (void)inp(0x3da);
    outp(0x3c0, 0x33);
    pan = (unsigned char)inp(0x3c1);
    return start == display_base && pan == (unsigned char)(display_pan * 2);
}

int video_write_ppm(const char *path, const AssetPack *assets)
{
    FILE *file;
    int y;
    reconstruct_page(display_base, display_pan);
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    if (fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H) < 0) {
        fclose(file);
        return 0;
    }
    for (y = 0; y < SCREEN_H; ++y) {
        int x;
        for (x = 0; x < SCREEN_W; ++x) {
            unsigned color = scratch[(unsigned)y * SCREEN_W + x];
            unsigned component;
            for (component = 0; component < 3; ++component) {
                unsigned value = assets->palette[color * 3 + component];
                capture_row[x * 3 + component] =
                    (unsigned char)((value << 2) | (value >> 4));
            }
        }
        if (fwrite(capture_row, 1, sizeof(capture_row), file) !=
            sizeof(capture_row)) {
            fclose(file);
            return 0;
        }
    }
    return fclose(file) == 0;
}
