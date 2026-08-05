#include "assets.h"
#include "game.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ARCHIVE "build/KOLOBOK.DAT"
#define GARDEN_LEVEL "build/GARDEN.KLV"
#define FOREST_LEVEL "build/SFOREST.KLV"
#define DEEP_LEVEL "build/DFOREST.KLV"

#define GROUND_ROW 9
#define ERROR_SIZE 96

/* Per-frame acceleration each surface grants, as chosen by grip_for in game.c. */
#define GRASS_ACCEL 32
#define SAND_ACCEL 24
#define ICE_ACCEL 16
#define AIR_ACCEL 16
#define BOOSTED_GRASS_ACCEL 40

/* Tile columns whose ground material is fixed by the level JSON. */
#define GARDEN_GRASS_COLUMN 6
#define GARDEN_SAND_COLUMN 33
#define FOREST_ICE_COLUMN 66

#define FOREST_WOLF 6
#define DEEP_BEAR 6

static s32 to_fp(int value) { return (s32)value << FP_SHIFT; }

static void load(AssetPack *pack, const char *bank, const char *level)
{
    char error[ERROR_SIZE];
    assert(assets_load_bank(pack, ARCHIVE, bank, level, error, sizeof(error)));
}

static void step(GameState *game, unsigned count)
{
    GameInput idle;
    memset(&idle, 0, sizeof(idle));
    while (count--) game_step(game, &idle);
}

/* Parks the player at rest on the ground row of one tile column, so the next step
 * reveals the acceleration that column's material grants. */
static void stand_at(GameState *game, int column)
{
    game->player.on_ground = 1;
    game->player.x = to_fp(column * TILE_SIZE);
    game->player.y = to_fp(GROUND_ROW * TILE_SIZE - PLAYER_H);
    game->player.vx = 0;
}

static unsigned animal_index_for_encounter(const AssetPack *pack, unsigned encounter)
{
    unsigned i;
    for (i = 0; i < pack->level.animal_count; ++i)
        if (pack->level.animals[i].id == pack->level.encounters[encounter].animal_id)
            return i;
    assert(0 && "encounter refers to an animal the level does not contain");
    return 0;
}

static void stand_on_animal(GameState *game, unsigned animal)
{
    game->player.x = game->enemies[animal].x;
    game->player.y = game->enemies[animal].y;
}

static void test_assets_and_levels(void)
{
    AssetPack p;
    load(&p, "GARDEN", GARDEN_LEVEL);
    assert(p.level.width == 96 && p.level.height == LEVEL_HEIGHT);
    assert(p.tile_count == Tile::COUNT && p.sprite_count == MAX_SPRITES);
    assert(p.level.required_red == 6 && p.level.pickup_count == 10);
    assert(p.level.animal_count == 8);
    assets_free(&p);

    load(&p, "FOREST", FOREST_LEVEL);
    assert(p.level.width == 128 && p.level.required_red == 8);
    assets_free(&p);

    load(&p, "DEEP", DEEP_LEVEL);
    assert(p.level.width == 160 && p.level.required_red == 10);
    assert(p.level.animal_count == 9);
    assets_free(&p);
}

/* Flipping one payload bit must be caught by the KLV checksum, rather than
 * surfacing later as a corrupt level. */
static void test_crc_rejection(void)
{
    const char *corrupt = "build/BAD.KLV";
    FILE *source = fopen(GARDEN_LEVEL, "rb");
    FILE *target = fopen(corrupt, "wb");
    AssetPack p;
    char error[ERROR_SIZE];
    long offset = 0;
    int byte;
    assert(source && target);
    while ((byte = fgetc(source)) != EOF) {
        if (offset++ == 100) byte ^= 1;
        fputc(byte, target);
    }
    fclose(source);
    fclose(target);
    assert(!assets_load_bank(&p, ARCHIVE, "GARDEN", corrupt, error, sizeof(error)));
    assert(strstr(error, "checksum") != 0);
    remove(corrupt);
}

/* level_validate is the only gate a hand-written or tool-generated level passes
 * through before the runtime trusts it, so every field the runtime dereferences
 * or drives an entity to must be rejected here when it is out of range. */
static void test_level_validation(void)
{
    LevelData level;
    char error[ERROR_SIZE];
    u16 saved;
    assert(level_load(&level, DEEP_LEVEL, error, sizeof(error)));
    assert(level_validate(&level, error, sizeof(error)));

    /* A climb range past the last row sends update_bear off the bottom of the
     * map; an inverted one makes the bear climb and descend past each other. */
    saved = level.animals[DEEP_BEAR].climb_max;
    level.animals[DEEP_BEAR].climb_max = LEVEL_HEIGHT;
    assert(!level_validate(&level, error, sizeof(error)));
    assert(strstr(error, "climb") != 0);
    level.animals[DEEP_BEAR].climb_min = LEVEL_HEIGHT - 1;
    level.animals[DEEP_BEAR].climb_max = 0;
    assert(!level_validate(&level, error, sizeof(error)));
    assert(strstr(error, "climb") != 0);
    level.animals[DEEP_BEAR].climb_min = 0;
    level.animals[DEEP_BEAR].climb_max = saved;
    assert(level_validate(&level, error, sizeof(error)));

    /* NO_ID means "no dialogue" and stays legal; anything else has to survive
     * the narrowing to the encounter's one-byte dialogue ID. */
    saved = level.animals[0].dialogue_id;
    level.animals[0].dialogue_id = MAX_DIALOGUE_ID + 1;
    assert(!level_validate(&level, error, sizeof(error)));
    assert(strstr(error, "dialogue") != 0);
    level.animals[0].dialogue_id = NO_ID;
    assert(level_validate(&level, error, sizeof(error)));
    level.animals[0].dialogue_id = saved;

    level_free(&level);
}

static void test_surface_physics(void)
{
    AssetPack p;
    GameState g;
    GameInput right;
    memset(&right, 0, sizeof(right));
    right.right = 1;

    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    g.player.invulnerable = 255;
    stand_at(&g, GARDEN_GRASS_COLUMN);
    game_step(&g, &right);
    assert(g.player.vx == GRASS_ACCEL);

    stand_at(&g, GARDEN_SAND_COLUMN);
    game_step(&g, &right);
    assert(g.player.vx == SAND_ACCEL);

    /* Airborne, the material underfoot must not matter. */
    g.player.on_ground = 0;
    g.player.x = to_fp(GARDEN_SAND_COLUMN * TILE_SIZE);
    g.player.y = to_fp(7 * TILE_SIZE);
    g.player.vx = 0;
    game_step(&g, &right);
    assert(g.player.vx == AIR_ACCEL);
    assets_free(&p);

    load(&p, "FOREST", FOREST_LEVEL);
    game_init(&g, &p);
    stand_at(&g, FOREST_ICE_COLUMN);
    game_step(&g, &right);
    assert(g.player.vx == ICE_ACCEL);
    assets_free(&p);
}

static void test_boost_and_pies(void)
{
    AssetPack p;
    GameState g;
    GameInput right;
    memset(&right, 0, sizeof(right));
    right.right = 1;
    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);

    assert(game_apply_pickup(&g, PickupType::BLUE));
    assert(g.blue_timer == BLUE_FRAMES);
    g.player.on_ground = 1;
    g.player.vx = 0;
    game_step(&g, &right);
    assert(g.player.vx == BOOSTED_GRASS_ACCEL);
    assert(g.blue_timer == BLUE_FRAMES - 1);

    /* A second blue berry refreshes the boost rather than stacking it. */
    step(&g, 10);
    game_apply_pickup(&g, PickupType::BLUE);
    assert(g.blue_timer == BLUE_FRAMES);
    step(&g, BLUE_FRAMES);
    assert(g.blue_timer == 0);

    /* A small pie is refused when it would do nothing, so it stays on the map. */
    g.player.hp = FULL_HP;
    g.player.lives = DEFAULT_LIVES;
    assert(!game_apply_pickup(&g, PickupType::SMALL_PIE));

    g.player.hp = 50;
    assert(game_apply_pickup(&g, PickupType::SMALL_PIE));
    assert(g.player.hp == FULL_HP && g.player.lives == DEFAULT_LIVES);

    g.player.hp = 50;
    g.player.lives = 2;
    game_apply_pickup(&g, PickupType::SMALL_PIE);
    assert(g.player.hp == FULL_HP && g.player.lives == DEFAULT_LIVES);

    /* A big pie grants the bonus life only from a full three. */
    g.player.lives = DEFAULT_LIVES;
    game_apply_pickup(&g, PickupType::BIG_PIE);
    assert(g.player.lives == MAX_LIVES);
    g.player.lives = 1;
    game_apply_pickup(&g, PickupType::BIG_PIE);
    assert(g.player.lives == DEFAULT_LIVES);
    assets_free(&p);
}

static void test_damage_lives_checkpoint(void)
{
    AssetPack p;
    GameState g;
    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    g.player.invulnerable = 0;

    game_damage(&g, AnimalType::RABBIT, 1000);
    assert(g.player.hp == 90);
    assert(g.player.invulnerable == INVULNERABLE_FRAMES);
    assert(g.player.vx == -480 && g.player.vy == -300);

    /* Rabbit and fox contact can never take the last hit point. */
    g.player.invulnerable = 0;
    g.player.hp = 20;
    game_damage(&g, AnimalType::FOX, 0);
    assert(g.player.hp == 1 && g.player.lives == DEFAULT_LIVES);

    /* A wolf can, and respawning keeps pickups and berries but drops the boost. */
    g.checkpoint_x = to_fp(40 * TILE_SIZE);
    g.checkpoint_y = to_fp(130);
    g.pickup_taken[0] = 1;
    g.red_collected = 1;
    g.blue_timer = 200;
    g.player.invulnerable = 0;
    g.player.hp = 30;
    game_damage(&g, AnimalType::WOLF, 0);
    assert(g.player.lives == 2 && g.player.hp == FULL_HP);
    assert(g.player.x == g.checkpoint_x);
    assert(g.red_collected == 1 && g.pickup_taken[0] && g.blue_timer == 0);

    g.player.invulnerable = 0;
    g.player.hp = 40;
    g.player.lives = 1;
    game_damage(&g, AnimalType::BEAR, 0);
    assert(g.game_over && g.player.lives == 0);
    assets_free(&p);
}

static void test_ai_freeze(void)
{
    AssetPack p;
    GameState g;
    unsigned i;

    /* The rabbit waits, then launches into a hop. */
    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    g.player.x = g.player.y = 0;
    step(&g, 30);
    assert(g.enemies[0].state == AiState::WAIT && g.enemies[0].timer == 0);
    step(&g, 1);
    assert(g.enemies[0].state == AiState::PATROL && g.enemies[0].vy == -600);
    g.enemies[0].frozen = FREEZE_FRAMES;
    {
        s32 frozen_x = g.enemies[0].x;
        step(&g, 1);
        assert(g.enemies[0].frozen == FREEZE_FRAMES - 1);
        assert(g.enemies[0].x == frozen_x);
    }
    assets_free(&p);

    /* The wolf telegraphs before it charges. */
    load(&p, "FOREST", FOREST_LEVEL);
    game_init(&g, &p);
    stand_on_animal(&g, FOREST_WOLF);
    g.player.invulnerable = 255;
    step(&g, 1);
    assert(g.enemies[FOREST_WOLF].state == AiState::TELEGRAPH);
    assert(g.enemies[FOREST_WOLF].timer == 15);
    step(&g, 15);
    assert(g.enemies[FOREST_WOLF].state == AiState::CHARGE);
    assert(g.enemies[FOREST_WOLF].vx == 800 || g.enemies[FOREST_WOLF].vx == -800);
    assets_free(&p);

    /* The bear reaches its tree unprompted. */
    load(&p, "DEEP", DEEP_LEVEL);
    game_init(&g, &p);
    g.player.x = g.player.y = 0;
    for (i = 0; i < 400 && g.enemies[DEEP_BEAR].state != AiState::CLIMB; ++i) step(&g, 1);
    assert(g.enemies[DEEP_BEAR].state == AiState::CLIMB ||
           g.enemies[DEEP_BEAR].state == AiState::TOP_WAIT);
    assets_free(&p);
}

/* Stomping an animal that is already frozen must restart its freeze, or a player
 * bouncing repeatedly on one animal would eventually be hit by it. */
static void test_repeated_stomp_refresh(void)
{
    AssetPack p;
    GameState g;
    GameInput idle;
    EnemyState *rabbit;
    unsigned i;
    memset(&idle, 0, sizeof(idle));
    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    for (i = 1; i < p.level.animal_count; ++i) g.enemies[i].pacified = 1;
    rabbit = &g.enemies[0];
    rabbit->vx = 0;
    g.player.invulnerable = 255;

    g.player.x = rabbit->x;
    g.player.y = rabbit->y - to_fp(PLAYER_H);
    g.player.vy = 400;
    g.player.on_ground = 0;
    game_step(&g, &idle);
    assert((g.events & Event::BOUNCE) && rabbit->frozen == FREEZE_FRAMES);

    g.player.x = 0;
    step(&g, 10);
    assert(rabbit->frozen == FREEZE_FRAMES - 10);

    g.player.x = rabbit->x;
    g.player.y = rabbit->y - to_fp(PLAYER_H);
    g.player.vy = 400;
    g.player.on_ground = 0;
    game_step(&g, &idle);
    assert(rabbit->frozen == FREEZE_FRAMES);
    assets_free(&p);
}

static void test_complete_bear_cycle(void)
{
    AssetPack p;
    GameState g;
    EnemyState *bear;
    unsigned i, top_frames = 0, wait_frames = 0;
    int seen_climb = 0, seen_top = 0, seen_descend = 0, seen_wait = 0;
    load(&p, "DEEP", DEEP_LEVEL);
    game_init(&g, &p);
    for (i = 0; i < p.level.animal_count; ++i)
        if (i != DEEP_BEAR) g.enemies[i].pacified = 1;
    bear = &g.enemies[DEEP_BEAR];
    g.player.invulnerable = 255;
    for (i = 0; i < 1400; ++i) {
        step(&g, 1);
        if (bear->state == AiState::CLIMB) seen_climb = 1;
        if (bear->state == AiState::TOP_WAIT) {
            seen_top = 1;
            ++top_frames;
        }
        if (bear->state == AiState::DESCEND) seen_descend = 1;
        if (seen_descend && bear->state == AiState::WAIT) {
            seen_wait = 1;
            ++wait_frames;
        }
        if (seen_wait && bear->state == AiState::PATROL) break;
    }
    assert(seen_climb && seen_top && seen_descend && seen_wait);
    assert(bear->state == AiState::PATROL);
    assert(top_frames == 30 && wait_frames == 30);
    assets_free(&p);
}

static void test_dialogue_and_gate(void)
{
    AssetPack p;
    GameState g;
    unsigned guardian;
    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    guardian = animal_index_for_encounter(&p, 0);

    /* A wrong answer costs health and starts a retry cooldown. */
    stand_on_animal(&g, guardian);
    g.player.invulnerable = 0;
    assert(game_try_talk(&g));
    assert(game_answer_dialogue(&g, 0) == -1);
    assert(g.enemies[guardian].retry == 150 && !g.guardian_solved);

    g.enemies[guardian].retry = 0;
    stand_on_animal(&g, guardian);
    g.player.invulnerable = 0;
    assert(game_try_talk(&g));
    assert(game_answer_dialogue(&g, 1) == 1);
    assert(g.guardian_solved && g.enemies[guardian].pacified);

    /* Both the guardian and every required berry gate the exit. */
    assert(!game_exit_ready(&g));
    g.red_collected = 6;
    assert(game_exit_ready(&g));
    assets_free(&p);
}

static void test_optional_rewards(void)
{
    AssetPack p;
    GameState g;
    unsigned animal;

    load(&p, "GARDEN", GARDEN_LEVEL);
    game_init(&g, &p);
    animal = animal_index_for_encounter(&p, 1);
    stand_on_animal(&g, animal);
    g.player.invulnerable = 0;
    assert(game_try_talk(&g));
    assert(g.active_encounter == 1);
    assert(game_answer_dialogue(&g, 0) == 1);
    assert(g.blue_timer == BLUE_FRAMES && g.enemies[animal].pacified);
    assets_free(&p);

    load(&p, "FOREST", FOREST_LEVEL);
    game_init(&g, &p);
    animal = animal_index_for_encounter(&p, 1);
    g.player.hp = 50;
    g.player.lives = 2;
    stand_on_animal(&g, animal);
    assert(game_try_talk(&g));
    assert(g.active_encounter == 1);
    assert(game_answer_dialogue(&g, 2) == 1);
    assert(g.player.hp == FULL_HP && g.player.lives == DEFAULT_LIVES);
    assets_free(&p);
}

/* Health and lives carry between levels; berries and the boost do not. */
static void test_sequential_carry(void)
{
    AssetPack garden, forest, deep;
    GameState g;
    load(&garden, "GARDEN", GARDEN_LEVEL);
    load(&forest, "FOREST", FOREST_LEVEL);
    load(&deep, "DEEP", DEEP_LEVEL);

    game_init(&g, &garden);
    g.player.hp = 63;
    g.player.lives = 2;
    g.blue_timer = 200;
    game_init(&g, &forest, g.player.hp, g.player.lives);
    assert(g.player.hp == 63 && g.player.lives == 2);
    assert(g.blue_timer == 0 && g.red_collected == 0);

    g.player.hp = 41;
    g.player.lives = MAX_LIVES;
    game_init(&g, &deep, g.player.hp, g.player.lives);
    assert(g.player.hp == 41 && g.player.lives == MAX_LIVES);
    assert(g.blue_timer == 0);

    game_init(&g, &deep);
    assert(g.player.hp == FULL_HP && g.player.lives == DEFAULT_LIVES);
    assets_free(&garden);
    assets_free(&forest);
    assets_free(&deep);
}

static void test_codewords(void)
{
    assert(campaign_codeword_stage("REPKA") == 0);
    assert(campaign_codeword_stage("teremok") == 1);
    assert(campaign_codeword_stage("Morozko") == 2);
    assert(campaign_codeword_stage("bogus") == -1);
}

int main(void)
{
    test_assets_and_levels();
    test_crc_rejection();
    test_level_validation();
    test_surface_physics();
    test_boost_and_pies();
    test_damage_lives_checkpoint();
    test_ai_freeze();
    test_repeated_stomp_refresh();
    test_complete_bear_cycle();
    test_dialogue_and_gate();
    test_optional_rewards();
    test_sequential_carry();
    test_codewords();
    puts("host gameplay tests: PASS");
    return 0;
}
