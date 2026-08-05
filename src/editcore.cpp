#include "editcore.h"

#include <stdlib.h>
#include <string.h>

int make_blank(LevelData *level)
{
    unsigned x;
    memset(level, 0, sizeof(*level));
    level->width = BLANK_WIDTH;
    level->height = LEVEL_HEIGHT;
    level->theme = Theme::GARDEN;
    level->required_red = 1;
    level->cloud_seed = 1;
    level->map = (u8 *)calloc(BLANK_WIDTH * LEVEL_HEIGHT, 1);
    if (!level->map) return 0;
    for (x = 0; x < BLANK_WIDTH; ++x) {
        level->map[BLANK_GROUND_ROW * BLANK_WIDTH + x] = Tile::GRASS_TOP;
        level->map[(BLANK_GROUND_ROW + 1) * BLANK_WIDTH + x] = Tile::GRASS_BODY;
    }
    level->start.x = 2;
    level->start.y = 8;
    level->exit.x = BLANK_WIDTH - 3;
    level->exit.y = 8;
    level->home = level->start;

    level->pickup_count = 1;
    level->pickups[0].id = 1;
    level->pickups[0].type = PickupType::RED;
    level->pickups[0].x = 40;
    level->pickups[0].y = 8;

    level->animal_count = 1;
    level->animals[0].id = 2;
    level->animals[0].type = AnimalType::RABBIT;
    level->animals[0].x = 70;
    level->animals[0].y = 8;
    level->animals[0].min_x = 65;
    level->animals[0].max_x = 75;
    level->animals[0].tree_id = NO_ID;
    level->animals[0].dialogue_id = 1;
    level->animals[0].flags = 1;

    level->encounter_count = 1;
    level->encounters[0].id = 1;
    level->encounters[0].animal_id = 2;
    level->encounters[0].dialogue_id = 1;
    level->encounters[0].required = 1;
    level->encounters[0].correct = 1;
    level->encounters[0].retry_frames = DEFAULT_RETRY_FRAMES;
    return 1;
}

/* Levels are saved next to the DOS executable, so the name has to survive an 8.3
 * filesystem: at most eight stem characters, one dot and three of extension. */
int valid_83(const char *name)
{
    const char *base = name, *p;
    unsigned stem = 0, ext = 0;
    int seen_dot = 0;
    for (p = name; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base) return 0;
    for (p = base; *p; ++p) {
        if (*p == '.') {
            if (seen_dot) return 0;
            seen_dot = 1;
        } else if (*p == ' ' || *p == '/' || *p == '\\') {
            return 0;
        } else if (seen_dot) {
            ++ext;
        } else {
            ++stem;
        }
    }
    return stem > 0 && stem <= 8 && ext <= 3;
}

static int id_in_use(const LevelData *level, u16 id)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].id == id) return 1;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == id) return 1;
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == id) return 1;
    return 0;
}

/* Pickups, animals and trees draw from one ID space. level_validate only rejects
 * duplicates within a kind, since animals resolve tree_id against trees alone and
 * encounters resolve animal_id against animals alone, but keeping the space
 * shared means an ID printed in the property panel names exactly one object. */
static u16 next_object_id(const LevelData *level)
{
    u16 id = 1;
    while (id_in_use(level, id)) ++id;
    return id;
}

static int encounter_id_in_use(const LevelData *level, u16 id)
{
    unsigned i;
    for (i = 0; i < level->encounter_count; ++i)
        if (level->encounters[i].id == id) return 1;
    return 0;
}

static u16 next_encounter_id(const LevelData *level)
{
    u16 id = 1;
    while (encounter_id_in_use(level, id)) ++id;
    return id;
}

Encounter *encounter_for(LevelData *level, u16 animal_id, int create)
{
    const AnimalSpawn *animal = 0;
    Encounter *encounter;
    unsigned i;
    for (i = 0; i < level->encounter_count; ++i)
        if (level->encounters[i].animal_id == animal_id) return &level->encounters[i];
    if (!create || level->encounter_count >= MAX_ENCOUNTERS) return 0;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == animal_id) animal = &level->animals[i];
    encounter = &level->encounters[level->encounter_count++];
    memset(encounter, 0, sizeof(*encounter));
    encounter->id = next_encounter_id(level);
    encounter->animal_id = animal_id;
    encounter->dialogue_id = (u8)(animal && animal->dialogue_id != NO_ID
                                  ? animal->dialogue_id : 1);
    encounter->retry_frames = DEFAULT_RETRY_FRAMES;
    return encounter;
}

int find_object(const LevelData *level, unsigned x, unsigned y,
                       unsigned *kind, unsigned *index)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].x == x && level->pickups[i].y == y) {
            *kind = PropertyKind::PICKUP;
            *index = i;
            return 1;
        }
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].x == x && level->animals[i].y == y) {
            *kind = PropertyKind::ANIMAL;
            *index = i;
            return 1;
        }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].x == x && level->trees[i].y == y) {
            *kind = PropertyKind::TREE;
            *index = i;
            return 1;
        }
    return 0;
}

static u8 wrap_u8(u8 value, int delta, unsigned count)
{
    int next = (int)value + delta;
    while (next < 0) next += (int)count;
    while (next >= (int)count) next -= (int)count;
    return (u8)next;
}

static int clamp_int(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/* Cycles through the level's trees and back to "no tree" at either end. */
static void adjust_tree_association(LevelData *level, AnimalSpawn *animal, int delta)
{
    int position = -1, next;
    unsigned i;
    if (!level->tree_count) {
        animal->tree_id = NO_ID;
        return;
    }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == animal->tree_id) position = (int)i;
    next = position + delta;
    if (next < -1) next = (int)level->tree_count - 1;
    if (next >= (int)level->tree_count) next = -1;
    animal->tree_id = next < 0 ? NO_ID : level->trees[next].id;
}

static int adjust_level_property(LevelData *level, unsigned field, int delta)
{
    if (field == LevelField::THEME) {
        level->theme = wrap_u8(level->theme, delta, Theme::COUNT);
    } else if (field == LevelField::REQUIRED_RED) {
        level->required_red = (u8)clamp_int((int)level->required_red + delta,
                                            0, MAX_PICKUPS);
    } else {
        long seed = (long)level->cloud_seed + delta;
        if (seed < 1) seed = MAX_CLOUD_SEED;
        if (seed > MAX_CLOUD_SEED) seed = 1;
        level->cloud_seed = (u32)seed;
    }
    return 1;
}

static int adjust_pickup_property(Pickup *pickup, unsigned field, int delta)
{
    if (field == PickupField::SUBTYPE)
        pickup->type = wrap_u8(pickup->type, delta, PickupType::COUNT);
    else
        pickup->flags = (u8)(pickup->flags + delta);
    return 1;
}

static int adjust_tree_property(Tree *tree, unsigned field, int delta)
{
    if (field == TreeField::TYPE)
        tree->type = wrap_u8(tree->type, delta, TreeType::COUNT);
    else if (field == TreeField::FLAGS)
        tree->flags = (u8)(tree->flags + delta);
    else
        tree->height = (u8)clamp_int((int)tree->height + delta, 1, MAX_TREE_HEIGHT);
    return 1;
}

/* Editing an encounter field creates the encounter on demand and marks the animal
 * as talkable, so an animal can be turned into a guardian without a second step. */
static int adjust_animal_property(LevelData *level, unsigned index,
                                  unsigned field, int delta)
{
    AnimalSpawn *animal = &level->animals[index];
    Encounter *encounter;
    int value;
    switch (field) {
    case AnimalField::SUBTYPE:
        animal->type = wrap_u8(animal->type, delta, AnimalType::COUNT);
        break;
    case AnimalField::FLAGS:
        animal->flags = (u8)(animal->flags + delta);
        break;
    case AnimalField::DIALOGUE:
        value = animal->dialogue_id == NO_ID ? 1 : (int)animal->dialogue_id + delta;
        if (value < 1) value = MAX_DIALOGUE_ID;
        if (value > MAX_DIALOGUE_ID) value = 1;
        animal->dialogue_id = (u16)value;
        encounter = encounter_for(level, animal->id, 1);
        if (encounter) encounter->dialogue_id = (u8)value;
        break;
    case AnimalField::REWARD:
        encounter = encounter_for(level, animal->id, 1);
        if (!encounter) return 0;
        encounter->reward = wrap_u8(encounter->reward, delta, Reward::COUNT);
        animal->flags |= 1;
        break;
    case AnimalField::ANSWER:
        encounter = encounter_for(level, animal->id, 1);
        if (!encounter) return 0;
        encounter->correct = wrap_u8(encounter->correct, delta, 3);
        animal->flags |= 1;
        break;
    case AnimalField::PATROL_LEFT:
        animal->min_x = (u16)clamp_int((int)animal->min_x + delta, 0, (int)animal->x);
        break;
    case AnimalField::PATROL_RIGHT:
        animal->max_x = (u16)clamp_int((int)animal->max_x + delta, (int)animal->x,
                                       (int)level->width - 1);
        break;
    case AnimalField::TREE:
        adjust_tree_association(level, animal, delta);
        break;
    case AnimalField::CLIMB_TOP:
        animal->climb_min = (u16)clamp_int((int)animal->climb_min + delta, 0,
                                           (int)animal->climb_max);
        break;
    default:
        animal->climb_max = (u16)clamp_int((int)animal->climb_max + delta,
                                           (int)animal->climb_min,
                                           (int)level->height - 1);
        break;
    }
    return 1;
}

int adjust_property(LevelData *level, unsigned kind, unsigned index,
                          unsigned field, int delta)
{
    if (kind == PropertyKind::LEVEL) return adjust_level_property(level, field, delta);
    if (kind == PropertyKind::PICKUP && index < level->pickup_count)
        return adjust_pickup_property(&level->pickups[index], field, delta);
    if (kind == PropertyKind::TREE && index < level->tree_count)
        return adjust_tree_property(&level->trees[index], field, delta);
    if (kind == PropertyKind::ANIMAL && index < level->animal_count)
        return adjust_animal_property(level, index, field, delta);
    return 0;
}

static int place_pickup(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    Pickup *pickup;
    if (level->pickup_count >= MAX_PICKUPS) return 0;
    pickup = &level->pickups[level->pickup_count++];
    memset(pickup, 0, sizeof(*pickup));
    pickup->id = id;
    pickup->type = (u8)tool;
    pickup->x = (u16)x;
    pickup->y = (u16)y;
    return 1;
}

static int place_animal(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    AnimalSpawn *animal;
    if (level->animal_count >= MAX_ENEMIES) return 0;
    animal = &level->animals[level->animal_count++];
    memset(animal, 0, sizeof(*animal));
    animal->id = id;
    animal->type = (u8)(tool - TOOL_ANIMAL_FIRST);
    animal->x = (u16)x;
    animal->y = (u16)y;
    animal->min_x = (u16)(x > PATROL_HALF_WIDTH ? x - PATROL_HALF_WIDTH : 0);
    animal->max_x = (u16)(x + PATROL_HALF_WIDTH < level->width
                          ? x + PATROL_HALF_WIDTH : level->width - 1);
    animal->tree_id = animal->dialogue_id = NO_ID;
    animal->climb_min = animal->climb_max = (u16)y;
    return 1;
}

static int place_tree(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    Tree *tree;
    if (level->tree_count >= MAX_TREES) return 0;
    tree = &level->trees[level->tree_count++];
    memset(tree, 0, sizeof(*tree));
    tree->id = id;
    tree->type = (u8)(tool - TOOL_TREE_FIRST);
    tree->x = (u16)x;
    tree->y = (u16)y;
    tree->height = DEFAULT_TREE_HEIGHT;
    return 1;
}

int place_object(LevelData *level, unsigned x, unsigned y, unsigned tool)
{
    u16 id = next_object_id(level);
    if (tool < TOOL_ANIMAL_FIRST) return place_pickup(level, x, y, tool, id);
    if (tool < TOOL_TREE_FIRST) return place_animal(level, x, y, tool, id);
    if (tool < TOOL_COUNT) return place_tree(level, x, y, tool, id);
    return 0;
}

static void remove_encounters_for(LevelData *level, u16 animal_id)
{
    unsigned i = 0;
    while (i < level->encounter_count) {
        if (level->encounters[i].animal_id != animal_id) {
            ++i;
            continue;
        }
        memmove(&level->encounters[i], &level->encounters[i + 1],
                (level->encounter_count - i - 1) * sizeof(Encounter));
        --level->encounter_count;
    }
}

int erase_object(LevelData *level, unsigned x, unsigned y)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].x == x && level->pickups[i].y == y) {
            memmove(&level->pickups[i], &level->pickups[i + 1],
                    (level->pickup_count - i - 1) * sizeof(Pickup));
            --level->pickup_count;
            return 1;
        }
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].x == x && level->animals[i].y == y) {
            u16 id = level->animals[i].id;
            memmove(&level->animals[i], &level->animals[i + 1],
                    (level->animal_count - i - 1) * sizeof(AnimalSpawn));
            --level->animal_count;
            remove_encounters_for(level, id);
            return 1;
        }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].x == x && level->trees[i].y == y) {
            u16 id = level->trees[i].id;
            unsigned j;
            memmove(&level->trees[i], &level->trees[i + 1],
                    (level->tree_count - i - 1) * sizeof(Tree));
            --level->tree_count;
            for (j = 0; j < level->animal_count; ++j)
                if (level->animals[j].tree_id == id) level->animals[j].tree_id = NO_ID;
            return 1;
        }
    return 0;
}
