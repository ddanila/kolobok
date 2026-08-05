#include "editcore.h"

#include <stdlib.h>
#include <string.h>

bool make_blank(LevelData *level)
{
    unsigned x;
    memset(level, 0, sizeof(*level));
    level->width = BLANK_WIDTH;
    level->height = LEVEL_HEIGHT;
    level->theme = Theme::GARDEN;
    level->required_red = 1;
    level->cloud_seed = 1;
    level->map = (u8 *)calloc(BLANK_WIDTH * LEVEL_HEIGHT, 1);
    if (!level->map) return false;
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
    return true;
}

/* Levels are saved next to the DOS executable, so the name has to survive an 8.3
 * filesystem: at most eight stem characters, one dot and three of extension. */
bool valid_83(const char *name)
{
    const char *base = name, *p;
    unsigned stem = 0, ext = 0;
    bool seen_dot = false;
    for (p = name; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base) return false;
    for (p = base; *p; ++p) {
        if (*p == '.') {
            if (seen_dot) return false;
            seen_dot = true;
        } else if (*p == ' ' || *p == '/' || *p == '\\') {
            return false;
        } else if (seen_dot) {
            ++ext;
        } else {
            ++stem;
        }
    }
    return stem > 0 && stem <= 8 && ext <= 3;
}

static bool id_in_use(const LevelData *level, u16 id)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].id == id) return true;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == id) return true;
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == id) return true;
    return false;
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

static bool encounter_id_in_use(const LevelData *level, u16 id)
{
    unsigned i;
    for (i = 0; i < level->encounter_count; ++i)
        if (level->encounters[i].id == id) return true;
    return false;
}

static u16 next_encounter_id(const LevelData *level)
{
    u16 id = 1;
    while (encounter_id_in_use(level, id)) ++id;
    return id;
}

Encounter *encounter_for(LevelData *level, u16 animal_id, bool create)
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

bool find_object(const LevelData *level, unsigned x, unsigned y,
                 unsigned *kind, unsigned *index)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].x == x && level->pickups[i].y == y) {
            *kind = PropertyKind::PICKUP;
            *index = i;
            return true;
        }
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].x == x && level->animals[i].y == y) {
            *kind = PropertyKind::ANIMAL;
            *index = i;
            return true;
        }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].x == x && level->trees[i].y == y) {
            *kind = PropertyKind::TREE;
            *index = i;
            return true;
        }
    return false;
}

/* The property panel only ever nudges one field by one step, so each kind of
 * field gets a mutator that takes the field itself. Keeping the arithmetic and
 * the narrowing in here is what leaves the adjust_ functions below reading as a
 * list of rules rather than a list of casts. */
static void cycle(u8 &value, int delta, unsigned count)
{
    int next = (int)value + delta;
    while (next < 0) next += (int)count;
    while (next >= (int)count) next -= (int)count;
    value = (u8)next;
}

static void bump(u8 &value, int delta) { value = (u8)((int)value + delta); }

static int clamped(int value, int low, int high)
{
    if (value < low) return low;
    return value > high ? high : value;
}

static void clamp(u8 &value, int delta, int low, int high)
{
    value = (u8)clamped((int)value + delta, low, high);
}

static void clamp(u16 &value, int delta, int low, int high)
{
    value = (u16)clamped((int)value + delta, low, high);
}

/* Wraps round the ends of an inclusive range, for the fields that start at 1
 * because 0 already means "unset". */
static long wrapped(long value, long low, long high)
{
    if (value < low) return high;
    return value > high ? low : value;
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

static bool adjust_level_property(LevelData *level, unsigned field, int delta)
{
    if (field == LevelField::THEME)
        cycle(level->theme, delta, Theme::COUNT);
    else if (field == LevelField::REQUIRED_RED)
        clamp(level->required_red, delta, 0, MAX_PICKUPS);
    else
        level->cloud_seed = (u32)wrapped((long)level->cloud_seed + delta,
                                         1, MAX_CLOUD_SEED);
    return true;
}

static bool adjust_pickup_property(Pickup *pickup, unsigned field, int delta)
{
    if (field == PickupField::SUBTYPE) cycle(pickup->type, delta, PickupType::COUNT);
    else bump(pickup->flags, delta);
    return true;
}

static bool adjust_tree_property(Tree *tree, unsigned field, int delta)
{
    if (field == TreeField::TYPE) cycle(tree->type, delta, TreeType::COUNT);
    else if (field == TreeField::FLAGS) bump(tree->flags, delta);
    else clamp(tree->height, delta, 1, MAX_TREE_HEIGHT);
    return true;
}

/* Editing an encounter field creates the encounter on demand and marks the animal
 * as talkable, so an animal can be turned into a guardian without a second step. */
static bool adjust_animal_property(LevelData *level, unsigned index,
                                   unsigned field, int delta)
{
    AnimalSpawn *animal = &level->animals[index];
    Encounter *encounter;
    switch (field) {
    case AnimalField::SUBTYPE:
        cycle(animal->type, delta, AnimalType::COUNT);
        break;
    case AnimalField::FLAGS:
        bump(animal->flags, delta);
        break;
    case AnimalField::DIALOGUE:
        animal->dialogue_id = (u16)wrapped(animal->dialogue_id == NO_ID
                                               ? 1 : (long)animal->dialogue_id + delta,
                                           1, MAX_DIALOGUE_ID);
        encounter = encounter_for(level, animal->id, true);
        if (encounter) encounter->dialogue_id = (u8)animal->dialogue_id;
        break;
    case AnimalField::REWARD:
        encounter = encounter_for(level, animal->id, true);
        if (!encounter) return false;
        cycle(encounter->reward, delta, Reward::COUNT);
        animal->flags |= 1;
        break;
    case AnimalField::ANSWER:
        encounter = encounter_for(level, animal->id, true);
        if (!encounter) return false;
        cycle(encounter->correct, delta, ANSWER_COUNT);
        animal->flags |= 1;
        break;
    case AnimalField::PATROL_LEFT:
        clamp(animal->min_x, delta, 0, animal->x);
        break;
    case AnimalField::PATROL_RIGHT:
        clamp(animal->max_x, delta, animal->x, level->width - 1);
        break;
    case AnimalField::TREE:
        adjust_tree_association(level, animal, delta);
        break;
    case AnimalField::CLIMB_TOP:
        clamp(animal->climb_min, delta, 0, animal->climb_max);
        break;
    default:
        clamp(animal->climb_max, delta, animal->climb_min, level->height - 1);
        break;
    }
    return true;
}

bool adjust_property(LevelData *level, unsigned kind, unsigned index,
                     unsigned field, int delta)
{
    if (kind == PropertyKind::LEVEL) return adjust_level_property(level, field, delta);
    if (kind == PropertyKind::PICKUP && index < level->pickup_count)
        return adjust_pickup_property(&level->pickups[index], field, delta);
    if (kind == PropertyKind::TREE && index < level->tree_count)
        return adjust_tree_property(&level->trees[index], field, delta);
    if (kind == PropertyKind::ANIMAL && index < level->animal_count)
        return adjust_animal_property(level, index, field, delta);
    return false;
}

static bool place_pickup(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    Pickup *pickup;
    if (level->pickup_count >= MAX_PICKUPS) return false;
    pickup = &level->pickups[level->pickup_count++];
    memset(pickup, 0, sizeof(*pickup));
    pickup->id = id;
    pickup->type = (u8)tool;
    pickup->x = (u16)x;
    pickup->y = (u16)y;
    return true;
}

static bool place_animal(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    AnimalSpawn *animal;
    if (level->animal_count >= MAX_ENEMIES) return false;
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
    return true;
}

static bool place_tree(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    Tree *tree;
    if (level->tree_count >= MAX_TREES) return false;
    tree = &level->trees[level->tree_count++];
    memset(tree, 0, sizeof(*tree));
    tree->id = id;
    tree->type = (u8)(tool - TOOL_TREE_FIRST);
    tree->x = (u16)x;
    tree->y = (u16)y;
    tree->height = DEFAULT_TREE_HEIGHT;
    return true;
}

bool place_object(LevelData *level, unsigned x, unsigned y, unsigned tool)
{
    u16 id = next_object_id(level);
    if (tool < TOOL_ANIMAL_FIRST) return place_pickup(level, x, y, tool, id);
    if (tool < TOOL_TREE_FIRST) return place_animal(level, x, y, tool, id);
    if (tool < TOOL_COUNT) return place_tree(level, x, y, tool, id);
    return false;
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

bool erase_object(LevelData *level, unsigned x, unsigned y)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].x == x && level->pickups[i].y == y) {
            memmove(&level->pickups[i], &level->pickups[i + 1],
                    (level->pickup_count - i - 1) * sizeof(Pickup));
            --level->pickup_count;
            return true;
        }
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].x == x && level->animals[i].y == y) {
            u16 id = level->animals[i].id;
            memmove(&level->animals[i], &level->animals[i + 1],
                    (level->animal_count - i - 1) * sizeof(AnimalSpawn));
            --level->animal_count;
            remove_encounters_for(level, id);
            return true;
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
            return true;
        }
    return false;
}
