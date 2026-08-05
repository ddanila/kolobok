#include "game.h"
#include "trace.h"

#include <string.h>
#include <ctype.h>

#define GRAVITY 55L
#define MAX_FALL 760L
#define SHORT_JUMP (-350L)

#define JUMP_BUFFER_FRAMES 3
#define COYOTE_FRAMES 3
#define CAMERA_EASE_DIVISOR 4
#define WAIT_FRAMES 30

#define RABBIT_HOP_SPEED 220L
#define RABBIT_HOP_LAUNCH (-600L)
#define FOX_PATROL_SPEED 220L
#define FOX_CHASE_SPEED 360L
#define FOX_SIGHT_X 140
#define FOX_SIGHT_Y 28
#define WOLF_PATROL_SPEED 260L
#define WOLF_CHARGE_SPEED 800L
#define WOLF_SIGHT_X 176
#define WOLF_SIGHT_Y 32
#define WOLF_TELEGRAPH_FRAMES 15
#define WOLF_CHARGE_FRAMES 24
#define WOLF_RECOVER_FRAMES 30
#define BEAR_PATROL_SPEED 160L
#define BEAR_CLIMB_SPEED 180L
#define BEAR_TRUNK_REACH 3

#define TALK_REACH 24
#define STOMP_SLACK 4
#define KNOCKBACK_SPEED 480L
#define KNOCKBACK_LIFT (-300L)

#define PICKUP_HITBOX_W 10
#define PICKUP_HITBOX_H 12
#define CHECKPOINT_REACH_BEHIND 8
#define CHECKPOINT_REACH_AHEAD 20

typedef struct SurfaceGrip { s32 accel, reverse, max_speed, brake; } SurfaceGrip;

static s32 fp(int value) { return (s32)value << FP_SHIFT; }
static int px(s32 value) { return (int)(value >> FP_SHIFT); }
static int iabs(int value) { return value < 0 ? -value : value; }
static int tiles_to_px(int tiles) { return tiles * TILE_SIZE; }

static u8 tile_at(const GameState *game, int x, int y)
{
    const LevelData *level = &game->assets->level;
    int tx, ty;
    if (x < 0 || x >= (int)level->width * TILE_SIZE) return Tile::GRASS_BODY;
    if (y < 0) return Tile::AIR;
    tx = x / TILE_SIZE;
    ty = y / TILE_SIZE;
    if (ty >= level->height) return Tile::SPIKES;
    return level->map[ty * level->width + tx];
}

static int tile_has_flag(const GameState *game, int x, int y, u8 flag)
{
    u8 tile = tile_at(game, x, y);
    return tile < game->assets->tile_count &&
           (game->assets->tile_flags[tile] & flag) != 0;
}

int game_tile_solid(const GameState *game, int x, int y)
{
    return tile_has_flag(game, x, y, TileFlag::SOLID);
}

int game_tile_hazard(const GameState *game, int x, int y)
{
    return tile_has_flag(game, x, y, TileFlag::HAZARD);
}

u8 game_surface_at(const GameState *game, int x, int y)
{
    u8 tile = tile_at(game, x, y);
    return tile < game->assets->tile_count ? game->assets->tile_material[tile]
                                           : Surface::AIR;
}

static void event_add(GameState *game, u16 event)
{
    game->events |= event;
}

void game_respawn(GameState *game)
{
    PlayerState *p = &game->player;
    p->x = game->checkpoint_x;
    p->y = game->checkpoint_y;
    p->vx = p->vy = 0;
    p->invulnerable = INVULNERABLE_FRAMES;
    p->on_ground = p->enemy_bounce = 0;
    ++game->respawns;
}

void game_lose_life(GameState *game)
{
    KOLO_LOG(("death x=%d y=%d lv=%u", px(game->player.x), px(game->player.y),
              (unsigned)game->player.lives));
    if (game->player.lives) --game->player.lives;
    game->blue_timer = 0;
    event_add(game, Event::DEATH);
    if (!game->player.lives) {
        game->game_over = 1;
        event_add(game, Event::GAME_OVER);
        return;
    }
    game->player.hp = FULL_HP;
    game_respawn(game);
}

static s32 animal_patrol_speed(u8 type)
{
    if (type == AnimalType::RABBIT) return RABBIT_HOP_SPEED;
    if (type == AnimalType::FOX) return -FOX_PATROL_SPEED;
    if (type == AnimalType::WOLF) return -WOLF_PATROL_SPEED;
    return -BEAR_PATROL_SPEED;
}

static void spawn_enemies(GameState *game)
{
    const LevelData *level = &game->assets->level;
    unsigned i;
    for (i = 0; i < level->animal_count; ++i) {
        const KoloAnimalSpawn *spawn = &level->animals[i];
        EnemyState *enemy = &game->enemies[i];
        enemy->id = spawn->id;
        enemy->type = spawn->type;
        enemy->flags = spawn->flags;
        enemy->tree_id = spawn->tree_id;
        enemy->x = fp(tiles_to_px(spawn->x));
        enemy->y = fp(tiles_to_px(spawn->y) + 2);
        enemy->spawn_y = enemy->y;
        enemy->min_x = fp(tiles_to_px(spawn->min_x));
        enemy->max_x = fp(tiles_to_px(spawn->max_x));
        enemy->vx = animal_patrol_speed(spawn->type);
        if (spawn->type == AnimalType::RABBIT) {
            enemy->state = AiState::WAIT;
            enemy->timer = WAIT_FRAMES;
        }
    }
}

/* Zero means "unspecified", which is how a fresh game asks for the defaults. */
static u8 starting_hp(u8 requested)
{
    return requested ? requested : FULL_HP;
}

static u8 starting_lives(u8 requested)
{
    if (!requested) return DEFAULT_LIVES;
    return requested > MAX_LIVES ? MAX_LIVES : requested;
}

static void initialize(GameState *game, const AssetPack *assets, u8 hp, u8 lives)
{
    const LevelData *level = &assets->level;
    memset(game, 0, sizeof(*game));
    game->assets = assets;
    game->player.hp = starting_hp(hp);
    game->player.lives = starting_lives(lives);
    game->active_encounter = -1;
    game->checkpoint_x = fp(tiles_to_px(level->start.x));
    game->checkpoint_y = fp(tiles_to_px(level->start.y) + 2);
    spawn_enemies(game);
    game_respawn(game);
    game->respawns = 0;
}

void game_init(GameState *game, const AssetPack *assets)
{
    initialize(game, assets, FULL_HP, DEFAULT_LIVES);
}

void game_init_carry(GameState *game, const AssetPack *assets, u8 hp, u8 lives)
{
    initialize(game, assets, hp, lives);
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
    int x, head_y, foot_y;
    p->x += p->vx;
    x = px(p->x);
    head_y = px(p->y) + 2;
    foot_y = px(p->y) + PLAYER_H - 2;
    if (x < 0) {
        p->x = 0;
        p->vx = 0;
    } else if (p->vx > 0 && (game_tile_solid(game, x + PLAYER_W - 1, head_y) ||
                             game_tile_solid(game, x + PLAYER_W - 1, foot_y))) {
        int tx = (x + PLAYER_W - 1) / TILE_SIZE;
        p->x = fp(tiles_to_px(tx) - PLAYER_W);
        p->vx = 0;
    } else if (p->vx < 0 && (game_tile_solid(game, x, head_y) ||
                             game_tile_solid(game, x, foot_y))) {
        int tx = x / TILE_SIZE;
        p->x = fp(tiles_to_px(tx + 1));
        p->vx = 0;
    }
}

static void move_vertical(GameState *game)
{
    PlayerState *p = &game->player;
    int y, left_x, right_x;
    p->on_ground = 0;
    p->y += p->vy;
    y = px(p->y);
    left_x = px(p->x) + 2;
    right_x = px(p->x) + PLAYER_W - 3;
    if (p->vy >= 0 && (game_tile_solid(game, left_x, y + PLAYER_H) ||
                       game_tile_solid(game, right_x, y + PLAYER_H))) {
        int ty = (y + PLAYER_H) / TILE_SIZE;
        p->y = fp(tiles_to_px(ty) - PLAYER_H);
        p->vy = 0;
        p->on_ground = 1;
        p->coyote = COYOTE_FRAMES;
    } else if (p->vy < 0 && (game_tile_solid(game, left_x, y) ||
                             game_tile_solid(game, right_x, y))) {
        int ty = y / TILE_SIZE;
        p->y = fp(tiles_to_px(ty + 1));
        p->vy = 0;
    }
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void game_damage(GameState *game, u8 type, int source_x)
{
    static const u8 damage[4] = {10, 25, 40, 50};
    PlayerState *p = &game->player;
    u8 amount;
    if (p->invulnerable || game->game_over) return;
    amount = damage[type <= AnimalType::BEAR ? type : AnimalType::BEAR];
    if (type <= AnimalType::FOX && p->hp <= amount) {
        p->hp = 1;
    } else if (p->hp <= amount) {
        p->hp = 0;
        event_add(game, Event::HURT);
        game_lose_life(game);
        return;
    } else {
        p->hp = (u8)(p->hp - amount);
    }
    p->invulnerable = INVULNERABLE_FRAMES;
    p->vx = px(p->x) < source_x ? -KNOCKBACK_SPEED : KNOCKBACK_SPEED;
    p->vy = KNOCKBACK_LIFT;
    event_add(game, Event::HURT);
}

static void bound_enemy(EnemyState *e)
{
    if (e->x < e->min_x) {
        e->x = e->min_x;
        e->vx = e->vx < 0 ? -e->vx : e->vx;
    } else if (e->x > e->max_x) {
        e->x = e->max_x;
        e->vx = e->vx > 0 ? -e->vx : e->vx;
    }
}

static void patrol(EnemyState *e, s32 speed)
{
    e->vx = e->vx < 0 ? -speed : speed;
    e->x += e->vx;
    bound_enemy(e);
}

static int tree_x(const GameState *game, u16 id)
{
    const LevelData *level = &game->assets->level;
    unsigned i;
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == id) return tiles_to_px(level->trees[i].x);
    return -1;
}

static void update_rabbit(EnemyState *e)
{
    if (e->state == AiState::WAIT) {
        if (e->timer) {
            --e->timer;
            return;
        }
        e->state = AiState::PATROL;
        e->vx = e->vx < 0 ? -RABBIT_HOP_SPEED : RABBIT_HOP_SPEED;
        e->vy = RABBIT_HOP_LAUNCH;
        e->y = e->spawn_y;
        return;
    }
    e->x += e->vx;
    e->y += e->vy;
    e->vy += GRAVITY;
    if (e->y >= e->spawn_y && e->vy > 0) {
        e->y = e->spawn_y;
        e->vy = 0;
        e->state = AiState::WAIT;
        e->timer = WAIT_FRAMES;
    }
    bound_enemy(e);
}

static void update_fox(EnemyState *e, int dx, int dy)
{
    if (iabs(dx) < FOX_SIGHT_X && iabs(dy) < FOX_SIGHT_Y)
        e->vx = dx < 0 ? -FOX_CHASE_SPEED : FOX_CHASE_SPEED;
    else
        e->vx = e->vx < 0 ? -FOX_PATROL_SPEED : FOX_PATROL_SPEED;
    e->x += e->vx;
    bound_enemy(e);
}

static void update_wolf(EnemyState *e, int dx, int dy)
{
    switch (e->state) {
    case AiState::PATROL:
        if (iabs(dx) < WOLF_SIGHT_X && iabs(dy) < WOLF_SIGHT_Y) {
            e->state = AiState::TELEGRAPH;
            e->timer = WOLF_TELEGRAPH_FRAMES;
            e->vx = 0;
        } else {
            patrol(e, WOLF_PATROL_SPEED);
        }
        break;
    case AiState::TELEGRAPH:
        if (!--e->timer) {
            e->state = AiState::CHARGE;
            e->timer = WOLF_CHARGE_FRAMES;
            e->vx = dx < 0 ? -WOLF_CHARGE_SPEED : WOLF_CHARGE_SPEED;
        }
        break;
    case AiState::CHARGE:
        e->x += e->vx;
        bound_enemy(e);
        if (!--e->timer) {
            e->state = AiState::RECOVER;
            e->timer = WOLF_RECOVER_FRAMES;
            e->vx = 0;
        }
        break;
    case AiState::RECOVER:
        if (!--e->timer) {
            e->state = AiState::PATROL;
            e->vx = WOLF_PATROL_SPEED;
        }
        break;
    default:
        break;
    }
}

static void update_bear(GameState *game, EnemyState *e,
                        const KoloAnimalSpawn *spawn)
{
    int trunk_x = tree_x(game, e->tree_id);
    switch (e->state) {
    case AiState::PATROL:
        patrol(e, BEAR_PATROL_SPEED);
        if (trunk_x >= 0 && iabs(px(e->x) - trunk_x) < BEAR_TRUNK_REACH) {
            e->x = fp(trunk_x);
            e->vx = 0;
            e->state = AiState::CLIMB;
        }
        break;
    case AiState::CLIMB:
        e->y -= BEAR_CLIMB_SPEED;
        if (px(e->y) <= tiles_to_px(spawn->climb_min) + 2) {
            e->y = fp(tiles_to_px(spawn->climb_min) + 2);
            e->state = AiState::TOP_WAIT;
            e->timer = WAIT_FRAMES;
        }
        break;
    case AiState::TOP_WAIT:
        if (!--e->timer) e->state = AiState::DESCEND;
        break;
    case AiState::DESCEND:
        e->y += BEAR_CLIMB_SPEED;
        if (px(e->y) >= tiles_to_px(spawn->climb_max) + 2) {
            e->y = e->spawn_y;
            e->state = AiState::WAIT;
            e->timer = WAIT_FRAMES;
        }
        break;
    case AiState::WAIT:
        if (!--e->timer) {
            e->state = AiState::PATROL;
            e->vx = -BEAR_PATROL_SPEED;
        }
        break;
    default:
        break;
    }
}

static void update_enemy_ai(GameState *game, unsigned index)
{
    EnemyState *e = &game->enemies[index];
    const KoloAnimalSpawn *spawn = &game->assets->level.animals[index];
    int dx = px(game->player.x - e->x);
    int dy = px(game->player.y - e->y);
    if (e->retry) --e->retry;
    if (e->pacified) return;
    if (e->frozen) {
        --e->frozen;
        return;
    }
    switch (e->type) {
    case AnimalType::RABBIT: update_rabbit(e); break;
    case AnimalType::FOX:    update_fox(e, dx, dy); break;
    case AnimalType::WOLF:   update_wolf(e, dx, dy); break;
    default:                 update_bear(game, e, spawn); break;
    }
}

static void update_enemies(GameState *game, int old_bottom)
{
    PlayerState *p = &game->player;
    int player_x = px(p->x), player_y = px(p->y);
    unsigned i;
    for (i = 0; i < game->assets->level.animal_count; ++i) {
        EnemyState *e = &game->enemies[i];
        int ex, ey;
        update_enemy_ai(game, i);
        ex = px(e->x);
        ey = px(e->y);
        if (e->pacified ||
            !overlap(player_x, player_y, PLAYER_W, PLAYER_H,
                     ex, ey, PLAYER_W, PLAYER_H)) continue;
        if (p->vy > 0 && old_bottom <= ey + STOMP_SLACK) {
            p->y = fp(ey - PLAYER_H);
            p->vy = ENEMY_BOUNCE_SPEED;
            p->enemy_bounce = 1;
            e->frozen = FREEZE_FRAMES;
            event_add(game, Event::BOUNCE);
        } else if (!e->frozen) {
            game_damage(game, e->type, ex);
            if (game->game_over || game->respawns) return;
        }
    }
}

int game_apply_pickup(GameState *game, u8 type)
{
    PlayerState *p = &game->player;
    if (type == PickupType::RED) {
        ++game->red_collected;
        event_add(game, Event::BERRY);
        return 1;
    }
    if (type == PickupType::BLUE) {
        game->blue_timer = BLUE_FRAMES;
        event_add(game, Event::BLUE);
        return 1;
    }
    if (type == PickupType::SMALL_PIE) {
        if (p->hp == FULL_HP && p->lives >= DEFAULT_LIVES) return 0;
        p->hp = FULL_HP;
        if (p->lives < DEFAULT_LIVES) ++p->lives;
        event_add(game, Event::PIE);
        return 1;
    }
    p->hp = FULL_HP;
    if (p->lives < DEFAULT_LIVES) p->lives = DEFAULT_LIVES;
    else if (p->lives < MAX_LIVES) ++p->lives;
    event_add(game, Event::PIE);
    return 1;
}

static void collect_pickups(GameState *game)
{
    const LevelData *level = &game->assets->level;
    int x = px(game->player.x), y = px(game->player.y);
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i) {
        const KoloPickup *pickup = &level->pickups[i];
        if (game->pickup_taken[i]) continue;
        if (!overlap(x, y, PLAYER_W, PLAYER_H,
                     tiles_to_px(pickup->x) + 3, tiles_to_px(pickup->y) + 2,
                     PICKUP_HITBOX_W, PICKUP_HITBOX_H)) continue;
        if (game_apply_pickup(game, pickup->type)) game->pickup_taken[i] = 1;
    }
}

static void claim_checkpoints(GameState *game)
{
    const LevelData *level = &game->assets->level;
    int x = px(game->player.x);
    unsigned i;
    for (i = 0; i < level->checkpoint_count; ++i) {
        int cx = tiles_to_px(level->checkpoints[i].x);
        if (x < cx - CHECKPOINT_REACH_BEHIND || x > cx + CHECKPOINT_REACH_AHEAD) continue;
        if (game->checkpoint_x == fp(cx)) continue;
        game->checkpoint_x = fp(cx);
        game->checkpoint_y = fp(tiles_to_px(level->checkpoints[i].y) + 2);
        event_add(game, Event::CHECKPOINT);
    }
}

static void update_collectibles(GameState *game)
{
    const LevelData *level = &game->assets->level;
    collect_pickups(game);
    claim_checkpoints(game);
    if (game_exit_ready(game) &&
        iabs(px(game->player.x) - tiles_to_px(level->exit.x)) < TILE_SIZE) {
        game->won = 1;
        event_add(game, Event::WIN);
    }
}

int game_exit_ready(const GameState *game)
{
    return game->red_collected >= game->assets->level.required_red &&
           game->guardian_solved;
}

static EnemyState *enemy_by_id(GameState *game, u16 animal_id)
{
    unsigned i;
    for (i = 0; i < game->assets->level.animal_count; ++i)
        if (game->enemies[i].id == animal_id) return &game->enemies[i];
    return 0;
}

int game_try_talk(GameState *game)
{
    const LevelData *level = &game->assets->level;
    int x = px(game->player.x), y = px(game->player.y);
    unsigned i;
    if (game->active_dialogue) return 1;
    for (i = 0; i < level->encounter_count; ++i) {
        EnemyState *e;
        if (game->encounter_solved[i]) continue;
        e = enemy_by_id(game, level->encounters[i].animal_id);
        if (!e || e->retry) continue;
        if (iabs(x - px(e->x)) > TALK_REACH || iabs(y - px(e->y)) > TALK_REACH) continue;
        game->active_dialogue = 1;
        game->active_encounter = (s8)i;
        event_add(game, Event::DIALOGUE);
        return 1;
    }
    return 0;
}

static void grant_reward(GameState *game, u8 reward)
{
    if (reward == Reward::BLUE) game_apply_pickup(game, PickupType::BLUE);
    else if (reward == Reward::SMALL_PIE) game_apply_pickup(game, PickupType::SMALL_PIE);
}

int game_answer_dialogue(GameState *game, unsigned answer)
{
    const KoloEncounter *encounter;
    EnemyState *e;
    unsigned index;
    if (!game->active_dialogue || game->active_encounter < 0 || answer > 2) return 0;
    index = (unsigned)game->active_encounter;
    encounter = &game->assets->level.encounters[index];
    game->active_dialogue = 0;
    game->active_encounter = -1;
    e = enemy_by_id(game, encounter->animal_id);
    if (!e) return 0;
    if (answer != encounter->correct) {
        e->retry = encounter->retry_frames;
        game_damage(game, e->type, px(e->x));
        return -1;
    }
    game->encounter_solved[index] = 1;
    e->pacified = 1;
    if (encounter->required) game->guardian_solved = 1;
    grant_reward(game, encounter->reward);
    event_add(game, Event::PACIFY);
    return 1;
}

static SurfaceGrip grip_for(u8 surface)
{
    SurfaceGrip grip;
    if (surface == Surface::SAND) {
        grip.accel = 24; grip.reverse = 48; grip.max_speed = 512; grip.brake = 32;
    } else if (surface == Surface::ICE) {
        grip.accel = 16; grip.reverse = 24; grip.max_speed = 704; grip.brake = 4;
    } else if (surface == Surface::AIR) {
        grip.accel = 16; grip.reverse = 16; grip.max_speed = 640; grip.brake = 0;
    } else {
        grip.accel = 32; grip.reverse = 64; grip.max_speed = 640; grip.brake = 20;
    }
    return grip;
}

static SurfaceGrip player_grip(const GameState *game)
{
    const PlayerState *p = &game->player;
    u8 surface = p->on_ground
        ? game_surface_at(game, px(p->x) + PLAYER_W / 2, px(p->y) + PLAYER_H + 1)
        : Surface::AIR;
    SurfaceGrip grip = grip_for(surface);
    if (game->blue_timer) {
        grip.accel = grip.accel * 5 / 4;
        grip.reverse = grip.reverse * 5 / 4;
        grip.max_speed = grip.max_speed * 3 / 2;
    }
    return grip;
}

static void apply_run_input(PlayerState *p, const GameInput *input,
                            const SurfaceGrip *grip)
{
    if (input->left && !input->right) {
        p->vx -= p->vx > 0 ? grip->reverse : grip->accel;
        if (p->vx < -grip->max_speed) p->vx = -grip->max_speed;
    } else if (input->right && !input->left) {
        p->vx += p->vx < 0 ? grip->reverse : grip->accel;
        if (p->vx > grip->max_speed) p->vx = grip->max_speed;
    } else if (grip->brake) {
        approach_zero(&p->vx, grip->brake);
    }
}

static void apply_jump_input(GameState *game, const GameInput *input)
{
    PlayerState *p = &game->player;
    if (p->jump_buffer && (p->on_ground || p->coyote)) {
        p->vy = JUMP_SPEED;
        p->enemy_bounce = p->on_ground = p->coyote = p->jump_buffer = 0;
        event_add(game, Event::JUMP);
    } else if (p->jump_buffer) {
        --p->jump_buffer;
    }
    if (!input->jump_held && !p->enemy_bounce && p->vy < SHORT_JUMP) p->vy = SHORT_JUMP;
}

static int fell_to_death(const GameState *game)
{
    const PlayerState *p = &game->player;
    int x = px(p->x), foot_y = px(p->y) + PLAYER_H;
    return game_tile_hazard(game, x + 2, foot_y) ||
           game_tile_hazard(game, x + PLAYER_W - 3, foot_y) ||
           px(p->y) > (int)game->assets->level.height * TILE_SIZE;
}

static void update_camera(GameState *game)
{
    s32 furthest = fp((int)game->assets->level.width * TILE_SIZE - SCREEN_W);
    s32 target = game->player.x - fp(CAMERA_OFFSET);
    if (target < 0) target = 0;
    if (target > furthest) target = furthest;
    game->camera_x += (target - game->camera_x) / CAMERA_EASE_DIVISOR;
}

void game_step(GameState *game, const GameInput *input)
{
    PlayerState *p = &game->player;
    SurfaceGrip grip;
    int old_bottom;
    game->events = 0;
    ++game->ticks;
    if (game->won || game->game_over || game->active_dialogue) return;
    if (p->invulnerable) --p->invulnerable;
    if (game->blue_timer) --game->blue_timer;
    if (input->talk_pressed && game_try_talk(game)) return;

    grip = player_grip(game);
    if (input->jump_pressed) p->jump_buffer = JUMP_BUFFER_FRAMES;
    apply_run_input(p, input, &grip);
    apply_jump_input(game, input);

    old_bottom = px(p->y) + PLAYER_H;
    p->vy += GRAVITY;
    if (p->vy > MAX_FALL) p->vy = MAX_FALL;
    move_horizontal(game);
    move_vertical(game);
    if (p->vy >= 0) p->enemy_bounce = 0;
    if (!p->on_ground && p->coyote) --p->coyote;

    update_enemies(game, old_bottom);
    if (game->game_over) return;
    if (fell_to_death(game)) {
        event_add(game, Event::HURT);
        game_lose_life(game);
        return;
    }
    update_collectibles(game);
    if (p->vx) p->animation = (u8)((px(p->x) >> 2) & 3);
    update_camera(game);
}

int campaign_codeword_stage(const char *word)
{
    char upper[9];
    unsigned i = 0;
    while (word[i] && i < 8) {
        upper[i] = (char)toupper((unsigned char)word[i]);
        ++i;
    }
    upper[i] = 0;
    if (!strcmp(upper, "REPKA")) return 0;
    if (!strcmp(upper, "TEREMOK")) return 1;
    if (!strcmp(upper, "MOROZKO")) return 2;
    return -1;
}
