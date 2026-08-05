#include "assets.h"
#include "editcore.h"
#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

#define FILENAME_MAX_LEN 80
#define ERROR_SIZE 96

/* Which of the three overlaid documents the cursor edits. Tiles and objects are
 * painted with the tool; markers are placed by their own keys. */
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

static void handle_property_modal(Editor *editor)
{
    PropertyModal *prop = &editor->prop;
    unsigned count = assets_property_field_count(prop->kind);
    if (key_pressed(Key::ESCAPE)) {
        editor->assets.level = prop->backup;
        prop->open = 0;
        keyboard_clear_edges();
        return;
    }
    if (key_pressed(Key::ENTER)) {
        prop->open = 0;
        editor->dirty = 1;
        keyboard_clear_edges();
        return;
    }
    if (key_pressed(Key::UP)) prop->field = prop->field ? prop->field - 1 : count - 1;
    if (key_pressed(Key::DOWN)) prop->field = (prop->field + 1) % count;
    if (key_pressed(Key::LEFT))
        adjust_property(&editor->assets.level, prop->kind, prop->index, prop->field, -1);
    if (key_pressed(Key::RIGHT))
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
    if (key_pressed(Key::ENTER)) {
        if (level_save(&editor->assets.level, editor->filename,
                       editor->error, sizeof(editor->error))) return 0;
        editor->confirm_exit = 0;
    } else if (key_pressed(Key::DELETE)) {
        return 0;
    } else if (key_pressed(Key::ESCAPE)) {
        editor->confirm_exit = 0;
    }
    return 1;
}

static void move_cursor(Editor *editor)
{
    if (key_pressed(Key::LEFT) && editor->cursor_x) --editor->cursor_x;
    if (key_pressed(Key::RIGHT) && editor->cursor_x + 1 < editor->assets.level.width)
        ++editor->cursor_x;
    if (key_pressed(Key::UP) && editor->cursor_y) --editor->cursor_y;
    if (key_pressed(Key::DOWN) && editor->cursor_y + 1 < LEVEL_HEIGHT)
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

static void place_marker(Editor *editor, Point *marker)
{
    marker->x = (u16)editor->cursor_x;
    marker->y = (u16)editor->cursor_y;
    editor->dirty = 1;
}

static void handle_editing(Editor *editor)
{
    LevelData *level = &editor->assets.level;
    if (key_pressed(Key::F1)) {
        editor->help = 1;
        keyboard_clear_edges();
    }
    if (key_pressed(Key::ESCAPE)) {
        editor->confirm_exit = 1;
        keyboard_clear_edges();
    }
    move_cursor(editor);
    if (key_pressed(Key::TAB)) editor->layer = (editor->layer + 1) % LAYER_COUNT;
    if (key_pressed(Key::PAGE_UP)) editor->tool = (editor->tool + 1) % TOOL_COUNT;
    if (key_pressed(Key::PAGE_DOWN))
        editor->tool = editor->tool ? editor->tool - 1 : TOOL_COUNT - 1;
    if (key_pressed(Key::SPACE)) paint_at_cursor(editor);
    if (key_pressed(Key::DELETE)) erase_at_cursor(editor);
    if (key_pressed(Key::DIGIT_1)) place_marker(editor, &level->start);
    if (key_pressed(Key::DIGIT_2)) {
        if (!level->checkpoint_count) level->checkpoint_count = 1;
        place_marker(editor, &level->checkpoints[0]);
    }
    if (key_pressed(Key::DIGIT_3)) place_marker(editor, &level->exit);
    if (key_pressed(Key::ENTER) && editor->layer == LAYER_OBJECT) {
        unsigned kind, index;
        if (find_object(level, editor->cursor_x, editor->cursor_y, &kind, &index))
            open_property_modal(editor, kind, index);
    }
    if (key_pressed(Key::F2) &&
        level_save(level, editor->filename, editor->error, sizeof(editor->error)))
        editor->dirty = 0;
    if (key_pressed(Key::F3))
        editor->valid = level_validate(level, editor->error, sizeof(editor->error));
    if (key_pressed(Key::F4)) open_property_modal(editor, PropertyKind::LEVEL, 0);
}

static void render_editor(Editor *editor)
{
    /* The preview is rebuilt every frame because the editor mutates the level in
     * place, and GameState caches spawn positions taken from it. */
    game_init(&editor->preview, &editor->assets);
    editor->preview.camera_x = (s32)(editor->cursor_x * TILE_SIZE > CAMERA_OFFSET
        ? editor->cursor_x * TILE_SIZE - CAMERA_OFFSET : 0) << FP_SHIFT;
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
    AnimalSpawn *animal;
    Encounter *encounter;
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
    animal->id = 3;
    animal->type = AnimalType::FOX;
    animal->x = 60;
    animal->y = 8;
    animal->min_x = 57;
    animal->max_x = 63;
    animal->tree_id = animal->dialogue_id = NO_ID;
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
            if (key_pressed(Key::F1) || key_pressed(Key::ESCAPE)) {
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
