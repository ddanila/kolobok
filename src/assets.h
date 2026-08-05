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
typedef u8 __far *FarPtr;
typedef const u8 __far *ConstFarPtr;
#else
typedef u8 *FarPtr;
typedef const u8 *ConstFarPtr;
#endif

#define TILE_SIZE 16
#define LEVEL_HEIGHT 11
#define MAX_PICKUPS 32
#define MAX_ENEMIES 16
#define MAX_TREES 32
#define MAX_CHECKPOINTS 8
#define MAX_ENCOUNTERS 8
#define MAX_SPRITES 15

static_assert(TILE_SIZE == 16, "tools/assets.py TILE");
static_assert(MAX_SPRITES == 15, "tools/assets.py SPRITE_COUNT");
static_assert(LEVEL_HEIGHT == 11, "tools/levels.py HEIGHT");

struct Theme { enum Enum { GARDEN, FOREST, DEEP, COUNT }; };

static_assert(Theme::GARDEN == 0 && Theme::FOREST == 1 && Theme::DEEP == 2,
              "tools/levels.py THEMES");
struct PickupType { enum Enum { RED, BLUE, SMALL_PIE, BIG_PIE, COUNT }; };
struct AnimalType { enum Enum { RABBIT, FOX, WOLF, BEAR, COUNT }; };
struct TreeType { enum Enum { FIR, BIRCH, OAK, COUNT }; };
struct Reward { enum Enum { NONE, BLUE, SMALL_PIE, COUNT }; };

/* Tile indices, collision flags and surface materials are a contract with
 * tools/assets.py, which paints the tile sheet in this order and emits the two
 * per-tile tables, and with tools/levels.py, which writes these indices into
 * the tile map. Reordering either side used to swap tiles in game silently; the
 * assertions below now fail the build instead. */
struct Tile {
    enum Enum {
        AIR,
        GRASS_TOP, GRASS_BODY, GRASS_PLATFORM,
        SPIKES,
        SAND_TOP, SAND_BODY, SAND_PLATFORM,
        ICE_TOP, ICE_BODY, ICE_PLATFORM,
        COUNT
    };
};

static_assert(Tile::COUNT == 11, "tools/assets.py TILE_COUNT");
static_assert(Tile::GRASS_TOP == 1 && Tile::SPIKES == 4 &&
              Tile::SAND_TOP == 5 && Tile::ICE_TOP == 8,
              "tools/assets.py draw_tiles paints the sheet in this order");

struct TileFlag { enum Enum { SOLID = 1, HAZARD = 2 }; };

struct Surface { enum Enum { GRASS, SAND, ICE, AIR }; };

/* Object kinds the editor can inspect; the renderer draws a panel per kind. The
 * field enums below are shared so that the row the editor adjusts and the row the
 * renderer labels can never drift apart. */
struct PropertyKind { enum Enum { PICKUP, ANIMAL, TREE, LEVEL }; };

struct PickupField { enum Enum { SUBTYPE, FLAGS, COUNT }; };

struct TreeField { enum Enum { TYPE, FLAGS, HEIGHT, COUNT }; };

struct LevelField { enum Enum { THEME, REQUIRED_RED, CLOUD_SEED, COUNT }; };

struct AnimalField {
    enum Enum {
        SUBTYPE, FLAGS, DIALOGUE, REWARD, ANSWER,
        PATROL_LEFT, PATROL_RIGHT,
        TREE, CLIMB_TOP, CLIMB_BASE,
        COUNT
    };
};

/* Stored in the KLV as an absent tree, dialogue or other cross-reference. */
#define NO_ID 0xffff

typedef struct Point { u16 x, y; } Point;

typedef struct Pickup {
    u8 type, flags;
    u16 id, x, y;
} Pickup;

typedef struct AnimalSpawn {
    u8 type, flags;
    u16 id, x, y, min_x, max_x, tree_id, climb_min, climb_max, dialogue_id;
} AnimalSpawn;

typedef struct Tree {
    u8 type, flags;
    u16 id, x, y;
    u8 height;
} Tree;

typedef struct Encounter {
    u16 id, animal_id;
    u8 dialogue_id, required, correct, reward;
    u16 retry_frames;
} Encounter;

typedef struct LevelData {
    u16 width, height;
    u8 theme, required_red;
    u32 cloud_seed;
    u8 *map;
    Point start, exit, home;
    u16 checkpoint_count, pickup_count, animal_count, tree_count, encounter_count;
    Point checkpoints[MAX_CHECKPOINTS];
    Pickup pickups[MAX_PICKUPS];
    AnimalSpawn animals[MAX_ENEMIES];
    Tree trees[MAX_TREES];
    Encounter encounters[MAX_ENCOUNTERS];
} LevelData;

typedef struct AssetPack {
    FarPtr blob;
    u32 blob_size;
    u16 bank_segment;
    u16 theme;
    u16 tile_count;
    u16 sprite_count;
    FarPtr palette;
    FarPtr tiles;
    FarPtr tile_flags;
    FarPtr tile_material;
    FarPtr sprite_spans[MAX_SPRITES];
    FarPtr sprite_planar_spans[MAX_SPRITES][16];
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
u32 assets_crc32(ConstFarPtr data, u32 length);
int assets_far_memory_active(const AssetPack *pack);
unsigned assets_property_field_count(unsigned kind);

#endif
