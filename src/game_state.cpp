#include "game.h"

#include <string.h>

/* Building a GameState from a level, and putting the player back on it.
 *
 * This is the only part of the game KOLOEDIT.EXE needs: the editor rebuilds a
 * preview state every frame so the shared renderer has something to draw, and
 * video.cpp does the same behind the title and intro screens. Keeping it apart
 * from the physics and AI in game.cpp means neither binary links a simulation
 * it never steps. */

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
        const AnimalSpawn *spawn = &level->animals[i];
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

void game_init(GameState *game, const AssetPack *assets, u8 hp, u8 lives)
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
