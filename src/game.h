#ifndef KOLOBOK_GAME_H
#define KOLOBOK_GAME_H

#include "assets.h"

#define KOLO_FP_SHIFT 8
#define KOLO_FP_ONE 256L
#define KOLO_SCREEN_W 320
#define KOLO_SCREEN_H 200
#define KOLO_PLAYER_W 14
#define KOLO_PLAYER_H 14
/* Where the camera holds the player horizontally: slightly left of centre, so
 * more of the level ahead is visible than behind. */
#define KOLO_CAMERA_OFFSET 153
/* A full jump must clear a two-tile platform. The player stands at y=130 on the
 * ground row and has to reach y<=99, because below that the lower body is still
 * inside the platform row and move_horizontal refuses to carry it over the edge.
 * -850 peaked at y=105, which made every platform in the campaign unreachable.
 * -950 barely reaches but lands from only 2.5% of run-up timings; -1025 leaves a
 * usable 26% window. Raising this further starts to disturb enemy encounters. */
#define KOLO_JUMP_SPEED (-1025L)
#define KOLO_ENEMY_BOUNCE_SPEED (-1000L)
#define KOLO_BLUE_FRAMES 300
#define KOLO_FREEZE_FRAMES 90
#define KOLO_INVULNERABLE_FRAMES 30
#define KOLO_FULL_HP 100
#define KOLO_DEFAULT_LIVES 3
#define KOLO_MAX_LIVES 4

struct Event {
    enum Enum {
        JUMP       = 0x0001,
        BERRY      = 0x0002,
        HURT       = 0x0004,
        CHECKPOINT = 0x0008,
        BOUNCE     = 0x0010,
        WIN        = 0x0020,
        BLUE       = 0x0040,
        PIE        = 0x0080,
        DEATH      = 0x0100,
        DIALOGUE   = 0x0200,
        PACIFY     = 0x0400,
        GAME_OVER  = 0x0800
    };
};

struct AiState {
    enum Enum {
        PATROL, WAIT, TELEGRAPH, CHARGE,
        RECOVER, CLIMB, TOP_WAIT, DESCEND
    };
};

typedef struct GameInput {
    u8 left, right, jump_held, jump_pressed, talk_pressed;
} GameInput;

typedef struct PlayerState {
    s32 x, y, vx, vy;
    u8 on_ground, coyote, jump_buffer, invulnerable, animation, enemy_bounce;
    u8 hp, lives;
} PlayerState;

typedef struct EnemyState {
    s32 x, y, min_x, max_x, vx, vy, spawn_y;
    u16 id, tree_id;
    u8 type, state, timer, frozen, pacified, flags;
    u16 retry;
} EnemyState;

typedef struct GameState {
    const AssetPack *assets;
    PlayerState player;
    EnemyState enemies[KOLO_MAX_ENEMIES];
    u8 pickup_taken[KOLO_MAX_PICKUPS];
    u8 encounter_solved[KOLO_MAX_ENCOUNTERS];
    u16 red_collected;
    u16 blue_timer;
    u8 active_dialogue;
    s8 active_encounter;
    s32 checkpoint_x, checkpoint_y, camera_x;
    u32 ticks;
    u16 respawns;
    u8 guardian_solved, won, game_over;
    u16 events;
} GameState;

void game_init(GameState *game, const AssetPack *assets);
void game_init_carry(GameState *game, const AssetPack *assets, u8 hp, u8 lives);
void game_step(GameState *game, const GameInput *input);
void game_respawn(GameState *game);
void game_lose_life(GameState *game);
void game_damage(GameState *game, u8 animal_type, int source_x);
int game_apply_pickup(GameState *game, u8 type);
int game_try_talk(GameState *game);
int game_answer_dialogue(GameState *game, unsigned answer);
int game_exit_ready(const GameState *game);
int game_tile_solid(const GameState *game, int px, int py);
int game_tile_hazard(const GameState *game, int px, int py);
u8 game_surface_at(const GameState *game, int px, int py);
int campaign_codeword_stage(const char *word);

#endif
