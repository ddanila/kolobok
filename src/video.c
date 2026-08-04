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
#define TILE_CACHE_BASE (HUD_CACHE_BASE + HUD_CACHE_BYTES * 4)
#define TILE_CACHE_BYTES 64
#define MAX_CAMERA 3776
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
static unsigned char current_map_mask = 0xff;
static int flip_pending;
static int vsync_enabled = 1;
static int profile_enabled;
static int render_state;
static int title_cache_valid;
static int hud_cache_valid;
static short tree_origin[MAX_CAMERA + 1];
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

static void blit_tile_plane_386(const unsigned char __far *source, unsigned target);
#pragma aux blit_tile_plane_386 = \
    "push ds" \
    "mov ds,dx" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "mov cx,16" \
    "cld" \
    "mx_tile_row:" \
    "movsd" \
    "add di,78" \
    "dec cx" \
    "jnz mx_tile_row" \
    "pop ds" \
    parm [dx si] [di] modify [ax cx si di es];

static void copy_to_vram_386(const unsigned char __far *source, unsigned target,
                             unsigned count);
#pragma aux copy_to_vram_386 = \
    "push ds" \
    "mov ds,dx" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "cld" \
    "rep movsb" \
    "pop ds" \
    parm [dx si] [di] [cx] modify [ax cx si di es];

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

static void blit_tile_plane(KoloConstFarPtr source, int x, int y,
                            unsigned plane)
{
    int sy;
    int relative = ((int)plane - (x & 3)) & 3;
    KoloConstFarPtr plane_source = source + relative * 64;
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
    KoloConstFarPtr source;
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

static void draw_cloud(int x,int y,unsigned shape,unsigned char color)
{
    int wide=18+(int)(shape&3)*4;
    int high=3+(int)((shape>>1)&1);
    fill_rect(x,y,wide,high,color);
    fill_rect(x+3+(int)(shape&1)*3,y-high,wide/2,high,color);
    if(shape>=3)fill_rect(x+wide/2,y-high-2,wide/3,3,color);
}

static void draw_background(int camera, int pan, int preserve_hud,
                            const LevelData *level)
{
    int x, base, tall;
    unsigned char sky=level->theme==KOLO_THEME_GARDEN?2:level->theme==KOLO_THEME_FOREST?3:1;
    unsigned char horizon=level->theme==KOLO_THEME_GARDEN?3:level->theme==KOLO_THEME_FOREST?5:24;
    unsigned spacing=level->theme==KOLO_THEME_GARDEN?68:level->theme==KOLO_THEME_FOREST?104:184;
    fill_rect(0, preserve_hud ? HUD_H : 0, LOGICAL_W,
              SCREEN_H - (preserve_hud ? HUD_H : 0), sky);
    fill_rect(0, 112, LOGICAL_W, 88, horizon);
    if (camera < 0) camera = 0;
    if (camera > MAX_CAMERA) camera = MAX_CAMERA;
    if(level->theme!=KOLO_THEME_GARDEN){
        x = tree_origin[camera];tall = tree_tall[camera];
        while (x < SCREEN_W + 48) {
            int h = 42 + tall * 12;
            fill_rect(x + pan + 20, 112 - h, 7, h, level->theme==KOLO_THEME_DEEP?24:5);
            for (base = 0; base < 4; ++base)
                fill_rect(x + pan + 8 + base * 4, 78 - base * 7,
                          31 - base * 8, 8, 5);
            if (!(x > -48 && x < 0)) tall ^= 1;x += 48;
        }
    }
    x=(int)((level->cloud_seed&63UL)-(unsigned)(camera/(level->theme==KOLO_THEME_GARDEN?8:12)));
    while(x<0)x+=(int)spacing;
    for (; x < SCREEN_W; x += (int)spacing) {
        unsigned shape=(unsigned)((level->cloud_seed+(u32)(x+camera)/spacing)%6UL);
        int y=27+(int)((level->cloud_seed>>(shape+1))&23UL);
        draw_cloud(x+pan,y,shape,level->theme==KOLO_THEME_DEEP?22:4);
    }
}

static void draw_cottage(const AssetPack *assets, int camera)
{
    int x = assets->level.home.x * KOLO_TILE_SIZE - camera;
    int ground = assets->level.home.y * KOLO_TILE_SIZE + KOLO_TILE_SIZE;
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

static void draw_level_trees(const AssetPack *assets, int camera)
{
    unsigned i;
    for (i = 0; i < assets->level.tree_count; ++i) {
        const KoloTree *tree = &assets->level.trees[i];
        int x = tree->x * 16 - camera;
        int base = HUD_H + tree->y * 16 + 16;
        int h = tree->height * 12;
        unsigned char trunk = tree->type == KOLO_TREE_BIRCH ? 23 : 9;
        unsigned char leaf = tree->type == KOLO_TREE_FIR ? 5 : 6;
        if (x < -24 || x > LOGICAL_W + 8) continue;
        fill_rect(x + 7, base - h, 4, h, trunk);
        if (tree->type == KOLO_TREE_FIR) {
            int row;
            for (row = 0; row < tree->height; ++row)
                fill_rect(x + 1 + row, base - h + row * 8, 17 - row * 2, 7, leaf);
        } else {
            fill_rect(x - 2, base - h - 2, 23, 10, leaf);
            fill_rect(x + 1, base - h - 8, 17, 9, leaf);
        }
    }
}

static void draw_entity_plane(const GameState *game, int camera, unsigned plane)
{
    const AssetPack *assets = game->assets;
    const LevelData *level = &assets->level;
    unsigned i;
    static const unsigned pickup_sprite[4] = {4, 11, 12, 13};
    static const unsigned animal_sprite[4] = {5, 6, 9, 10};
    for (i = 0; i < level->pickup_count; ++i)
        if (!game->pickup_taken[i])
            blit_sprite_plane(assets, pickup_sprite[level->pickups[i].type],
                              level->pickups[i].x * 16 + 3 - camera,
                              HUD_H + level->pickups[i].y * 16 + 2, plane);
    for (i = 0; i < level->checkpoint_count; ++i)
        blit_sprite_plane(assets, 7, level->checkpoints[i].x * 16 - camera,
                          HUD_H + level->checkpoints[i].y * 16, plane);
    blit_sprite_plane(assets, 7, level->exit.x * 16 - camera,
                      HUD_H + level->exit.y * 16, plane);
    for (i = 0; i < level->animal_count; ++i) {
        blit_sprite_plane(assets, animal_sprite[game->enemies[i].type],
                          (int)(game->enemies[i].x >> KOLO_FP_SHIFT) - camera,
                          HUD_H + (int)(game->enemies[i].y >> KOLO_FP_SHIFT), plane);
        if (game->enemies[i].frozen)
            blit_sprite_plane(assets, 14,
                          (int)(game->enemies[i].x >> KOLO_FP_SHIFT) - camera,
                          HUD_H + (int)(game->enemies[i].y >> KOLO_FP_SHIFT), plane);
    }
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
    draw_background(actual_camera, pan, preserve_hud, &assets->level);
    if (assets->level.theme == KOLO_THEME_GARDEN || assets->level.theme == KOLO_THEME_DEEP)
        draw_cottage(assets, camera);
    draw_level_trees(assets, camera);
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
    draw_number(58 + pan, 8, assets->level.required_red, 15);
    draw_text(92 + pan, 8, "HP", 11, 1);
    draw_text(155 + pan, 8, "L", 11, 1);
    draw_text(194 + pan, 8, "M MUSIC S SFX", 23, 1);
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
    draw_number(28 + draw_pan, 8, game->red_collected, 15);
    draw_number(110 + draw_pan, 8, game->player.hp, 15);
    draw_number(169 + draw_pan, 8, game->player.lives, 15);
    if (game->blue_timer) {
        draw_text(181 + draw_pan, 8, "B", 28, 1);
        draw_number(187 + draw_pan, 8, (game->blue_timer + 29) / 30, 28);
    }
}

int video_init(const AssetPack *assets)
{
    unsigned i;
    if (_dos_allocmem(4000, &scratch_segment) != 0) return 0;
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO scratch");
#endif
    scratch = (unsigned char __far *)MK_FP(scratch_segment, 0);
    set_mode_x();
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO mode");
#endif
    for (i = 0; i <= MAX_CAMERA; ++i) {
        tree_origin[i] = (short)(-40 - ((i / 5) % 48));
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
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO tables");
#endif
    outp(0x3c8, 0);
    for (i = 0; i < 768; ++i) outp(0x3c9, assets->palette[i]);
    build_tile_cache(assets);
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO cache");
#endif
    draw_base = display_base = pending_base = GAME_PAGE_0;
    draw_pan = display_pan = 0;
    pending_pan = 0;
    flip_pending = 0;
    set_display_start(display_base, display_pan);
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO display");
#endif
    return 1;
}

void video_shutdown(void)
{
    set_mode(0x03);
    if (scratch_segment != 0) _dos_freemem(scratch_segment);
    scratch = NULL;
    scratch_segment = 0;
    title_cache_valid = hud_cache_valid = 0;
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

/* Every animated frame is built on the game page that is not being scanned.
 * Choosing from display_base, rather than a free-running toggle, also makes the
 * first frame after video_init and every UI transition safe. */
static void begin_hidden_frame(void)
{
    draw_base = display_base == GAME_PAGE_0 ? GAME_PAGE_1 : GAME_PAGE_0;
    draw_pan = 0;
}

static void queue_hidden_frame(unsigned char pan, int state)
{
    pending_base = draw_base;
    pending_pan = pan;
    flip_pending = 1;
    render_state = state;
}

void video_render_game(const GameState *game)
{
    u16 stage;
    begin_hidden_frame();
    draw_world(game, 1);
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_hud(game);
    if (profile_enabled) {
        profile.hud_ticks += profile_elapsed(stage);
        ++profile.frames;
    }
    queue_hidden_frame(draw_pan, RENDER_GAME);
}

static void begin_title_frame(const AssetPack *assets)
{
    GameState preview;
    if (!title_cache_valid) {
        draw_base = TITLE_PAGE;
        draw_pan = 0;
        game_init(&preview, assets);
        preview.camera_x = 0;
        draw_world(&preview, 0);
        fill_rect(32, 26, 256, 92, 1);
        fill_rect(36, 30, 248, 84, 5);
        draw_text(70, 42, "KOLOBOK", 14, 3);
        draw_text(67, 72, "FOREST BERRIES", 15, 1);
        draw_text(55, 88, "ARROWS OR A D TO ROLL", 23, 1);
        draw_text(67, 99, "SPACE OR UP TO JUMP", 23, 1);
        title_cache_valid = 1;
    }
    begin_hidden_frame();
    latch_copy(TITLE_PAGE, draw_base, PAGE_SIZE);
}

void video_render_title(const AssetPack *assets, u32 ticks)
{
    begin_title_frame(assets);
    if ((ticks / 24) & 1) draw_text(94, 128, "PRESS ENTER", 31, 1);
    queue_hidden_frame(0, RENDER_TITLE);
}

void video_render_menu(const AssetPack *assets, u32 ticks, unsigned selection)
{
    static const char *items[3] = {"NEW GAME", "CODEWORD", "QUIT"};
    unsigned i;
    (void)ticks;
    begin_title_frame(assets);
    fill_rect(76, 119, 172, 49, 5);
    for (i = 0; i < 3; ++i) {
        draw_text(104, 122 + i * 14, items[i], i == selection ? 31 : 23, 1);
        if (i == selection) draw_text(91, 122 + i * 14, "1", 14, 1);
    }
    queue_hidden_frame(0, RENDER_TITLE);
}

void video_render_codeword(const AssetPack *assets, const char *word, int invalid)
{
    begin_title_frame(assets);
    fill_rect(58, 112, 204, 57, 5);
    draw_text(76, 119, "ENTER CODEWORD", 15, 1);
    fill_rect(79, 135, 162, 14, 1);
    draw_text(91, 139, word, invalid ? 18 : 31, 1);
    draw_text(67, 155, "ENTER OK  ESC BACK", 23, 1);
    queue_hidden_frame(0, RENDER_TITLE);
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

void video_render_dialogue(const GameState *game, unsigned selection)
{
    static const char *questions[6] = {
        "MANY COATS NO BUTTONS",
        "OAK LEFT BIRCH RIGHT SAFE PATH",
        "PUT THE SONG LINES IN ORDER",
        "RED TAIL QUICK FEET WHO AM I",
        "LONG EARS SHORT TAIL WHO AM I",
        "WHO HOWLS UNDER THE MOON"
    };
    static const char *answers[6][3] = {
        {"1 TURNIP", "2 CABBAGE", "3 ONION"},
        {"1 LEFT", "2 MIDDLE", "3 RIGHT"},
        {"1 ROAD WINDOW HOME", "2 HOME ROAD WINDOW", "3 WINDOW ROAD HOME"},
        {"1 FOX", "2 BEAR", "3 WOLF"},
        {"1 FOX", "2 BEAR", "3 RABBIT"},
        {"1 BEAR", "2 WOLF", "3 FOX"}
    };
    unsigned id = 0, i;
    if (game->active_encounter >= 0)
        id = game->assets->level.encounters[(unsigned)game->active_encounter].dialogue_id;
    if (id < 1 || id > 6) id = 1;
    video_render_game(game);
    fill_rect(12 + draw_pan, 45, 296, 112, 1);
    fill_rect(16 + draw_pan, 49, 288, 104, game->assets->level.theme == KOLO_THEME_DEEP ? 29 : 5);
    draw_text(25 + draw_pan, 57, questions[id - 1], 15, 1);
    for (i = 0; i < 3; ++i)
        draw_text(30 + draw_pan, 82 + i * 20, answers[id - 1][i], i == selection ? 31 : 23, 1);
    draw_text(42 + draw_pan, 140, "ARROWS 1 2 3 ENTER", 14, 1);
    render_state = RENDER_PAUSE;
}

void video_render_game_over(const GameState *game)
{
    video_render_game(game); overlay_box(16);
    draw_text(88 + draw_pan, 77, "GAME OVER", 15, 2);
    draw_text(79 + draw_pan, 113, "ENTER FOR TITLE", 23, 1);
    render_state = RENDER_WIN;
}

static void draw_grandparent(int x,int y,int grandmother,unsigned phase)
{
    unsigned char skin=20,clothes=grandmother?30:27,hair=grandmother?23:22;
    fill_rect(x+5,y,10,9,skin);fill_rect(x+4,y-2,12,4,hair);fill_rect(x+7,y+3,2,2,1);fill_rect(x+12,y+3,2,2,1);
    fill_rect(x+3,y+9,14,20,clothes);fill_rect(x+5,y+29,4,9,9);fill_rect(x+12,y+29,4,9,9);
    if(phase&1){fill_rect(x-3,y+11,7,4,skin);fill_rect(x+17,y+18,8,4,skin);}else{fill_rect(x-1,y+18,6,4,skin);fill_rect(x+17,y+11,7,4,skin);}
    if(grandmother)fill_rect(x+1,y+25,18,5,clothes);else fill_rect(x+4,y+15,12,3,21);
}

static void draw_oven(int x,int y,unsigned phase)
{
    fill_rect(x,y,54,55,23);fill_rect(x+5,y+5,44,50,4);fill_rect(x+12,y+26,30,25,9);
    fill_rect(x+16,y+31,22,16,1);fill_rect(x+20,y+38,5,8,phase&1?19:13);fill_rect(x+28,y+34,6,12,phase&1?13:19);
    fill_rect(x+9,y-5,8,10,23);fill_rect(x+34,y-9,9,14,23);
}

void video_render_intro(const AssetPack *assets, unsigned scene, u32 ticks)
{
    GameState preview;unsigned phase=(unsigned)(ticks/8);game_init(&preview,assets);preview.camera_x=0;
    begin_hidden_frame();
    if(scene==3){int fall=(int)(ticks*3/2);draw_world(&preview,0);draw_text(78,34,"OUT INTO THE GARDEN",15,1);draw_cottage(assets,0);blit_sprite(assets,(unsigned)((ticks>>2)&3),145+(int)ticks/3,55+(fall<92?fall:92));}
    else{
        draw_world(&preview,0);fill_rect(22,25,276,127,21);fill_rect(27,30,266,117,26);fill_rect(27,126,266,21,9);
        if(scene==0){int dough=(int)(ticks<80?ticks/5:16);draw_text(62,36,"GRANDPARENTS BAKE KOLOBOK",15,1);draw_oven(220,66,phase);draw_grandparent(53,78,1,phase);draw_grandparent(112,78,0,phase+1);fill_rect(151,109,48,6,21);fill_rect(166-dough/2,101-dough/3,10+dough,8+dough/3,14);if(ticks>92)blit_sprite(assets,0,230,83);}
        else if(scene==1){int walk=(int)(ticks<90?ticks:90);draw_text(73,36,"COOLING ON THE WINDOW",15,1);fill_rect(56,105,204,9,21);fill_rect(194,55,58,50,1);fill_rect(199,60,48,40,28);draw_grandparent(52+walk,72,1,phase);blit_sprite(assets,0,72+walk,80);if(ticks>92){blit_sprite(assets,0,211,88);fill_rect(216,76-(int)(ticks%12),2,6,4);}}
        else{int roll=65+(int)(ticks*11/10);if(roll>238)roll=238;draw_text(68,36,"KOLOBOK WAKES TURNS AND ROLLS",15,1);fill_rect(55,112,210,8,21);fill_rect(61,58,198,54,1);fill_rect(67,64,186,42,28);draw_grandparent(35,77,1,phase);draw_grandparent(268,77,0,phase+1);blit_sprite(assets,(unsigned)((ticks>>2)&3),roll,95);if((ticks/12)&1)draw_text(139,73,"I AM AWAKE",31,1);}
    }
    draw_text(56, 151, "ENTER NEXT  ESC SKIP", 23, 1);
    queue_hidden_frame(0, RENDER_GAME);
}

void video_render_ending(const GameState *game, u32 ticks)
{
    int arrival=(int)(ticks<100?ticks:100);video_render_game(game);
    fill_rect(18+draw_pan,31,284,148,1);fill_rect(22+draw_pan,35,276,140,21);fill_rect(22+draw_pan,143,276,32,9);
    draw_text(63+draw_pan,43,"KOLOBOK ROLLS HOME AGAIN",15,1);draw_cottage(game->assets,(int)game->assets->level.home.x*16-190);
    blit_sprite(game->assets,(unsigned)((ticks>>2)&3),285-arrival*2,126);
    if(ticks>55){draw_grandparent(88+draw_pan,98,1,(unsigned)(ticks/8));draw_grandparent(130+draw_pan,98,0,(unsigned)(ticks/8)+1);}
    if(ticks>105)draw_text(69+draw_pan,70,"WELCOME HOME DEAR KOLOBOK",31,1);
    if(ticks>145){draw_text(75+draw_pan,84,"THE BERRIES ARE SAFE",14,1);draw_text(111+draw_pan,153,"THE END",15,2);}
    if(ticks>180&&((ticks/20)&1))draw_text(88+draw_pan,166,"ENTER FOR CREDITS",23,1);
    render_state = RENDER_WIN;
}

void video_render_credits(const GameState *game,u32 ticks)
{
    static const char*lines[]={"KOLOBOK EXPANDED ADVENTURE","DESIGN AND PROGRAMMING","D DANILA","ORIGINAL OPL ARRANGEMENTS","PUBLIC DOMAIN FOLK MELODY","TCHAIKOVSKY COLLECTION 1869","BUILT WITH OPEN WATCOM","TESTED WITH DOSBOX X","THANK YOU FOR PLAYING"};
    unsigned i;int base=188-(int)(ticks/2);video_render_game(game);fill_rect(0,24,LOGICAL_W,176,1);
    for(i=0;i<sizeof(lines)/sizeof(lines[0]);++i){int y=base+(int)i*25;if(y>27&&y<190)draw_text(160-(int)strlen(lines[i])*3+draw_pan,y,lines[i],i==0?14:i==8?31:23,1);}
    if(base+(int)(sizeof(lines)/sizeof(lines[0]))*25<45)draw_text(82+draw_pan,166,"ENTER FOR TITLE",15,1);render_state=RENDER_WIN;
}

void video_render_editor(const GameState *game, unsigned cursor_x, unsigned cursor_y,
                         unsigned layer, unsigned tool, int dirty, int valid)
{
    static const char *layers[3]={"TILE","OBJECT","MARKER"};
    int camera=(int)(game->camera_x>>KOLO_FP_SHIFT);
    int x=(int)cursor_x*16-camera+draw_pan;
    int y=HUD_H+(int)cursor_y*16;
    video_render_game(game);
    x=(int)cursor_x*16-camera+draw_pan;
    fill_rect(x,y,16,2,31);fill_rect(x,y+14,16,2,31);
    fill_rect(x,y,2,16,31);fill_rect(x+14,y,2,16,31);
    fill_rect(0,0,LOGICAL_W,HUD_H,1);
    draw_text(3,3,"X",23,1);draw_number(10,3,cursor_x,15);
    draw_text(35,3,"Y",23,1);draw_number(42,3,cursor_y,15);
    draw_text(62,3,layers[layer%3],31,1);draw_text(112,3,"TOOL",23,1);draw_number(143,3,tool,15);
    draw_text(168,3,game->assets->level.theme==0?"GARDEN":game->assets->level.theme==1?"FOREST":"DEEP",11,1);
    draw_text(224,3,valid?"VALID":"INVALID",valid?7:18,1);
    if(dirty)draw_text(299,3,"D",14,1);
    render_state=RENDER_GAME;
}

void video_render_editor_help(const GameState *game)
{
    video_render_game(game);fill_rect(18+draw_pan,31,284,142,1);fill_rect(22+draw_pan,35,276,134,5);
    draw_text(119+draw_pan,42,"KOLOEDIT HELP",15,1);
    draw_text(34+draw_pan,60,"ARROWS MOVE  TAB LAYER",23,1);
    draw_text(34+draw_pan,74,"PGUP PGDN SELECT  SPACE PAINT",23,1);
    draw_text(34+draw_pan,88,"DELETE ERASE  ENTER PROPERTIES",23,1);
    draw_text(34+draw_pan,102,"1 START  2 CHECKPOINT  3 EXIT",23,1);
    draw_text(34+draw_pan,116,"F2 SAVE  F3 VALIDATE  F4 LEVEL",23,1);
    draw_text(34+draw_pan,130,"ESC SAVE DISCARD CANCEL",23,1);
    draw_text(88+draw_pan,151,"F1 CLOSE HELP",31,1);render_state=RENDER_PAUSE;
}

void video_render_editor_exit(const GameState *game)
{
    video_render_game(game);overlay_box(21);
    draw_text(85+draw_pan,73,"UNSAVED CHANGES",15,1);
    draw_text(78+draw_pan,94,"ENTER SAVE AND QUIT",31,1);
    draw_text(78+draw_pan,108,"DELETE DISCARD",23,1);
    draw_text(78+draw_pan,122,"ESC CANCEL",23,1);render_state=RENDER_PAUSE;
}

static const KoloEncounter *video_encounter_for(const LevelData *level,u16 animal_id)
{
    unsigned i;for(i=0;i<level->encounter_count;++i)if(level->encounters[i].animal_id==animal_id)return &level->encounters[i];return 0;
}

void video_render_editor_properties(const GameState *game, unsigned kind,
                                    unsigned index, unsigned field)
{
    static const char *pickup_types[4]={"RED BERRY","BLUE BERRY","SMALL PIE","BIG PIE"};
    static const char *animal_types[4]={"RABBIT","FOX","WOLF","BEAR"};
    static const char *tree_types[3]={"FIR","BIRCH","OAK"};
    static const char *rewards[3]={"NONE","BLUE BERRY","SMALL PIE"};
    static const char *animal_fields[10]={"SUBTYPE","FLAGS","DIALOGUE ID","REWARD","CORRECT ANSWER","PATROL LEFT","PATROL RIGHT","CLIMB TREE","CLIMB TOP","CLIMB BASE"};
    const LevelData *level=&game->assets->level;unsigned row,count=kind==0?2:kind==1?10:3;
    video_render_game(game);fill_rect(25+draw_pan,25,270,158,1);fill_rect(29+draw_pan,29,262,150,5);
    draw_text(80+draw_pan,34,kind==0?"PICKUP PROPERTIES":kind==1?"ANIMAL PROPERTIES":kind==2?"TREE PROPERTIES":"LEVEL PROPERTIES",15,1);
    for(row=0;row<count;++row){int y=50+(int)row*12;draw_text(40+draw_pan,y,row==field?"1":" ",14,1);
        if(kind==0&&index<level->pickup_count){const KoloPickup*p=&level->pickups[index];draw_text(51+draw_pan,y,row==0?"SUBTYPE":"FLAGS",23,1);if(row==0)draw_text(160+draw_pan,y,pickup_types[p->type],row==field?31:15,1);else draw_number(160+draw_pan,y,p->flags,row==field?31:15);}
        else if(kind==2&&index<level->tree_count){const KoloTree*t=&level->trees[index];draw_text(51+draw_pan,y,row==0?"TREE TYPE":row==1?"FLAGS":"HEIGHT",23,1);if(row==0)draw_text(160+draw_pan,y,tree_types[t->type],row==field?31:15,1);else draw_number(160+draw_pan,y,row==1?t->flags:t->height,row==field?31:15);}
        else if(kind==3){draw_text(51+draw_pan,y,row==0?"THEME":row==1?"REQUIRED RED":"CLOUD SEED",23,1);if(row==0)draw_text(160+draw_pan,y,level->theme==0?"GARDEN":level->theme==1?"FOREST":"DEEP",row==field?31:15,1);else draw_number(160+draw_pan,y,row==1?level->required_red:(unsigned)level->cloud_seed,row==field?31:15);}
        else if(kind==1&&index<level->animal_count){const KoloAnimalSpawn*a=&level->animals[index];const KoloEncounter*e=video_encounter_for(level,a->id);draw_text(51+draw_pan,y,animal_fields[row],23,1);
            if(row==0)draw_text(173+draw_pan,y,animal_types[a->type],row==field?31:15,1);else if(row==3)draw_text(173+draw_pan,y,rewards[e?e->reward:0],row==field?31:15,1);
            else if(row==7&&a->tree_id==0xffff)draw_text(173+draw_pan,y,"NONE",row==field?31:15,1);else{unsigned value=row==1?a->flags:row==2?(a->dialogue_id==0xffff?0:a->dialogue_id):row==4?(e?e->correct+1:1):row==5?a->min_x:row==6?a->max_x:row==7?a->tree_id:row==8?a->climb_min:a->climb_max;draw_number(173+draw_pan,y,value,row==field?31:15);}}
    }
    draw_text(46+draw_pan,169,"ARROWS EDIT  ENTER OK  ESC CANCEL",31,1);render_state=RENDER_PAUSE;
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
