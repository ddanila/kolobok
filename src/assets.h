#ifndef KOLOBOK_ASSETS_H
#define KOLOBOK_ASSETS_H

#include <stdio.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

#define KOLO_TILE_SIZE 16
#define KOLO_LEVEL_HEIGHT 11
#define KOLO_MAX_PICKUPS 32
#define KOLO_MAX_ENEMIES 16
#define KOLO_MAX_TREES 32
#define KOLO_MAX_CHECKPOINTS 8
#define KOLO_MAX_ENCOUNTERS 8
#define KOLO_MAX_SPRITES 15

enum KoloTheme { KOLO_THEME_GARDEN, KOLO_THEME_FOREST, KOLO_THEME_DEEP };
enum KoloPickupType { KOLO_PICKUP_RED, KOLO_PICKUP_BLUE, KOLO_PICKUP_SMALL_PIE, KOLO_PICKUP_BIG_PIE };
enum KoloAnimalType { KOLO_ANIMAL_RABBIT, KOLO_ANIMAL_FOX, KOLO_ANIMAL_WOLF, KOLO_ANIMAL_BEAR };
enum KoloTreeType { KOLO_TREE_FIR, KOLO_TREE_BIRCH, KOLO_TREE_OAK };
enum KoloRewardType { KOLO_REWARD_NONE, KOLO_REWARD_BLUE, KOLO_REWARD_SMALL_PIE };

typedef struct KoloPoint { u16 x, y; } KoloPoint;

typedef struct KoloPickup {
    u8 type, flags;
    u16 id, x, y;
} KoloPickup;

typedef struct KoloAnimalSpawn {
    u8 type, flags;
    u16 id, x, y, min_x, max_x, tree_id, climb_min, climb_max, dialogue_id;
} KoloAnimalSpawn;

typedef struct KoloTree {
    u8 type, flags;
    u16 id, x, y;
    u8 height;
} KoloTree;

typedef struct KoloEncounter {
    u16 id, animal_id;
    u8 dialogue_id, required, correct, reward;
    u16 retry_frames;
} KoloEncounter;

typedef struct LevelData {
    u16 width, height;
    u8 theme, required_red;
    u32 cloud_seed;
    u8 *map;
    KoloPoint start, exit, home;
    u16 checkpoint_count, pickup_count, animal_count, tree_count, encounter_count;
    KoloPoint checkpoints[KOLO_MAX_CHECKPOINTS];
    KoloPickup pickups[KOLO_MAX_PICKUPS];
    KoloAnimalSpawn animals[KOLO_MAX_ENEMIES];
    KoloTree trees[KOLO_MAX_TREES];
    KoloEncounter encounters[KOLO_MAX_ENCOUNTERS];
} LevelData;

typedef struct AssetPack {
    u8 *blob;
    u32 blob_size;
    u16 theme;
    u16 tile_count;
    u16 sprite_count;
    u8 *palette;
    u8 *tiles;
    u8 *tile_flags;
    u8 *tile_material;
    u8 *sprite_spans[KOLO_MAX_SPRITES];
    u8 *sprite_planar_spans[KOLO_MAX_SPRITES][16];
    LevelData level;
    /* Compatibility aliases used by the renderer and small host tools. */
    u16 map_w, map_h, berry_count, enemy_count;
    u8 *map;
} AssetPack;

int assets_load_bank(AssetPack *pack, const char *archive_path,
                     const char *bank_name, const char *level_path,
                     char *error, unsigned error_size);
int assets_load(AssetPack *pack, const char *path, char *error, unsigned error_size);
void assets_free(AssetPack *pack);
int level_load(LevelData *level, const char *path, char *error, unsigned error_size);
int level_save(const LevelData *level, const char *path, char *error, unsigned error_size);
void level_free(LevelData *level);
int level_validate(const LevelData *level, char *error, unsigned error_size);
u32 assets_crc32(const u8 *data, u32 length);

#endif
