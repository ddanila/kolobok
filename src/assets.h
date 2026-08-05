#ifndef KOLOBOK_ASSETS_H
#define KOLOBOK_ASSETS_H

#include <stdio.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

#ifdef __WATCOMC__
typedef u8 __far *KoloFarPtr;
typedef const u8 __far *KoloConstFarPtr;
#else
typedef u8 *KoloFarPtr;
typedef const u8 *KoloConstFarPtr;
#endif

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

/* Tile indices, collision flags and surface materials are a contract with
 * tools/assets.py, which paints the tile sheet in this order and emits the two
 * per-tile tables, and with tools/levels.py, which writes these indices into
 * the tile map. Reordering either side silently swaps tiles in game. */
enum KoloTile {
    KOLO_TILE_AIR,
    KOLO_TILE_GRASS_TOP, KOLO_TILE_GRASS_BODY, KOLO_TILE_GRASS_PLATFORM,
    KOLO_TILE_SPIKES,
    KOLO_TILE_SAND_TOP, KOLO_TILE_SAND_BODY, KOLO_TILE_SAND_PLATFORM,
    KOLO_TILE_ICE_TOP, KOLO_TILE_ICE_BODY, KOLO_TILE_ICE_PLATFORM,
    KOLO_TILE_COUNT
};

enum KoloTileFlag { KOLO_TILE_SOLID = 1, KOLO_TILE_HAZARD = 2 };

enum KoloSurface {
    KOLO_SURFACE_GRASS, KOLO_SURFACE_SAND, KOLO_SURFACE_ICE, KOLO_SURFACE_AIR
};

/* Object kinds the editor can inspect; the renderer draws a panel per kind. The
 * field enums below are shared so that the row the editor adjusts and the row the
 * renderer labels can never drift apart. */
enum KoloPropertyKind {
    KOLO_PROP_PICKUP, KOLO_PROP_ANIMAL, KOLO_PROP_TREE, KOLO_PROP_LEVEL
};

enum KoloPickupField {
    KOLO_PICKUP_FIELD_SUBTYPE, KOLO_PICKUP_FIELD_FLAGS, KOLO_PICKUP_FIELD_COUNT
};

enum KoloTreeField {
    KOLO_TREE_FIELD_TYPE, KOLO_TREE_FIELD_FLAGS, KOLO_TREE_FIELD_HEIGHT,
    KOLO_TREE_FIELD_COUNT
};

enum KoloLevelField {
    KOLO_LEVEL_FIELD_THEME, KOLO_LEVEL_FIELD_REQUIRED_RED,
    KOLO_LEVEL_FIELD_CLOUD_SEED, KOLO_LEVEL_FIELD_COUNT
};

enum KoloAnimalField {
    KOLO_ANIMAL_FIELD_SUBTYPE, KOLO_ANIMAL_FIELD_FLAGS, KOLO_ANIMAL_FIELD_DIALOGUE,
    KOLO_ANIMAL_FIELD_REWARD, KOLO_ANIMAL_FIELD_ANSWER,
    KOLO_ANIMAL_FIELD_PATROL_LEFT, KOLO_ANIMAL_FIELD_PATROL_RIGHT,
    KOLO_ANIMAL_FIELD_TREE, KOLO_ANIMAL_FIELD_CLIMB_TOP,
    KOLO_ANIMAL_FIELD_CLIMB_BASE, KOLO_ANIMAL_FIELD_COUNT
};

/* Stored in the KLV as an absent tree, dialogue or other cross-reference. */
#define KOLO_NO_ID 0xffff

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
    KoloFarPtr blob;
    u32 blob_size;
    u16 bank_segment;
    u16 theme;
    u16 tile_count;
    u16 sprite_count;
    KoloFarPtr palette;
    KoloFarPtr tiles;
    KoloFarPtr tile_flags;
    KoloFarPtr tile_material;
    KoloFarPtr sprite_spans[KOLO_MAX_SPRITES];
    KoloFarPtr sprite_planar_spans[KOLO_MAX_SPRITES][16];
    LevelData level;
} AssetPack;

int assets_load_bank(AssetPack *pack, const char *archive_path,
                     const char *bank_name, const char *level_path,
                     char *error, unsigned error_size);
void assets_free(AssetPack *pack);
int level_load(LevelData *level, const char *path, char *error, unsigned error_size);
int level_save(const LevelData *level, const char *path, char *error, unsigned error_size);
void level_free(LevelData *level);
int level_validate(const LevelData *level, char *error, unsigned error_size);
u32 assets_crc32(KoloConstFarPtr data, u32 length);
int assets_far_memory_active(const AssetPack *pack);
unsigned kolo_property_field_count(unsigned kind);

#endif
