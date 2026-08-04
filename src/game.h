#ifndef KOLOBOK_GAME_H
#define KOLOBOK_GAME_H

#include "assets.h"

#define KOLO_FP_SHIFT 8
#define KOLO_FP_ONE 256L
#define KOLO_PLAYER_W 14
#define KOLO_PLAYER_H 14

#define KOLO_EVENT_JUMP       0x01
#define KOLO_EVENT_BERRY      0x02
#define KOLO_EVENT_HURT       0x04
#define KOLO_EVENT_CHECKPOINT 0x08
#define KOLO_EVENT_BOUNCE     0x10
#define KOLO_EVENT_WIN        0x20

typedef struct GameInput {
    u8 left;
    u8 right;
    u8 jump_held;
    u8 jump_pressed;
} GameInput;

typedef struct PlayerState {
    s32 x;
    s32 y;
    s32 vx;
    s32 vy;
    u8 on_ground;
    u8 coyote;
    u8 jump_buffer;
    u8 invulnerable;
    u8 animation;
} PlayerState;

typedef struct EnemyState {
    s32 x;
    s32 y;
    s32 min_x;
    s32 max_x;
    s32 vx;
    u8 type;
} EnemyState;

typedef struct GameState {
    const AssetPack *assets;
    PlayerState player;
    EnemyState enemies[KOLO_MAX_ENEMIES];
    u8 berry_taken[KOLO_MAX_BERRIES];
    u16 berries_collected;
    s32 checkpoint_x;
    s32 checkpoint_y;
    s32 camera_x;
    u32 ticks;
    u16 respawns;
    u8 left_home;
    u8 won;
    u8 events;
} GameState;

void game_init(GameState *game, const AssetPack *assets);
void game_step(GameState *game, const GameInput *input);
void game_respawn(GameState *game);
int game_tile_solid(const GameState *game, int px, int py);
int game_tile_hazard(const GameState *game, int px, int py);

#endif

