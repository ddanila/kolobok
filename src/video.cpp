#include "video.h"

#include "platform.h"
#include "videoint.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#include "grandparents.inc"
#define PITCH 82
#define PAGE_SIZE (PITCH * SCREEN_H)
#define GAME_PAGE_0 0
#define GAME_PAGE_1 PAGE_SIZE
#define TITLE_PAGE (PAGE_SIZE * 2)
#define HUD_CACHE_BYTES (PITCH * HUD_H)
#define HUD_CACHE_BASE (PAGE_SIZE * 3)
#define TILE_CACHE_BASE (HUD_CACHE_BASE + HUD_CACHE_BYTES * 4)
#define TILE_CACHE_BYTES 64
#define MAX_CAMERA 3776
#define COTTAGE_W 48
#define COTTAGE_H 44
#define HORIZON_Y 112
#define CLOUD_TOP 27
#define TREE_BAND_SPACING 48
#define FRAME_RATE 30

/* Sprite sheet order, also fixed by tools/assets.py. Frames 0..3 are Kolobok
 * rolling; 8 is a sparkle the game does not currently place. */
enum Sprite {
    SPRITE_ROLL_FIRST = 0,
    SPRITE_RED_BERRY = 4, SPRITE_RABBIT, SPRITE_FOX, SPRITE_MARKER, SPRITE_SPARKLE,
    SPRITE_WOLF, SPRITE_BEAR, SPRITE_BLUE_BERRY, SPRITE_SMALL_PIE, SPRITE_BIG_PIE,
    SPRITE_FROZEN
};

static_assert(SPRITE_FROZEN + 1 == MAX_SPRITES, "tools/assets.py SPRITE_COUNT");

#define ROLL_FRAMES 4

typedef struct ThemeStyle {
    unsigned char sky, horizon, trunk, cloud;
    unsigned cloud_spacing, cloud_parallax;
} ThemeStyle;

static const ThemeStyle theme_styles[Theme::COUNT] = {
    {COLOR_SKY,      COLOR_SKY_DEEP, COLOR_PINE, COLOR_CREAM, 68,  8},
    {COLOR_SKY_DEEP, COLOR_PINE,     COLOR_PINE, COLOR_CREAM, 104, 12},
    {COLOR_NIGHT,    COLOR_SOOT,     COLOR_SOOT, COLOR_GREY,  184, 12}
};

static const ThemeStyle *style_for(const LevelData *level)
{
    u8 theme = level->theme < Theme::COUNT ? level->theme : Theme::GARDEN;
    return &theme_styles[theme];
}

static unsigned char __far *scratch;
static unsigned scratch_segment;
static unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
static unsigned draw_base;
static unsigned display_base;
static unsigned pending_base;
unsigned char draw_pan;
static unsigned char display_pan;
static unsigned char pending_pan;
static unsigned char current_map_mask = 0xff;
static bool flip_pending;
static bool vsync_enabled = true;
static bool profile_enabled;
int render_state;
static bool title_cache_valid;
static bool hud_cache_valid;
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

/* The CRTC latches the start address at the onset of vertical retrace, so this
 * must be written before the retrace that should show it, never after. */
static void set_start_address(unsigned base)
{
    outpw(0x3d4, 0x0c | (base & 0xff00));
    outpw(0x3d4, 0x0d | ((base & 0x00ff) << 8));
}

/* Index 0x33 keeps the attribute controller's display-enable bit set, which
 * avoids a transient blank. Write this inside blanking. */
static void set_pel_pan(unsigned char pan)
{
    (void)inp(0x3da);
    outp(0x3c0, 0x33);
    outp(0x3c0, (unsigned char)(pan * 2));
}

static void set_display_start(unsigned base, unsigned char pan)
{
    set_start_address(base);
    set_pel_pan(pan);
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

void fill_rect(int x, int y, int w, int h, unsigned char color)
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

void draw_text(int x, int y, const char *text, unsigned char color, int scale)
{
    while (*text) {
        if (*text != ' ') draw_char(x, y, *text, color, scale);
        x += 6 * scale;
        ++text;
    }
}

void draw_number(int x, int y, unsigned value, unsigned char color)
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

static void blit_tile_plane(ConstFarPtr source, int x, int y,
                            unsigned plane)
{
    int sy;
    int relative = ((int)plane - (x & 3)) & 3;
    ConstFarPtr plane_source = source + relative * 64;
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
    ConstFarPtr source;
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

static void draw_cloud(int x, int y, unsigned shape, unsigned char color)
{
    int wide = 18 + (int)(shape & 3) * 4;
    int high = 3 + (int)((shape >> 1) & 1);
    fill_rect(x, y, wide, high, color);
    fill_rect(x + 3 + (int)(shape & 1) * 3, y - high, wide / 2, high, color);
    if (shape >= 3) fill_rect(x + wide / 2, y - high - 2, wide / 3, 3, color);
}

static void draw_cloud_band(int camera, int pan, const LevelData *level,
                            const ThemeStyle *style)
{
    int x = (int)((level->cloud_seed & 63UL) -
                  (unsigned)(camera / style->cloud_parallax));
    while (x < 0) x += (int)style->cloud_spacing;
    for (; x < SCREEN_W; x += (int)style->cloud_spacing) {
        unsigned shape = (unsigned)((level->cloud_seed +
                                     (u32)(x + camera) / style->cloud_spacing) % 6UL);
        int y = CLOUD_TOP + (int)((level->cloud_seed >> (shape + 1)) & 23UL);
        draw_cloud(x + pan, y, shape, style->cloud);
    }
}

static void draw_tree_band(int camera, int pan, const ThemeStyle *style)
{
    int x = tree_origin[camera];
    int tall = tree_tall[camera];
    while (x < SCREEN_W + TREE_BAND_SPACING) {
        int height = 42 + tall * 12;
        int tier;
        fill_rect(x + pan + 20, HORIZON_Y - height, 7, height, style->trunk);
        for (tier = 0; tier < 4; ++tier)
            fill_rect(x + pan + 8 + tier * 4, 78 - tier * 7,
                      31 - tier * 8, 8, COLOR_PINE);
        if (!(x > -TREE_BAND_SPACING && x < 0)) tall ^= 1;
        x += TREE_BAND_SPACING;
    }
}

static void draw_background(int camera, int pan, int preserve_hud,
                            const LevelData *level)
{
    const ThemeStyle *style = style_for(level);
    int sky_top = preserve_hud ? HUD_H : 0;
    fill_rect(0, sky_top, LOGICAL_W, SCREEN_H - sky_top, style->sky);
    fill_rect(0, HORIZON_Y, LOGICAL_W, SCREEN_H - HORIZON_Y, style->horizon);
    if (camera < 0) camera = 0;
    if (camera > MAX_CAMERA) camera = MAX_CAMERA;
    if (level->theme != Theme::GARDEN) draw_tree_band(camera, pan, style);
    draw_cloud_band(camera, pan, level, style);
}

static void draw_cottage(const AssetPack *assets, int camera)
{
    int x = assets->level.home.x * TILE_SIZE - camera;
    int ground = assets->level.home.y * TILE_SIZE + TILE_SIZE;
    int y = HUD_H + ground - COTTAGE_H;
    fill_rect(x, y, COTTAGE_W, COTTAGE_H, COLOR_WOOD);
    fill_rect(x + 4, y + 4, COTTAGE_W - 8, COTTAGE_H - 4, COLOR_TAN);
    fill_rect(x + 17, y + 20, 15, 24, COLOR_BARK_DARK);
    fill_rect(x + 20, y + 23, 9, 21, COLOR_WOOD);
    fill_rect(x + 5, y + 13, 10, 10, COLOR_NIGHT);
    fill_rect(x + 7, y + 15, 6, 6, COLOR_CYAN);
    fill_rect(x + 34, y + 13, 9, 10, COLOR_NIGHT);
    fill_rect(x + 36, y + 15, 5, 6, COLOR_CYAN);
    fill_rect(x - 4, y - 5, 56, 8, COLOR_RUST);
    fill_rect(x + 2, y - 11, 44, 8, COLOR_RUST);
    fill_rect(x + 8, y - 17, 32, 8, COLOR_RUST);
}

static void draw_level_trees(const AssetPack *assets, int camera)
{
    unsigned i;
    for (i = 0; i < assets->level.tree_count; ++i) {
        const Tree *tree = &assets->level.trees[i];
        int x = tree->x * TILE_SIZE - camera;
        int base = HUD_H + tree->y * TILE_SIZE + TILE_SIZE;
        int height = tree->height * 12;
        unsigned char trunk = tree->type == TreeType::BIRCH ? COLOR_GREY_LIGHT : COLOR_BARK;
        unsigned char leaf = tree->type == TreeType::FIR ? COLOR_PINE : COLOR_FOLIAGE;
        if (x < -24 || x > LOGICAL_W + 8) continue;
        fill_rect(x + 7, base - height, 4, height, trunk);
        if (tree->type == TreeType::FIR) {
            int row;
            for (row = 0; row < tree->height; ++row)
                fill_rect(x + 1 + row, base - height + row * 8,
                          17 - row * 2, 7, leaf);
        } else {
            fill_rect(x - 2, base - height - 2, 23, 10, leaf);
            fill_rect(x + 1, base - height - 8, 17, 9, leaf);
        }
    }
}

static void draw_entity_plane(const GameState *game, int camera, unsigned plane)
{
    static const unsigned pickup_sprite[4] = {
        SPRITE_RED_BERRY, SPRITE_BLUE_BERRY, SPRITE_SMALL_PIE, SPRITE_BIG_PIE
    };
    static const unsigned animal_sprite[4] = {
        SPRITE_RABBIT, SPRITE_FOX, SPRITE_WOLF, SPRITE_BEAR
    };
    const AssetPack *assets = game->assets;
    const LevelData *level = &assets->level;
    unsigned i;
    u16 guardian_id = NO_ID;
    for (i = 0; i < level->encounter_count; ++i)
        if (!game->encounter_solved[i] && level->encounters[i].required) {
            guardian_id = level->encounters[i].animal_id;
            break;
        }
    for (i = 0; i < level->pickup_count; ++i)
        if (!game->pickup_taken[i])
            blit_sprite_plane(assets, pickup_sprite[level->pickups[i].type],
                              level->pickups[i].x * TILE_SIZE + 3 - camera,
                              HUD_H + level->pickups[i].y * TILE_SIZE + 2, plane);
    for (i = 0; i < level->checkpoint_count; ++i)
        blit_sprite_plane(assets, SPRITE_MARKER,
                          level->checkpoints[i].x * TILE_SIZE - camera,
                          HUD_H + level->checkpoints[i].y * TILE_SIZE, plane);
    blit_sprite_plane(assets, SPRITE_MARKER, level->exit.x * TILE_SIZE - camera,
                      HUD_H + level->exit.y * TILE_SIZE, plane);
    if (game->red_collected >= level->required_red && game->guardian_solved)
        blit_sprite_plane(assets, SPRITE_SPARKLE,
                          level->exit.x * TILE_SIZE - camera,
                          HUD_H + level->exit.y * TILE_SIZE - 14, plane);
    for (i = 0; i < level->animal_count; ++i) {
        const EnemyState *enemy = &game->enemies[i];
        int x = (int)(enemy->x >> FP_SHIFT) - camera;
        int y = HUD_H + (int)(enemy->y >> FP_SHIFT);
        blit_sprite_plane(assets, animal_sprite[enemy->type], x, y, plane);
        if (enemy->frozen) blit_sprite_plane(assets, SPRITE_FROZEN, x, y, plane);
        if (enemy->id == guardian_id && ((game->ticks >> 3) & 1) == 0)
            blit_sprite_plane(assets, SPRITE_SPARKLE, x, y - 14, plane);
    }
    blit_sprite_plane(assets, game->player.animation,
                      (int)(game->player.x >> FP_SHIFT) - camera,
                      HUD_H + (int)(game->player.y >> FP_SHIFT), plane);
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

static bool tile_fully_onscreen(int x, int y)
{
    return x >= 0 && x <= LOGICAL_W - TILE_SIZE && y >= 0 && y <= SCREEN_H - TILE_SIZE;
}

/* Fully visible tiles go through the latch path, which copies all four planes in
 * one pass; only tiles clipped by a screen edge pay for the per-plane blitter. */
static void draw_tiles(const AssetPack *assets, int camera, int first, int last)
{
    const LevelData *level = &assets->level;
    int tx, ty;
    unsigned plane;
    begin_latch_writes();
    for (ty = 0; ty < level->height; ++ty)
        for (tx = first; tx < last; ++tx) {
            unsigned tile = level->tile(tx, ty);
            int x = tx * TILE_SIZE - camera;
            int y = HUD_H + ty * TILE_SIZE;
            if (tile && tile < assets->tile_count && tile_fully_onscreen(x, y))
                latch_tile_386(TILE_CACHE_BASE + tile * TILE_CACHE_BYTES,
                    draw_base + (unsigned)y * PITCH + (x >> 2));
        }
    end_latch_writes();
    for (plane = 0; plane < 4; ++plane) {
        set_map_mask((unsigned char)(1 << plane));
        for (ty = 0; ty < level->height; ++ty)
            for (tx = first; tx < last; ++tx) {
                unsigned tile = level->tile(tx, ty);
                int x = tx * TILE_SIZE - camera;
                int y = HUD_H + ty * TILE_SIZE;
                if (tile && tile < assets->tile_count && !tile_fully_onscreen(x, y))
                    blit_tile_plane(assets->tiles + tile * 256, x, y, plane);
            }
    }
}

static void draw_world(const GameState *game, int preserve_hud, int draw_entities)
{
    const AssetPack *assets = game->assets;
    int actual_camera = (int)(game->camera_x >> FP_SHIFT);
    int pan = preserve_hud ? (actual_camera & 3) : 0;
    int camera = actual_camera - pan;
    int first = camera / TILE_SIZE;
    int last = (camera + SCREEN_W + 3) / TILE_SIZE + 1;
    unsigned plane;
    u16 stage;
    draw_pan = (unsigned char)pan;
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_background(actual_camera, pan, preserve_hud, &assets->level);
    if (assets->level.theme == Theme::GARDEN || assets->level.theme == Theme::DEEP)
        draw_cottage(assets, camera);
    draw_level_trees(assets, camera);
    if (profile_enabled) profile.background_ticks += profile_elapsed(stage);
    if (first < 0) first = 0;
    if (last > assets->level.width) last = assets->level.width;
    if (profile_enabled) stage = platform_profile_timer_read();
    draw_tiles(assets, camera, first, last);
    if (profile_enabled) profile.tile_ticks += profile_elapsed(stage);
    if (profile_enabled) stage = platform_profile_timer_read();
    if (draw_entities)
        for (plane = 0; plane < 4; ++plane) {
            set_map_mask((unsigned char)(1 << plane));
            draw_entity_plane(game, camera, plane);
        }
    if (profile_enabled) profile.sprite_ticks += profile_elapsed(stage);
}

static void draw_hud_static(const AssetPack *assets, int pan)
{
    fill_rect(0, 0, LOGICAL_W, HUD_H, COLOR_NIGHT);
    fill_rect(0, HUD_H - 2, LOGICAL_W, 2, COLOR_GOLD);
    blit_sprite(assets, SPRITE_RED_BERRY, 8 + pan, 4);
    draw_text(40 + pan, 8, "OF", COLOR_GREY_LIGHT, 1);
    draw_number(58 + pan, 8, assets->level.required_red, COLOR_WHITE);
    draw_text(92 + pan, 8, "HP", COLOR_GOLD, 1);
    draw_text(155 + pan, 8, "L", COLOR_GOLD, 1);
    draw_text(210 + pan, 8, "GOAL", COLOR_GREY_LIGHT, 1);
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
    hud_cache_valid = true;
}

static void draw_hud(const GameState *game)
{
    if (!hud_cache_valid) build_hud_cache(game->assets);
    latch_copy(HUD_CACHE_BASE + draw_pan * HUD_CACHE_BYTES,
               draw_base, HUD_CACHE_BYTES);
    draw_number(28 + draw_pan, 8, game->red_collected, COLOR_WHITE);
    draw_number(110 + draw_pan, 8, game->player.hp, COLOR_WHITE);
    draw_number(169 + draw_pan, 8, game->player.lives, COLOR_WHITE);
    if (game->blue_timer) {
        unsigned seconds_left = (game->blue_timer + FRAME_RATE - 1) / FRAME_RATE;
        draw_text(181 + draw_pan, 8, "B", COLOR_CYAN, 1);
        draw_number(187 + draw_pan, 8, seconds_left, COLOR_CYAN);
    }
    if (!game->guardian_solved)
        draw_text(240 + draw_pan, 8, "TALK", COLOR_LEMON, 1);
    else if (game->red_collected < game->assets->level.required_red)
        draw_text(240 + draw_pan, 8, "BERRIES", COLOR_LEMON, 1);
    else
        draw_text(240 + draw_pan, 8, "EXIT", COLOR_WHITE, 1);
}

bool video_init(const AssetPack *assets)
{
    unsigned i;
    if (_dos_allocmem(4000, &scratch_segment) != 0) return false;
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
    flip_pending = false;
    set_display_start(display_base, display_pan);
#ifdef KOLO_DEBUG_LOAD
    puts("VIDEO display");
#endif
    return true;
}

void video_shutdown(void)
{
    set_mode(0x03);
    if (scratch_segment != 0) _dos_freemem(scratch_segment);
    scratch = NULL;
    scratch_segment = 0;
    title_cache_valid = hud_cache_valid = false;
    render_state = RENDER_NONE;
    current_map_mask = 0xff;
}

void video_vsync_enable(bool enabled)
{
    vsync_enabled = enabled;
}

void video_present(void)
{
    u16 stage;
    if (profile_enabled) stage = platform_profile_timer_read();
    if (flip_pending) {
        /* Arm the flip first so the coming retrace latches it, then wait for
         * that retrace so this returns only once the new page is on screen and
         * the page just vacated is safe to draw into. Waiting first would post
         * the write immediately after the latch, leaving the old page on screen
         * for one more refresh while the renderer drew into it. */
        set_start_address(pending_base);
        if (vsync_enabled) wait_vblank();
        set_pel_pan(pending_pan);
        display_base = pending_base;
        display_pan = pending_pan;
        flip_pending = false;
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
    flip_pending = true;
    render_state = state;
}

void video_render_game(const GameState *game)
{
    u16 stage;
    begin_hidden_frame();
    draw_world(game, 1, 1);
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
        draw_world(&preview, 0, 1);
        fill_rect(32, 8, 256, 92, COLOR_NIGHT);
        fill_rect(36, 12, 248, 84, COLOR_PINE);
        draw_text(70, 24, "KOLOBOK", COLOR_YELLOW, 3);
        draw_text(67, 54, "FOREST BERRIES", COLOR_WHITE, 1);
        draw_text(55, 70, "ARROWS OR A D TO ROLL", COLOR_GREY_LIGHT, 1);
        draw_text(67, 81, "SPACE OR UP TO JUMP", COLOR_GREY_LIGHT, 1);
        title_cache_valid = true;
    }
    begin_hidden_frame();
    latch_copy(TITLE_PAGE, draw_base, PAGE_SIZE);
}

static void draw_menu_arrow(int x, int y, unsigned char color)
{
    fill_rect(x, y + 3, 8, 2, color);
    fill_rect(x + 5, y + 1, 2, 6, color);
    fill_rect(x + 7, y + 2, 2, 4, color);
}

void video_render_menu(const AssetPack *assets, unsigned selection)
{
    static const char *items[3] = {"NEW GAME", "CODEWORD", "QUIT"};
    unsigned i;
    begin_title_frame(assets);
    fill_rect(76, 119, 172, 49, COLOR_PINE);
    for (i = 0; i < 3; ++i) {
        int y = 122 + (int)i * 14;
        draw_text(104, y, items[i], i == selection ? COLOR_LEMON : COLOR_GREY_LIGHT, 1);
        if (i == selection) draw_menu_arrow(91, y, COLOR_YELLOW);
    }
    queue_hidden_frame(0, RENDER_TITLE);
}

void video_render_codeword(const AssetPack *assets, const char *word, bool invalid)
{
    begin_title_frame(assets);
    fill_rect(58, 112, 204, 57, COLOR_PINE);
    draw_text(76, 119, "ENTER CODEWORD", COLOR_WHITE, 1);
    fill_rect(79, 135, 162, 14, COLOR_NIGHT);
    draw_text(91, 139, word, invalid ? COLOR_RED_BRIGHT : COLOR_LEMON, 1);
    draw_text(67, 155, "ENTER OK  ESC BACK", COLOR_GREY_LIGHT, 1);
    queue_hidden_frame(0, RENDER_TITLE);
}

void overlay_box(unsigned char color)
{
    fill_rect(54 + draw_pan, 61, 212, 78, COLOR_NIGHT);
    fill_rect(58 + draw_pan, 65, 204, 70, color);
}

void video_render_pause(const GameState *game)
{
    if (render_state == RENDER_PAUSE) return;
    video_render_game(game);
    overlay_box(COLOR_PINE);
    draw_text(124 + draw_pan, 76, "PAUSED", COLOR_WHITE, 1);
    draw_text(82 + draw_pan, 96, "ENTER TO CONTINUE", COLOR_GREY_LIGHT, 1);
    draw_text(94 + draw_pan, 112, "ESC TO QUIT", COLOR_LEMON, 1);
    render_state = RENDER_PAUSE;
}

void video_render_win(const GameState *game)
{
    static const char *completed[Theme::COUNT] = {
        "GARDEN COMPLETE", "SMALL FOREST COMPLETE", "DEEP FOREST COMPLETE"
    };
    const LevelData *level = &game->assets->level;
    unsigned theme = level->theme < Theme::COUNT ? level->theme : Theme::GARDEN;
    const char *prompt = theme == Theme::DEEP ? "ENTER FOR ENDING"
                                              : "ENTER FOR NEXT LEVEL";
    if (render_state == RENDER_WIN) return;
    video_render_game(game);
    overlay_box(COLOR_WOOD);
    draw_text((LOGICAL_W - (int)strlen(completed[theme]) * 6) / 2 + draw_pan,
              73, completed[theme], COLOR_YELLOW, 1);
    draw_text(103 + draw_pan, 92, "WELL DONE", COLOR_WHITE, 2);
    draw_text((LOGICAL_W - (int)strlen(prompt) * 6) / 2 + draw_pan,
              119, prompt, COLOR_GREY_LIGHT, 1);
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
    static const char *answers[6][ANSWER_COUNT] = {
        {"1 TURNIP", "2 CABBAGE", "3 ONION"},
        {"1 LEFT", "2 MIDDLE", "3 RIGHT"},
        {"1 ROAD WINDOW HOME", "2 HOME ROAD WINDOW", "3 WINDOW ROAD HOME"},
        {"1 FOX", "2 BEAR", "3 WOLF"},
        {"1 FOX", "2 BEAR", "3 RABBIT"},
        {"1 BEAR", "2 WOLF", "3 FOX"}
    };
    unsigned id = 0, i;
    unsigned char panel = game->assets->level.theme == Theme::DEEP
        ? COLOR_PURPLE : COLOR_PINE;
    if (game->active_encounter >= 0)
        id = game->assets->level.encounters[(unsigned)game->active_encounter].dialogue_id;
    if (id < 1 || id > 6) id = 1;
    video_render_game(game);
    fill_rect(12 + draw_pan, 45, 296, 112, COLOR_NIGHT);
    fill_rect(16 + draw_pan, 49, 288, 104, panel);
    draw_text(25 + draw_pan, 57, questions[id - 1], COLOR_WHITE, 1);
    for (i = 0; i < ANSWER_COUNT; ++i)
        draw_text(30 + draw_pan, 82 + (int)i * 20, answers[id - 1][i],
                  i == selection ? COLOR_LEMON : COLOR_GREY_LIGHT, 1);
    draw_text(42 + draw_pan, 140, "ARROWS 1 2 3 ENTER", COLOR_YELLOW, 1);
    render_state = RENDER_PAUSE;
}

void video_render_game_over(const GameState *game)
{
    video_render_game(game);
    overlay_box(COLOR_BLOOD);
    draw_text(88 + draw_pan, 77, "GAME OVER", COLOR_WHITE, 2);
    draw_text(79 + draw_pan, 113, "ENTER FOR TITLE", COLOR_GREY_LIGHT, 1);
    render_state = RENDER_WIN;
}

/* The intro and ending cast compile from the indexed designer sheet into these
 * inexpensive Mode X rectangle runs. */
static void draw_grandparent(int x, int y, int grandmother, unsigned phase)
{
    unsigned frame = (grandmother ? 0 : 2) + (phase & 1), i;
    for (i = kolo_grandparent_offsets[frame];
         i < kolo_grandparent_offsets[frame + 1]; ++i) {
        const KoloArtRect *rect = &kolo_grandparent_rects[i];
        fill_rect(x + rect->x, y + rect->y, rect->width, rect->height,
                  rect->color);
    }
}

static void draw_oven(int x, int y, unsigned phase)
{
    unsigned char near_flame = phase & 1 ? COLOR_FLAME : COLOR_EMBER;
    unsigned char far_flame = phase & 1 ? COLOR_EMBER : COLOR_FLAME;
    fill_rect(x, y, 54, 55, COLOR_GREY_LIGHT);
    fill_rect(x + 5, y + 5, 44, 50, COLOR_CREAM);
    fill_rect(x + 12, y + 26, 30, 25, COLOR_BARK);
    fill_rect(x + 16, y + 31, 22, 16, COLOR_NIGHT);
    fill_rect(x + 20, y + 38, 5, 8, near_flame);
    fill_rect(x + 28, y + 34, 6, 12, far_flame);
    fill_rect(x + 9, y - 5, 8, 10, COLOR_GREY_LIGHT);
    fill_rect(x + 34, y - 9, 9, 14, COLOR_GREY_LIGHT);
}

static unsigned roll_frame(u32 ticks)
{
    return (unsigned)((ticks >> 2) & (ROLL_FRAMES - 1));
}

static int clamp_up_to(int value, int limit)
{
    return value < limit ? value : limit;
}

/* The interior scenes share a framed room; only scene 3 opens onto the garden. */
static void draw_intro_room(void)
{
    fill_rect(22, 25, 276, 127, COLOR_WOOD);
    fill_rect(27, 30, 266, 117, COLOR_TAN);
    fill_rect(27, 126, 266, 21, COLOR_BARK);
}

static void draw_intro_baking(const AssetPack *assets, u32 ticks, unsigned phase)
{
    int dough = (int)(ticks < 80 ? ticks / 5 : 16);
    draw_text(62, 36, "GRANDPARENTS BAKE KOLOBOK", COLOR_WHITE, 1);
    draw_oven(220, 66, phase);
    draw_grandparent(53, 78, 1, phase);
    draw_grandparent(112, 78, 0, phase + 1);
    fill_rect(151, 109, 48, 6, COLOR_WOOD);
    fill_rect(166 - dough / 2, 101 - dough / 3,
              10 + dough, 8 + dough / 3, COLOR_YELLOW);
    if (ticks > 92) blit_sprite(assets, SPRITE_ROLL_FIRST, 230, 83);
}

static void draw_intro_cooling(const AssetPack *assets, u32 ticks, unsigned phase)
{
    int walk = clamp_up_to((int)(ticks * 6 / 5), 139);
    draw_text(73, 36, "COOLING ON THE WINDOW", COLOR_WHITE, 1);
    fill_rect(56, 105, 204, 9, COLOR_WOOD);
    fill_rect(194, 55, 58, 50, COLOR_NIGHT);
    fill_rect(199, 60, 48, 40, COLOR_CYAN);
    draw_grandparent(52 + walk, 72, 1, phase);
    if (walk < 139) {
        blit_sprite(assets, SPRITE_ROLL_FIRST, 72 + walk, 80);
    } else {
        blit_sprite(assets, SPRITE_ROLL_FIRST, 211, 88);
        fill_rect(216, 76 - (int)(ticks % 12), 2, 6, COLOR_CREAM);
    }
}

static void draw_intro_waking(const AssetPack *assets, u32 ticks, unsigned phase)
{
    int roll = clamp_up_to(65 + (int)(ticks * 11 / 10), 238);
    draw_text(68, 36, "KOLOBOK WAKES TURNS AND ROLLS", COLOR_WHITE, 1);
    fill_rect(55, 112, 210, 8, COLOR_WOOD);
    fill_rect(61, 58, 198, 54, COLOR_NIGHT);
    fill_rect(67, 64, 186, 42, COLOR_CYAN);
    draw_grandparent(35, 77, 1, phase);
    draw_grandparent(268, 77, 0, phase + 1);
    blit_sprite(assets, roll_frame(ticks), roll, 95);
    if ((ticks / 12) & 1) draw_text(139, 73, "I AM AWAKE", COLOR_LEMON, 1);
}

static void draw_intro_leaving(const AssetPack *assets, u32 ticks)
{
    int step = clamp_up_to((int)ticks, 42);
    int fall = clamp_up_to(step * step / 80, 22);
    int cottage_x = assets->level.home.x * TILE_SIZE;
    int ground = assets->level.home.y * TILE_SIZE + TILE_SIZE;
    int cottage_y = HUD_H + ground - COTTAGE_H;
    draw_text(78, 34, "OUT INTO THE GARDEN", COLOR_WHITE, 1);
    blit_sprite(assets, roll_frame(ticks), cottage_x + 2 + step / 3,
                cottage_y + 8 + fall);
}

void video_render_intro(const AssetPack *assets, unsigned scene, u32 ticks)
{
    GameState preview;
    unsigned phase = (unsigned)(ticks / 8);
    game_init(&preview, assets);
    preview.player.x = fp(-PLAYER_W);
    preview.camera_x = 0;
    begin_hidden_frame();
    draw_world(&preview, 0, 0);
    if (scene == 3) {
        draw_intro_leaving(assets, ticks);
    } else {
        draw_intro_room();
        if (scene == 0) draw_intro_baking(assets, ticks, phase);
        else if (scene == 1) draw_intro_cooling(assets, ticks, phase);
        else draw_intro_waking(assets, ticks, phase);
    }
    draw_text(56, 151, "ENTER NEXT  ESC SKIP", COLOR_GREY_LIGHT, 1);
    queue_hidden_frame(0, RENDER_GAME);
}

void video_render_ending(const GameState *game, u32 ticks)
{
    int arrival = clamp_up_to((int)ticks, 100);
    video_render_game(game);
    fill_rect(18 + draw_pan, 31, 284, 148, COLOR_NIGHT);
    fill_rect(22 + draw_pan, 35, 276, 140, COLOR_WOOD);
    fill_rect(22 + draw_pan, 143, 276, 32, COLOR_BARK);
    draw_text(63 + draw_pan, 43, "KOLOBOK ROLLS HOME AGAIN", COLOR_WHITE, 1);
    draw_cottage(game->assets, (int)game->assets->level.home.x * TILE_SIZE - 190);
    blit_sprite(game->assets, roll_frame(ticks), 285 - arrival * 2, 126);
    if (ticks > 55) {
        draw_grandparent(88 + draw_pan, 98, 1, (unsigned)(ticks / 8));
        draw_grandparent(130 + draw_pan, 98, 0, (unsigned)(ticks / 8) + 1);
    }
    if (ticks > 105)
        draw_text(69 + draw_pan, 70, "WELCOME HOME DEAR KOLOBOK", COLOR_LEMON, 1);
    if (ticks > 145) {
        draw_text(75 + draw_pan, 84, "THE BERRIES ARE SAFE", COLOR_YELLOW, 1);
        draw_text(111 + draw_pan, 153, "THE END", COLOR_WHITE, 2);
    }
    if (ticks > 180 && ((ticks / 20) & 1))
        draw_text(88 + draw_pan, 166, "ENTER FOR CREDITS", COLOR_GREY_LIGHT, 1);
    render_state = RENDER_WIN;
}

void video_render_credits(const GameState *game, u32 ticks)
{
    static const char *lines[] = {
        "KOLOBOK EXPANDED ADVENTURE", "DESIGN AND PROGRAMMING", "D DANILA",
        "ORIGINAL OPL ARRANGEMENTS", "PUBLIC DOMAIN FOLK MELODY",
        "KOROBEINIKI RUSSIA 1861", "BUILT WITH OPEN WATCOM",
        "TESTED WITH DOSBOX X", "THANK YOU FOR PLAYING"
    };
    const unsigned count = sizeof(lines) / sizeof(lines[0]);
    int base = 188 - (int)(ticks / 2);
    unsigned i;
    video_render_game(game);
    fill_rect(0, HUD_H, LOGICAL_W, SCREEN_H - HUD_H, COLOR_NIGHT);
    for (i = 0; i < count; ++i) {
        int y = base + (int)i * 25;
        unsigned char color = i == 0 ? COLOR_YELLOW
                            : i == count - 1 ? COLOR_LEMON : COLOR_GREY_LIGHT;
        if (y > 27 && y < 190)
            draw_text(SCREEN_W / 2 - (int)strlen(lines[i]) * 3 + draw_pan, y,
                      lines[i], color, 1);
    }
    if (base + (int)count * 25 < 45)
        draw_text(82 + draw_pan, 166, "ENTER FOR TITLE", COLOR_WHITE, 1);
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

void video_profile_enable(bool enabled)
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

bool video_display_state_valid(void)
{
    unsigned start;
    unsigned char pan;
    if (!(inp(0x3c0) & 0x20)) return false;
    outp(0x3d4, 0x0c);
    start = (unsigned)inp(0x3d5) << 8;
    outp(0x3d4, 0x0d);
    start |= inp(0x3d5);
    (void)inp(0x3da);
    outp(0x3c0, 0x33);
    pan = (unsigned char)inp(0x3c1);
    return start == display_base && pan == (unsigned char)(display_pan * 2);
}

bool video_write_ppm(const char *path, const AssetPack *assets)
{
    FILE *file;
    int y;
    reconstruct_page(display_base, display_pan);
    file = fopen(path, "wb");
    if (file == NULL) return false;
    if (fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H) < 0) {
        fclose(file);
        return false;
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
            return false;
        }
    }
    return fclose(file) == 0;
}
