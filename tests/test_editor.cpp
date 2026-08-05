/* Host coverage for the editor's portable core.
 *
 * These paths used to be reachable only through KOLOEDIT.EXE -selftest running
 * inside DOSBox-X under a 45-second cap, which put the editing rules outside the
 * fast loop and made them expensive to extend. src/editcore.cpp has no Mode X or
 * keyboard dependency, so all of it can be exercised here instead; the DOS
 * self-test keeps what only a real DOS filesystem can prove. */

#include "assets.h"
#include "editcore.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ERROR_SIZE 96

#define TOOL_RED_BERRY 0
#define TOOL_BIG_PIE 3
#define TOOL_RABBIT TOOL_ANIMAL_FIRST
#define TOOL_BEAR (TOOL_ANIMAL_FIRST + 3)
#define TOOL_FIR TOOL_TREE_FIRST
#define TOOL_OAK (TOOL_TREE_FIRST + 2)

/* Somewhere on the blank level with nothing already placed on it. */
#define FREE_X 12
#define FREE_Y 6

static int valid(const LevelData *level)
{
    char error[ERROR_SIZE];
    return level_validate(level, error, sizeof(error));
}

static const AnimalSpawn *animal_by_id(const LevelData *level, u16 id)
{
    unsigned i;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == id) return &level->animals[i];
    return 0;
}

/* A new file must open onto something the runtime would accept, or the editor
 * greets the author with an invalid level it did not write. */
static void test_blank_is_valid(void)
{
    LevelData level;
    unsigned x;
    assert(make_blank(&level));
    assert(valid(&level));
    assert(level.width == BLANK_WIDTH && level.height == LEVEL_HEIGHT);
    for (x = 0; x < BLANK_WIDTH; ++x)
        assert(level.map[BLANK_GROUND_ROW * BLANK_WIDTH + x] == Tile::GRASS_TOP);
    level_free(&level);
}

/* Levels sit next to the DOS executable, so the name has to survive 8.3. */
static void test_valid_83(void)
{
    assert(valid_83("LEVEL.KLV"));
    assert(valid_83("ABCDEFGH.KLV"));
    assert(valid_83("A"));
    assert(valid_83("C:\\GAMES\\LEVEL.KLV"));
    assert(valid_83("sub/LEVEL.KLV"));

    assert(!valid_83(""));
    assert(!valid_83("TOOLONGNAME.KLV"));
    assert(!valid_83("LEVEL.KLVX"));
    assert(!valid_83("A.B.C"));
    assert(!valid_83("MY LEVEL.KLV"));
    assert(!valid_83("C:\\GAMES\\"));
}

/* One ID space across the three object kinds, with freed IDs reused. */
static void test_placement_and_ids(void)
{
    LevelData level;
    unsigned kind, index;
    assert(make_blank(&level));
    assert(level.pickups[0].id == 1 && level.animals[0].id == 2);

    assert(place_object(&level, FREE_X, FREE_Y, TOOL_BIG_PIE));
    assert(level.pickup_count == 2 && level.pickups[1].id == 3);
    assert(level.pickups[1].type == PickupType::BIG_PIE);

    assert(place_object(&level, FREE_X + 2, FREE_Y, TOOL_BEAR));
    assert(level.animal_count == 2 && level.animals[1].id == 4);
    assert(level.animals[1].type == AnimalType::BEAR);
    /* A fresh animal patrols a band centred on where it was dropped, and climbs
     * nowhere until it is given a tree. */
    assert(level.animals[1].min_x == FREE_X + 2 - PATROL_HALF_WIDTH);
    assert(level.animals[1].max_x == FREE_X + 2 + PATROL_HALF_WIDTH);
    assert(level.animals[1].tree_id == NO_ID);
    assert(level.animals[1].climb_min == FREE_Y && level.animals[1].climb_max == FREE_Y);

    assert(place_object(&level, FREE_X + 4, FREE_Y, TOOL_OAK));
    assert(level.tree_count == 1 && level.trees[0].id == 5);
    assert(level.trees[0].type == TreeType::OAK);
    assert(level.trees[0].height == DEFAULT_TREE_HEIGHT);

    assert(!place_object(&level, FREE_X + 6, FREE_Y, TOOL_COUNT));
    assert(valid(&level));

    assert(find_object(&level, FREE_X, FREE_Y, &kind, &index));
    assert(kind == PropertyKind::PICKUP && index == 1);
    assert(find_object(&level, FREE_X + 2, FREE_Y, &kind, &index));
    assert(kind == PropertyKind::ANIMAL && index == 1);
    assert(find_object(&level, FREE_X + 4, FREE_Y, &kind, &index));
    assert(kind == PropertyKind::TREE && index == 0);
    assert(!find_object(&level, FREE_X + 6, FREE_Y, &kind, &index));

    /* Erasing the pickup frees ID 3, which the next object of any kind takes. */
    assert(erase_object(&level, FREE_X, FREE_Y));
    assert(level.pickup_count == 1);
    assert(place_object(&level, FREE_X, FREE_Y, TOOL_FIR));
    assert(level.trees[1].id == 3);
    level_free(&level);
}

static void test_placement_limits(void)
{
    LevelData level;
    unsigned i;
    assert(make_blank(&level));
    for (i = level.pickup_count; i < MAX_PICKUPS; ++i)
        assert(place_object(&level, i, 0, TOOL_RED_BERRY));
    assert(level.pickup_count == MAX_PICKUPS);
    assert(!place_object(&level, 0, 1, TOOL_RED_BERRY));

    for (i = level.animal_count; i < MAX_ENEMIES; ++i)
        assert(place_object(&level, i, 2, TOOL_RABBIT));
    assert(!place_object(&level, 0, 3, TOOL_RABBIT));

    for (i = level.tree_count; i < MAX_TREES; ++i)
        assert(place_object(&level, i, 4, TOOL_FIR));
    assert(!place_object(&level, 0, 5, TOOL_FIR));
    assert(valid(&level));
    level_free(&level);
}

/* Erasing must take the cross-references with it: level_validate rejects an
 * encounter whose animal is gone and an animal whose tree is gone, so a delete
 * that left either behind would produce a level the editor cannot save. */
static void test_erase_clears_references(void)
{
    LevelData level;
    u16 tree_id;
    assert(make_blank(&level));
    assert(place_object(&level, FREE_X, FREE_Y, TOOL_OAK));
    tree_id = level.trees[0].id;
    assert(place_object(&level, FREE_X + 2, FREE_Y, TOOL_BEAR));
    assert(adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::TREE, 1));
    assert(level.animals[1].tree_id == tree_id);

    assert(erase_object(&level, FREE_X, FREE_Y));
    assert(level.tree_count == 0 && level.animals[1].tree_id == NO_ID);
    assert(valid(&level));

    /* The blank level's guardian: erasing it drops its encounter with it, which
     * costs the level its one required guardian rather than orphaning a record. */
    assert(level.encounter_count == 1);
    assert(erase_object(&level, level.animals[0].x, level.animals[0].y));
    assert(level.encounter_count == 0);
    assert(!valid(&level));
    level_free(&level);
}

/* Property edits clamp at both ends rather than wrapping into nonsense, and the
 * ranges they clamp to are the ones level_validate enforces. */
static void test_property_clamps(void)
{
    LevelData level;
    AnimalSpawn *animal;
    unsigned i;
    assert(make_blank(&level));
    assert(place_object(&level, FREE_X, FREE_Y, TOOL_BEAR));
    animal = &level.animals[1];

    for (i = 0; i < BLANK_WIDTH + 8; ++i) {
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::PATROL_LEFT, -1);
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::PATROL_RIGHT, 1);
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::CLIMB_TOP, -1);
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::CLIMB_BASE, 1);
    }
    assert(animal->min_x == 0 && animal->max_x == level.width - 1);
    assert(animal->climb_min == 0 && animal->climb_max == LEVEL_HEIGHT - 1);
    assert(valid(&level));

    /* Driven the other way they collapse onto their partner, never past it. */
    for (i = 0; i < BLANK_WIDTH + 8; ++i) {
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::PATROL_LEFT, 1);
        adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::CLIMB_TOP, 1);
    }
    assert(animal->min_x == animal->x && animal->climb_min == animal->climb_max);
    assert(valid(&level));

    for (i = 0; i < MAX_TREE_HEIGHT + 4; ++i)
        adjust_property(&level, PropertyKind::TREE, 0, TreeField::HEIGHT, -1);
    assert(place_object(&level, FREE_X + 2, FREE_Y, TOOL_FIR));
    for (i = 0; i < MAX_TREE_HEIGHT + 4; ++i)
        adjust_property(&level, PropertyKind::TREE, 0, TreeField::HEIGHT, -1);
    assert(level.trees[0].height == 1);
    for (i = 0; i < MAX_TREE_HEIGHT + 4; ++i)
        adjust_property(&level, PropertyKind::TREE, 0, TreeField::HEIGHT, 1);
    assert(level.trees[0].height == MAX_TREE_HEIGHT);
    assert(valid(&level));
    level_free(&level);
}

/* Subtype and theme cycle rather than clamping, so a full lap returns the
 * original value and the panel never shows an out-of-range index. */
static void test_property_wraps(void)
{
    LevelData level;
    unsigned i;
    assert(make_blank(&level));
    for (i = 0; i < Theme::COUNT; ++i)
        adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::THEME, 1);
    assert(level.theme == Theme::GARDEN);
    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::THEME, -1);
    assert(level.theme == Theme::DEEP);

    for (i = 0; i < AnimalType::COUNT; ++i)
        adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::SUBTYPE, 1);
    assert(level.animals[0].type == AnimalType::RABBIT);
    adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::SUBTYPE, -1);
    assert(level.animals[0].type == AnimalType::BEAR);

    for (i = 0; i < PickupType::COUNT; ++i)
        adjust_property(&level, PropertyKind::PICKUP, 0, PickupField::SUBTYPE, 1);
    assert(level.pickups[0].type == PickupType::RED);

    /* The seed is the one cyclic field with a floor of 1, because 0 would leave
     * the cloud band identical on every level. */
    level.cloud_seed = 1;
    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::CLOUD_SEED, -1);
    assert(level.cloud_seed == MAX_CLOUD_SEED);
    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::CLOUD_SEED, 1);
    assert(level.cloud_seed == 1);
    level_free(&level);
}

/* Editing a reward or answer turns a plain animal into a guardian in one step,
 * creating the encounter on demand and marking the animal talkable. */
static void test_encounter_created_on_demand(void)
{
    LevelData level;
    const Encounter *encounter;
    u16 id;
    assert(make_blank(&level));
    assert(place_object(&level, FREE_X, FREE_Y, TOOL_RABBIT));
    id = level.animals[1].id;
    assert(!encounter_for(&level, id, 0));
    assert(!(level.animals[1].flags & 1));

    assert(adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::REWARD, 1));
    encounter = encounter_for(&level, id, 0);
    assert(encounter && encounter->reward == Reward::BLUE);
    assert(encounter->retry_frames == DEFAULT_RETRY_FRAMES);
    assert(level.animals[1].flags & 1);
    assert(level.encounter_count == 2 && encounter->id != level.encounters[0].id);
    /* Only the blank level's guardian is required, so the level stays valid with
     * a second, optional encounter on it. */
    assert(!encounter->required && valid(&level));

    /* A dialogue ID set on the animal reaches the encounter that answers for it.
     * An animal with no dialogue lands on 1 whichever way it is nudged, and only
     * then does the field cycle -- within the byte the KLV stores it in. */
    assert(level.animals[1].dialogue_id == NO_ID);
    assert(adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::DIALOGUE, -1));
    assert(level.animals[1].dialogue_id == 1);
    assert(adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::DIALOGUE, -1));
    assert(level.animals[1].dialogue_id == MAX_DIALOGUE_ID);
    assert(encounter_for(&level, id, 0)->dialogue_id == MAX_DIALOGUE_ID);
    assert(adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::DIALOGUE, 1));
    assert(level.animals[1].dialogue_id == 1);
    assert(valid(&level));
    level_free(&level);
}

/* Cycling the tree field walks every tree and passes through "no tree" at each
 * end, so an animal can always be detached without deleting anything. */
static void test_tree_association_cycles(void)
{
    LevelData level;
    const AnimalSpawn *animal;
    u16 first, second;
    assert(make_blank(&level));
    assert(place_object(&level, FREE_X, FREE_Y, TOOL_FIR));
    assert(place_object(&level, FREE_X + 2, FREE_Y, TOOL_OAK));
    first = level.trees[0].id;
    second = level.trees[1].id;
    animal = &level.animals[0];
    assert(animal->tree_id == NO_ID);

    adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::TREE, 1);
    assert(animal->tree_id == first);
    adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::TREE, 1);
    assert(animal->tree_id == second);
    adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::TREE, 1);
    assert(animal->tree_id == NO_ID);
    adjust_property(&level, PropertyKind::ANIMAL, 0, AnimalField::TREE, -1);
    assert(animal->tree_id == second);
    assert(animal_by_id(&level, animal->id) == animal);
    assert(valid(&level));
    level_free(&level);
}

/* Out-of-range targets are refused rather than writing past an array. */
static void test_out_of_range_targets(void)
{
    LevelData level;
    assert(make_blank(&level));
    assert(!adjust_property(&level, PropertyKind::PICKUP, level.pickup_count, 0, 1));
    assert(!adjust_property(&level, PropertyKind::ANIMAL, level.animal_count, 0, 1));
    assert(!adjust_property(&level, PropertyKind::TREE, level.tree_count, 0, 1));
    assert(!erase_object(&level, BLANK_WIDTH - 1, 0));
    assert(valid(&level));
    level_free(&level);
}

int main(void)
{
    test_blank_is_valid();
    test_valid_83();
    test_placement_and_ids();
    test_placement_limits();
    test_erase_clears_references();
    test_property_clamps();
    test_property_wraps();
    test_encounter_created_on_demand();
    test_tree_association_cycles();
    test_out_of_range_targets();
    puts("host editor tests: PASS");
    return 0;
}
