#include "assets.h"
#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLANK_WIDTH 80
#define BLANK_GROUND_ROW 9
#define DEFAULT_RETRY_FRAMES 150
#define DEFAULT_TREE_HEIGHT 3
#define MAX_TREE_HEIGHT 8
#define MAX_DIALOGUE_ID 255
#define MAX_CLOUD_SEED 65535L
#define PATROL_HALF_WIDTH 3
#define FILENAME_MAX_LEN 80
#define ERROR_SIZE 96

/* One tool per paintable thing. On the tile layer the tool *is* the tile index;
 * on the object layer it selects a pickup, animal or tree subtype in that order. */
#define TOOL_COUNT 11
#define TOOL_ANIMAL_FIRST 4
#define TOOL_TREE_FIRST 8

enum Layer { LAYER_TILE, LAYER_OBJECT, LAYER_MARKER, LAYER_COUNT };

typedef struct PropertyModal {
    int open;
    unsigned kind, index, field;
    LevelData backup;
} PropertyModal;

typedef struct Editor {
    AssetPack assets;
    GameState preview;
    PropertyModal prop;
    unsigned cursor_x, cursor_y, layer, tool;
    int dirty, valid, help, confirm_exit;
    char filename[FILENAME_MAX_LEN];
    char error[ERROR_SIZE];
} Editor;

static int make_blank(LevelData *level)
{
    unsigned x;
    memset(level, 0, sizeof(*level));
    level->width = BLANK_WIDTH;
    level->height = KOLO_LEVEL_HEIGHT;
    level->theme = Theme::GARDEN;
    level->required_red = 1;
    level->cloud_seed = 1;
    level->map = (u8 *)calloc(BLANK_WIDTH * KOLO_LEVEL_HEIGHT, 1);
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
    level->animals[0].id = 1;
    level->animals[0].type = AnimalType::RABBIT;
    level->animals[0].x = 70;
    level->animals[0].y = 8;
    level->animals[0].min_x = 65;
    level->animals[0].max_x = 75;
    level->animals[0].tree_id = KOLO_NO_ID;
    level->animals[0].dialogue_id = 1;
    level->animals[0].flags = 1;

    level->encounter_count = 1;
    level->encounters[0].id = 1;
    level->encounters[0].animal_id = 1;
    level->encounters[0].dialogue_id = 1;
    level->encounters[0].required = 1;
    level->encounters[0].correct = 1;
    level->encounters[0].retry_frames = DEFAULT_RETRY_FRAMES;
    return 1;
}

/* Levels are saved next to the DOS executable, so the name has to survive an 8.3
 * filesystem: at most eight stem characters, one dot and three of extension. */
static int valid_83(const char *name)
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

/* Pickups, animals and trees share one ID space, because animals and encounters
 * refer to trees by ID and level_validate rejects any duplicate. */
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

static KoloEncounter *encounter_for(LevelData *level, u16 animal_id, int create)
{
    const KoloAnimalSpawn *animal = 0;
    KoloEncounter *encounter;
    unsigned i;
    for (i = 0; i < level->encounter_count; ++i)
        if (level->encounters[i].animal_id == animal_id) return &level->encounters[i];
    if (!create || level->encounter_count >= KOLO_MAX_ENCOUNTERS) return 0;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == animal_id) animal = &level->animals[i];
    encounter = &level->encounters[level->encounter_count++];
    memset(encounter, 0, sizeof(*encounter));
    encounter->id = next_encounter_id(level);
    encounter->animal_id = animal_id;
    encounter->dialogue_id = (u8)(animal && animal->dialogue_id != KOLO_NO_ID
                                  ? animal->dialogue_id : 1);
    encounter->retry_frames = DEFAULT_RETRY_FRAMES;
    return encounter;
}

static int find_object(const LevelData *level, unsigned x, unsigned y,
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
static void adjust_tree_association(LevelData *level, KoloAnimalSpawn *animal, int delta)
{
    int position = -1, next;
    unsigned i;
    if (!level->tree_count) {
        animal->tree_id = KOLO_NO_ID;
        return;
    }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == animal->tree_id) position = (int)i;
    next = position + delta;
    if (next < -1) next = (int)level->tree_count - 1;
    if (next >= (int)level->tree_count) next = -1;
    animal->tree_id = next < 0 ? KOLO_NO_ID : level->trees[next].id;
}

static int adjust_level_property(LevelData *level, unsigned field, int delta)
{
    if (field == LevelField::THEME) {
        level->theme = wrap_u8(level->theme, delta, Theme::COUNT);
    } else if (field == LevelField::REQUIRED_RED) {
        level->required_red = (u8)clamp_int((int)level->required_red + delta,
                                            0, KOLO_MAX_PICKUPS);
    } else {
        long seed = (long)level->cloud_seed + delta;
        if (seed < 1) seed = MAX_CLOUD_SEED;
        if (seed > MAX_CLOUD_SEED) seed = 1;
        level->cloud_seed = (u32)seed;
    }
    return 1;
}

static int adjust_pickup_property(KoloPickup *pickup, unsigned field, int delta)
{
    if (field == PickupField::SUBTYPE)
        pickup->type = wrap_u8(pickup->type, delta, PickupType::COUNT);
    else
        pickup->flags = (u8)(pickup->flags + delta);
    return 1;
}

static int adjust_tree_property(KoloTree *tree, unsigned field, int delta)
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
    KoloAnimalSpawn *animal = &level->animals[index];
    KoloEncounter *encounter;
    int value;
    switch (field) {
    case AnimalField::SUBTYPE:
        animal->type = wrap_u8(animal->type, delta, AnimalType::COUNT);
        break;
    case AnimalField::FLAGS:
        animal->flags = (u8)(animal->flags + delta);
        break;
    case AnimalField::DIALOGUE:
        value = animal->dialogue_id == KOLO_NO_ID ? 1 : (int)animal->dialogue_id + delta;
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

static int adjust_property(LevelData *level, unsigned kind, unsigned index,
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
    KoloPickup *pickup;
    if (level->pickup_count >= KOLO_MAX_PICKUPS) return 0;
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
    KoloAnimalSpawn *animal;
    if (level->animal_count >= KOLO_MAX_ENEMIES) return 0;
    animal = &level->animals[level->animal_count++];
    memset(animal, 0, sizeof(*animal));
    animal->id = id;
    animal->type = (u8)(tool - TOOL_ANIMAL_FIRST);
    animal->x = (u16)x;
    animal->y = (u16)y;
    animal->min_x = (u16)(x > PATROL_HALF_WIDTH ? x - PATROL_HALF_WIDTH : 0);
    animal->max_x = (u16)(x + PATROL_HALF_WIDTH < level->width
                          ? x + PATROL_HALF_WIDTH : level->width - 1);
    animal->tree_id = animal->dialogue_id = KOLO_NO_ID;
    animal->climb_min = animal->climb_max = (u16)y;
    return 1;
}

static int place_tree(LevelData *level, unsigned x, unsigned y, unsigned tool, u16 id)
{
    KoloTree *tree;
    if (level->tree_count >= KOLO_MAX_TREES) return 0;
    tree = &level->trees[level->tree_count++];
    memset(tree, 0, sizeof(*tree));
    tree->id = id;
    tree->type = (u8)(tool - TOOL_TREE_FIRST);
    tree->x = (u16)x;
    tree->y = (u16)y;
    tree->height = DEFAULT_TREE_HEIGHT;
    return 1;
}

static int place_object(LevelData *level, unsigned x, unsigned y, unsigned tool)
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
                (level->encounter_count - i - 1) * sizeof(KoloEncounter));
        --level->encounter_count;
    }
}

static int erase_object(LevelData *level, unsigned x, unsigned y)
{
    unsigned i;
    for (i = 0; i < level->pickup_count; ++i)
        if (level->pickups[i].x == x && level->pickups[i].y == y) {
            memmove(&level->pickups[i], &level->pickups[i + 1],
                    (level->pickup_count - i - 1) * sizeof(KoloPickup));
            --level->pickup_count;
            return 1;
        }
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].x == x && level->animals[i].y == y) {
            u16 id = level->animals[i].id;
            memmove(&level->animals[i], &level->animals[i + 1],
                    (level->animal_count - i - 1) * sizeof(KoloAnimalSpawn));
            --level->animal_count;
            remove_encounters_for(level, id);
            return 1;
        }
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].x == x && level->trees[i].y == y) {
            u16 id = level->trees[i].id;
            unsigned j;
            memmove(&level->trees[i], &level->trees[i + 1],
                    (level->tree_count - i - 1) * sizeof(KoloTree));
            --level->tree_count;
            for (j = 0; j < level->animal_count; ++j)
                if (level->animals[j].tree_id == id) level->animals[j].tree_id = KOLO_NO_ID;
            return 1;
        }
    return 0;
}

static void handle_property_modal(Editor *editor)
{
    PropertyModal *prop = &editor->prop;
    unsigned count = kolo_property_field_count(prop->kind);
    if (key_pressed(KEY_ESCAPE)) {
        editor->assets.level = prop->backup;
        prop->open = 0;
        keyboard_clear_edges();
        return;
    }
    if (key_pressed(KEY_ENTER)) {
        prop->open = 0;
        editor->dirty = 1;
        keyboard_clear_edges();
        return;
    }
    if (key_pressed(KEY_UP)) prop->field = prop->field ? prop->field - 1 : count - 1;
    if (key_pressed(KEY_DOWN)) prop->field = (prop->field + 1) % count;
    if (key_pressed(KEY_LEFT))
        adjust_property(&editor->assets.level, prop->kind, prop->index, prop->field, -1);
    if (key_pressed(KEY_RIGHT))
        adjust_property(&editor->assets.level, prop->kind, prop->index, prop->field, 1);
}

static void open_property_modal(Editor *editor, unsigned kind, unsigned index)
{
    editor->prop.open = 1;
    editor->prop.kind = kind;
    editor->prop.index = index;
    editor->prop.field = 0;
    editor->prop.backup = editor->assets.level;
    keyboard_clear_edges();
}

/* Returns 0 when the editor should close. */
static int handle_exit_confirm(Editor *editor)
{
    if (key_pressed(KEY_ENTER)) {
        if (level_save(&editor->assets.level, editor->filename,
                       editor->error, sizeof(editor->error))) return 0;
        editor->confirm_exit = 0;
    } else if (key_pressed(KEY_DELETE)) {
        return 0;
    } else if (key_pressed(KEY_ESCAPE)) {
        editor->confirm_exit = 0;
    }
    return 1;
}

static void move_cursor(Editor *editor)
{
    if (key_pressed(KEY_LEFT) && editor->cursor_x) --editor->cursor_x;
    if (key_pressed(KEY_RIGHT) && editor->cursor_x + 1 < editor->assets.level.width)
        ++editor->cursor_x;
    if (key_pressed(KEY_UP) && editor->cursor_y) --editor->cursor_y;
    if (key_pressed(KEY_DOWN) && editor->cursor_y + 1 < KOLO_LEVEL_HEIGHT)
        ++editor->cursor_y;
}

static void paint_at_cursor(Editor *editor)
{
    LevelData *level = &editor->assets.level;
    if (editor->layer == LAYER_TILE) {
        level->map[editor->cursor_y * level->width + editor->cursor_x] = (u8)editor->tool;
        editor->dirty = 1;
    } else if (editor->layer == LAYER_OBJECT) {
        if (place_object(level, editor->cursor_x, editor->cursor_y, editor->tool))
            editor->dirty = 1;
    } else {
        level->start.x = (u16)editor->cursor_x;
        level->start.y = (u16)editor->cursor_y;
        editor->dirty = 1;
    }
}

static void erase_at_cursor(Editor *editor)
{
    LevelData *level = &editor->assets.level;
    if (editor->layer == LAYER_TILE) {
        level->map[editor->cursor_y * level->width + editor->cursor_x] = Tile::AIR;
        editor->dirty = 1;
    } else if (editor->layer == LAYER_OBJECT &&
               erase_object(level, editor->cursor_x, editor->cursor_y)) {
        editor->dirty = 1;
    }
}

static void place_marker(Editor *editor, KoloPoint *marker)
{
    marker->x = (u16)editor->cursor_x;
    marker->y = (u16)editor->cursor_y;
    editor->dirty = 1;
}

static void handle_editing(Editor *editor)
{
    LevelData *level = &editor->assets.level;
    if (key_pressed(KEY_F1)) {
        editor->help = 1;
        keyboard_clear_edges();
    }
    if (key_pressed(KEY_ESCAPE)) {
        editor->confirm_exit = 1;
        keyboard_clear_edges();
    }
    move_cursor(editor);
    if (key_pressed(KEY_TAB)) editor->layer = (editor->layer + 1) % LAYER_COUNT;
    if (key_pressed(KEY_PAGE_UP)) editor->tool = (editor->tool + 1) % TOOL_COUNT;
    if (key_pressed(KEY_PAGE_DOWN))
        editor->tool = editor->tool ? editor->tool - 1 : TOOL_COUNT - 1;
    if (key_pressed(KEY_SPACE)) paint_at_cursor(editor);
    if (key_pressed(KEY_DELETE)) erase_at_cursor(editor);
    if (key_pressed(KEY_1)) place_marker(editor, &level->start);
    if (key_pressed(KEY_2)) {
        if (!level->checkpoint_count) level->checkpoint_count = 1;
        place_marker(editor, &level->checkpoints[0]);
    }
    if (key_pressed(KEY_3)) place_marker(editor, &level->exit);
    if (key_pressed(KEY_ENTER) && editor->layer == LAYER_OBJECT) {
        unsigned kind, index;
        if (find_object(level, editor->cursor_x, editor->cursor_y, &kind, &index))
            open_property_modal(editor, kind, index);
    }
    if (key_pressed(KEY_F2) &&
        level_save(level, editor->filename, editor->error, sizeof(editor->error)))
        editor->dirty = 0;
    if (key_pressed(KEY_F3))
        editor->valid = level_validate(level, editor->error, sizeof(editor->error));
    if (key_pressed(KEY_F4)) open_property_modal(editor, PropertyKind::LEVEL, 0);
}

static void render_editor(Editor *editor)
{
    /* The preview is rebuilt every frame because the editor mutates the level in
     * place, and GameState caches spawn positions taken from it. */
    game_init(&editor->preview, &editor->assets);
    editor->preview.camera_x = (s32)(editor->cursor_x * KOLO_TILE_SIZE > KOLO_CAMERA_OFFSET
        ? editor->cursor_x * KOLO_TILE_SIZE - KOLO_CAMERA_OFFSET : 0) << KOLO_FP_SHIFT;
    if (editor->prop.open)
        video_render_editor_properties(&editor->preview, editor->prop.kind,
                                      editor->prop.index, editor->prop.field);
    else if (editor->help)
        video_render_editor_help(&editor->preview);
    else if (editor->confirm_exit)
        video_render_editor_exit(&editor->preview);
    else
        video_render_editor(&editor->preview, editor->cursor_x, editor->cursor_y,
                           editor->layer, editor->tool, editor->dirty, editor->valid);
    video_present();
}

static int editor_selftest(void)
{
    const char *path = "EDITTEST.KLV";
    LevelData level, check, backup;
    KoloAnimalSpawn *animal;
    KoloEncounter *encounter;
    char error[ERROR_SIZE];
    unsigned field;
    remove(path);
    remove("EDITTEST.TMP");
    if (!make_blank(&level)) return 0;
    level.map[5 * BLANK_WIDTH + 10] = Tile::GRASS_PLATFORM;
    level.checkpoint_count = 1;
    level.checkpoints[0].x = 20;
    level.checkpoints[0].y = 8;
    level.tree_count = 1;
    level.trees[0].id = 10;
    level.trees[0].type = TreeType::OAK;
    level.trees[0].x = 55;
    level.trees[0].y = 8;
    level.trees[0].height = 4;
    level.animal_count = 2;
    animal = &level.animals[1];
    memset(animal, 0, sizeof(*animal));
    animal->id = 2;
    animal->type = AnimalType::FOX;
    animal->x = 60;
    animal->y = 8;
    animal->min_x = 57;
    animal->max_x = 63;
    animal->tree_id = animal->dialogue_id = KOLO_NO_ID;
    animal->climb_min = animal->climb_max = 8;

    /* Escaping a property modal restores the level wholesale, so confirm a struct
     * copy really does undo an edit before relying on it below. */
    backup = level;
    adjust_property(&level, PropertyKind::ANIMAL, 1, AnimalField::SUBTYPE, 1);
    level = backup;
    if (level.animals[1].type != AnimalType::FOX) return 0;

    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::THEME, 1);
    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::REQUIRED_RED, -1);
    adjust_property(&level, PropertyKind::LEVEL, 0, LevelField::CLOUD_SEED, 1);
    for (field = 0; field < AnimalField::COUNT; ++field) {
        int delta = field == AnimalField::PATROL_LEFT ||
                    field == AnimalField::CLIMB_TOP ? -1 : 1;
        adjust_property(&level, PropertyKind::ANIMAL, 1, field, delta);
    }
    if (!level_save(&level, path, error, sizeof(error))) {
        printf("KOLOEDIT SELFTEST FAIL %s\n", error);
        level_free(&level);
        return 0;
    }
    level_free(&level);

    if (!level_load(&check, path, error, sizeof(error))) {
        printf("KOLOEDIT SELFTEST FAIL %s\n", error);
        return 0;
    }
    animal = &check.animals[1];
    encounter = encounter_for(&check, animal->id, 0);
    if (check.map[5 * BLANK_WIDTH + 10] != Tile::GRASS_PLATFORM ||
        check.checkpoint_count != 1 || check.theme != Theme::FOREST ||
        check.required_red != 0 || check.cloud_seed != 2 ||
        animal->type != AnimalType::WOLF || animal->flags != 1 ||
        animal->dialogue_id != 1 || animal->min_x != 56 || animal->max_x != 64 ||
        animal->tree_id != 10 || animal->climb_min != 7 || animal->climb_max != 9 ||
        !encounter || encounter->reward != Reward::BLUE || encounter->correct != 1) {
        level_free(&check);
        return 0;
    }

    /* Saving a level that lost objects must shrink the payload, not leave stale
     * records behind, so round-trip an erase as well as an edit. */
    check.map[5 * BLANK_WIDTH + 10] = Tile::AIR;
    check.checkpoint_count = 0;
    if (!level_save(&check, path, error, sizeof(error))) {
        level_free(&check);
        return 0;
    }
    level_free(&check);
    if (!level_load(&check, path, error, sizeof(error)) ||
        check.map[5 * BLANK_WIDTH + 10] != Tile::AIR || check.checkpoint_count != 0) {
        level_free(&check);
        return 0;
    }
    level_free(&check);
    if (remove(path)) return 0;
    puts("KOLOEDIT SELFTEST PASS create edit properties save reload delete");
    return 1;
}

static int prompt_for_filename(char *filename)
{
    printf("Level filename (8.3): ");
    return scanf("%79s", filename) == 1;
}

/* Opening a name that does not exist yet creates a blank level on disk, so the
 * editor always has something valid to load and save over. */
static int ensure_level_file(const char *filename, char *error, unsigned error_size)
{
    LevelData level;
    FILE *probe = fopen(filename, "rb");
    if (probe) {
        fclose(probe);
        if (!level_load(&level, filename, error, error_size)) return 0;
        level_free(&level);
        return 1;
    }
    if (!make_blank(&level)) {
        strncpy(error, "out of memory", error_size - 1);
        error[error_size - 1] = 0;
        return 0;
    }
    if (!level_save(&level, filename, error, error_size)) {
        level_free(&level);
        return 0;
    }
    level_free(&level);
    return 1;
}

int main(int argc, char **argv)
{
    Editor editor;
    int running = 1;
    if (argc > 1 && !stricmp(argv[1], "-selftest")) return editor_selftest() ? 0 : 3;
    memset(&editor, 0, sizeof(editor));
    editor.tool = 1;
    editor.valid = 1;
    if (argc > 1) strncpy(editor.filename, argv[1], sizeof(editor.filename) - 1);
    else if (!prompt_for_filename(editor.filename)) return 2;
    editor.filename[sizeof(editor.filename) - 1] = 0;
    if (!valid_83(editor.filename)) {
        fprintf(stderr, "KOLOEDIT: filename must use 8.3 form\n");
        return 2;
    }
    if (!ensure_level_file(editor.filename, editor.error, sizeof(editor.error))) {
        fprintf(stderr, "KOLOEDIT: %s\n", editor.error);
        return 2;
    }
    if (!assets_load_bank(&editor.assets, "KOLOBOK.DAT", "GARDEN", editor.filename,
                          editor.error, sizeof(editor.error))) {
        fprintf(stderr, "KOLOEDIT: %s\n", editor.error);
        return 2;
    }
    if (!video_init(&editor.assets) || !keyboard_install()) {
        assets_free(&editor.assets);
        fprintf(stderr, "KOLOEDIT: cannot initialize Mode X editor\n");
        return 4;
    }
    while (running) {
        if (editor.prop.open) {
            handle_property_modal(&editor);
        } else if (editor.help) {
            if (key_pressed(KEY_F1) || key_pressed(KEY_ESCAPE)) {
                editor.help = 0;
                keyboard_clear_edges();
            }
        } else if (editor.confirm_exit) {
            running = handle_exit_confirm(&editor);
        } else {
            handle_editing(&editor);
        }
        render_editor(&editor);
    }
    keyboard_remove();
    video_shutdown();
    assets_free(&editor.assets);
    return 0;
}
