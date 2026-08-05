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

/* Why a load, a save or a validation failed. Callers declare one, hand it to
 * whatever may fail, and print message() if it does. fail() always returns
 * false, so reporting a failure and returning it is a single statement. */
class Error {
public:
    static const unsigned TEXT_SIZE = 96;
    Error() { text[0] = 0; }
    bool fail(const char *message);
    const char *message() const { return text; }
private:
    char text[TEXT_SIZE];
};

/* Stored in the KLV as an absent tree, dialogue or other cross-reference. */
#define NO_ID 0xffff

/* An encounter stores its dialogue ID in a single byte, so an animal that names
 * a larger one cannot round-trip into the encounter that answers for it. */
#define MAX_DIALOGUE_ID 255

/* Every dialogue offers three answers, so a correct answer is 0..2. */
#define ANSWER_COUNT 3

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
    /* The map is width * height bytes, row-major. Spelling that arithmetic out at
     * each of its call sites is how a wrong stride gets in. */
    u8 tile(unsigned tx, unsigned ty) const { return map[ty * width + tx]; }
    u8 &tile(unsigned tx, unsigned ty) { return map[ty * width + tx]; }
} LevelData;

/* Pickups, animals, trees, encounters and live enemies are all looked up by ID
 * rather than by position, and every one of them keeps its ID in a field named
 * id, so one search serves them all. Returns 0 when nothing carries that ID.
 *
 * The result is const because Open Watcom drops the qualifier when deducing from
 * an array inside a const struct, which makes the const and non-const overload
 * pair it would otherwise take ambiguous. The one caller that has to modify what
 * it finds keeps its own search. */
template <class T> const T *find_by_id(const T *items, unsigned count, u16 id)
{
    for (unsigned i = 0; i < count; ++i)
        if (items[i].id == id) return &items[i];
    return 0;
}

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

bool assets_load_bank(AssetPack *pack, const char *archive_path,
                      const char *bank_name, const char *level_path, Error &error);
void assets_free(AssetPack *pack);
bool level_load(LevelData *level, const char *path, Error &error);
bool level_save(const LevelData *level, const char *path, Error &error);
void level_free(LevelData *level);
bool level_validate(const LevelData *level, Error &error);
u32 assets_crc32(ConstFarPtr data, u32 length);
bool assets_far_memory_active(const AssetPack *pack);
unsigned assets_property_field_count(unsigned kind);

#endif
