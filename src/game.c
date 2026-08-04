#include "game.h"

#include <string.h>

#define ACCEL 48L
#define BRAKE 64L
#define MAX_SPEED 640L
#define GRAVITY 55L
#define MAX_FALL 760L
#define JUMP_SPEED (-850L)
#define SHORT_JUMP (-350L)
#define BOUNCE_SPEED (-660L)

static s32 fp(int value) { return (s32)value << KOLO_FP_SHIFT; }
static int px(s32 value) { return (int)(value >> KOLO_FP_SHIFT); }

static u8 tile_at(const GameState *game, int x, int y)
{
    int tx, ty;
    if (x < 0 || x >= (int)game->assets->map_w * KOLO_TILE_SIZE)
        return 2;
    if (y < 0)
        return 0;
    tx = x / KOLO_TILE_SIZE;
    ty = y / KOLO_TILE_SIZE;
    if (ty >= game->assets->map_h)
        return 3;
    return game->assets->map[ty * game->assets->map_w + tx];
}

int game_tile_solid(const GameState *game, int x, int y)
{
    u8 tile = tile_at(game, x, y);
    return tile == 1 || tile == 2 || tile == 4;
}

int game_tile_hazard(const GameState *game, int x, int y)
{
    return tile_at(game, x, y) == 3;
}

void game_respawn(GameState *game)
{
    game->player.x = game->checkpoint_x;
    game->player.y = game->checkpoint_y;
    game->player.vx = 0;
    game->player.vy = 0;
    game->player.invulnerable = 30;
    game->player.on_ground = 0;
    ++game->respawns;
}

void game_init(GameState *game, const AssetPack *assets)
{
    unsigned i;
    memset(game, 0, sizeof(*game));
    game->assets = assets;
    game->checkpoint_x = fp(assets->home.x + 36);
    game->checkpoint_y = fp(assets->home.y + KOLO_TILE_SIZE - KOLO_PLAYER_H);
    for (i = 0; i < assets->enemy_count; ++i) {
        game->enemies[i].type = assets->enemies[i].type;
        game->enemies[i].x = fp(assets->enemies[i].x);
        game->enemies[i].y = fp(assets->enemies[i].y + KOLO_TILE_SIZE - KOLO_PLAYER_H);
        game->enemies[i].min_x = fp(assets->enemies[i].min_x);
        game->enemies[i].max_x = fp(assets->enemies[i].max_x);
        game->enemies[i].vx = assets->enemies[i].type ? -300L : 220L;
    }
    game_respawn(game);
    game->respawns = 0;
}

static void approach_zero(s32 *value, s32 amount)
{
    if (*value > 0) {
        *value -= amount;
        if (*value < 0) *value = 0;
    } else if (*value < 0) {
        *value += amount;
        if (*value > 0) *value = 0;
    }
}

static void move_horizontal(GameState *game)
{
    PlayerState *p = &game->player;
    int x, y1, y2, tx;
    p->x += p->vx;
    x = px(p->x);
    y1 = px(p->y) + 2;
    y2 = px(p->y) + KOLO_PLAYER_H - 2;
    if (p->vx > 0 && (game_tile_solid(game, x + KOLO_PLAYER_W - 1, y1) ||
                      game_tile_solid(game, x + KOLO_PLAYER_W - 1, y2))) {
        tx = (x + KOLO_PLAYER_W - 1) / KOLO_TILE_SIZE;
        p->x = fp(tx * KOLO_TILE_SIZE - KOLO_PLAYER_W);
        p->vx = 0;
    } else if (p->vx < 0 && (game_tile_solid(game, x, y1) ||
                             game_tile_solid(game, x, y2))) {
        tx = x / KOLO_TILE_SIZE;
        p->x = fp((tx + 1) * KOLO_TILE_SIZE);
        p->vx = 0;
    }
}

static void move_vertical(GameState *game)
{
    PlayerState *p = &game->player;
    int x1, x2, y, ty;
    p->on_ground = 0;
    p->y += p->vy;
    y = px(p->y);
    x1 = px(p->x) + 2;
    x2 = px(p->x) + KOLO_PLAYER_W - 3;
    if (p->vy >= 0 && (game_tile_solid(game, x1, y + KOLO_PLAYER_H) ||
                       game_tile_solid(game, x2, y + KOLO_PLAYER_H))) {
        ty = (y + KOLO_PLAYER_H) / KOLO_TILE_SIZE;
        p->y = fp(ty * KOLO_TILE_SIZE - KOLO_PLAYER_H);
        p->vy = 0;
        p->on_ground = 1;
        p->coyote = 3;
    } else if (p->vy < 0 && (game_tile_solid(game, x1, y) ||
                             game_tile_solid(game, x2, y))) {
        ty = y / KOLO_TILE_SIZE;
        p->y = fp((ty + 1) * KOLO_TILE_SIZE);
        p->vy = 0;
    }
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void update_enemies(GameState *game, int old_bottom)
{
    unsigned i;
    PlayerState *p = &game->player;
    int player_x = px(p->x), player_y = px(p->y);
    for (i = 0; i < game->assets->enemy_count; ++i) {
        EnemyState *enemy = &game->enemies[i];
        int ex, ey;
        if (enemy->type == 1 && px(p->x - enemy->x) > -140 &&
            px(p->x - enemy->x) < 140 && px(p->y - enemy->y) > -28 &&
            px(p->y - enemy->y) < 28) {
            enemy->vx = p->x < enemy->x ? -330L : 330L;
        }
        enemy->x += enemy->vx;
        if (enemy->x < enemy->min_x) {
            enemy->x = enemy->min_x; enemy->vx = -enemy->vx;
        } else if (enemy->x > enemy->max_x) {
            enemy->x = enemy->max_x; enemy->vx = -enemy->vx;
        }
        ex = px(enemy->x); ey = px(enemy->y);
        if (!overlap(player_x, player_y, KOLO_PLAYER_W, KOLO_PLAYER_H,
                     ex, ey, KOLO_PLAYER_W, KOLO_PLAYER_H))
            continue;
        if (p->vy > 0 && old_bottom <= ey + 4) {
            p->y = fp(ey - KOLO_PLAYER_H);
            p->vy = BOUNCE_SPEED;
            game->events |= KOLO_EVENT_BOUNCE;
        } else if (p->invulnerable == 0) {
            game->events |= KOLO_EVENT_HURT;
            game_respawn(game);
            return;
        }
    }
}

static void update_collectibles(GameState *game)
{
    unsigned i;
    int x = px(game->player.x), y = px(game->player.y);
    for (i = 0; i < game->assets->berry_count; ++i) {
        if (!game->berry_taken[i] && overlap(x, y, KOLO_PLAYER_W, KOLO_PLAYER_H,
            game->assets->berries[i].x, game->assets->berries[i].y, 10, 12)) {
            game->berry_taken[i] = 1;
            ++game->berries_collected;
            game->events |= KOLO_EVENT_BERRY;
        }
    }
    if (x > 160) game->left_home = 1;
    if (game->left_home && game->berries_collected == game->assets->berry_count &&
        x < game->assets->home.x + 52) {
        game->won = 1;
        game->events |= KOLO_EVENT_WIN;
    }
    if (x >= game->assets->checkpoint.x - 8 && x <= game->assets->checkpoint.x + 20 &&
        game->checkpoint_x != fp(game->assets->checkpoint.x)) {
        game->checkpoint_x = fp(game->assets->checkpoint.x);
        game->checkpoint_y = fp(game->assets->checkpoint.y + 16 - KOLO_PLAYER_H);
        game->events |= KOLO_EVENT_CHECKPOINT;
    }
}

void game_step(GameState *game, const GameInput *input)
{
    PlayerState *p = &game->player;
    int x, y, old_bottom;
    s32 target_camera;
    game->events = 0;
    ++game->ticks;
    if (game->won) return;
    if (p->invulnerable) --p->invulnerable;
    if (input->jump_pressed) p->jump_buffer = 3;
    if (input->left && !input->right) {
        p->vx -= ACCEL;
        if (p->vx < -MAX_SPEED) p->vx = -MAX_SPEED;
    } else if (input->right && !input->left) {
        p->vx += ACCEL;
        if (p->vx > MAX_SPEED) p->vx = MAX_SPEED;
    } else {
        approach_zero(&p->vx, BRAKE);
    }
    if (p->jump_buffer && (p->on_ground || p->coyote)) {
        p->vy = JUMP_SPEED;
        p->on_ground = 0;
        p->coyote = 0;
        p->jump_buffer = 0;
        game->events |= KOLO_EVENT_JUMP;
    } else if (p->jump_buffer) {
        --p->jump_buffer;
    }
    if (!input->jump_held && p->vy < SHORT_JUMP) p->vy = SHORT_JUMP;
    old_bottom = px(p->y) + KOLO_PLAYER_H;
    p->vy += GRAVITY;
    if (p->vy > MAX_FALL) p->vy = MAX_FALL;
    move_horizontal(game);
    move_vertical(game);
    if (!p->on_ground && p->coyote) --p->coyote;
    update_enemies(game, old_bottom);
    x = px(p->x); y = px(p->y);
    if (game_tile_hazard(game, x + 2, y + KOLO_PLAYER_H) ||
        game_tile_hazard(game, x + KOLO_PLAYER_W - 3, y + KOLO_PLAYER_H) ||
        y > (int)game->assets->map_h * KOLO_TILE_SIZE) {
        game->events |= KOLO_EVENT_HURT;
        game_respawn(game);
    }
    update_collectibles(game);
    if (p->vx != 0 && p->on_ground)
        p->animation = (u8)((game->ticks / 4) % 3);
    else
        p->animation = p->on_ground ? 0 : 2;
    target_camera = p->x - fp(153);
    if (target_camera < 0) target_camera = 0;
    if (target_camera > fp(game->assets->map_w * KOLO_TILE_SIZE - 320))
        target_camera = fp(game->assets->map_w * KOLO_TILE_SIZE - 320);
    game->camera_x += (target_camera - game->camera_x) / 4;
}

