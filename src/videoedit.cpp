#include "videoint.h"

#include "assets.h"
#include "video.h"

/* The editor's screen furniture: the cursor and status bar over a live frame,
 * the help and exit prompts, and the property panel. Each one draws the real
 * game frame first and overlays itself, which is what makes the editor show
 * exactly what the player will see rather than a second approximation of it.
 *
 * KOLOBOK.EXE never calls any of this, so it lives here rather than in
 * src/video.cpp and only KOLOEDIT.EXE pays for it. */

static const char *theme_name(u8 theme)
{
    return theme == Theme::GARDEN ? "GARDEN"
         : theme == Theme::FOREST ? "FOREST" : "DEEP";
}

void video_render_editor(const GameState *game, unsigned cursor_x, unsigned cursor_y,
                         unsigned layer, unsigned tool, int dirty, int valid)
{
    static const char *layers[3] = {"TILE", "OBJECT", "MARKER"};
    int camera = (int)(game->camera_x >> FP_SHIFT);
    int x, y;
    /* video_render_game picks the pan for this frame, so the cursor position can
     * only be placed once it has run. */
    video_render_game(game);
    x = (int)cursor_x * TILE_SIZE - camera + draw_pan;
    y = HUD_H + (int)cursor_y * TILE_SIZE;
    fill_rect(x, y, TILE_SIZE, 2, COLOR_LEMON);
    fill_rect(x, y + TILE_SIZE - 2, TILE_SIZE, 2, COLOR_LEMON);
    fill_rect(x, y, 2, TILE_SIZE, COLOR_LEMON);
    fill_rect(x + TILE_SIZE - 2, y, 2, TILE_SIZE, COLOR_LEMON);
    fill_rect(0, 0, LOGICAL_W, HUD_H, COLOR_NIGHT);
    draw_text(3, 3, "X", COLOR_GREY_LIGHT, 1);
    draw_number(10, 3, cursor_x, COLOR_WHITE);
    draw_text(35, 3, "Y", COLOR_GREY_LIGHT, 1);
    draw_number(42, 3, cursor_y, COLOR_WHITE);
    draw_text(62, 3, layers[layer % 3], COLOR_LEMON, 1);
    draw_text(112, 3, "TOOL", COLOR_GREY_LIGHT, 1);
    draw_number(143, 3, tool, COLOR_WHITE);
    draw_text(168, 3, theme_name(game->assets->level.theme), COLOR_GOLD, 1);
    draw_text(224, 3, valid ? "VALID" : "INVALID",
              valid ? COLOR_LEAF : COLOR_RED_BRIGHT, 1);
    if (dirty) draw_text(299, 3, "D", COLOR_YELLOW, 1);
    render_state = RENDER_GAME;
}

void video_render_editor_help(const GameState *game)
{
    static const char *lines[6] = {
        "ARROWS MOVE  TAB LAYER",
        "PGUP PGDN SELECT  SPACE PAINT",
        "DELETE ERASE  ENTER PROPERTIES",
        "1 START  2 CHECKPOINT  3 EXIT",
        "F2 SAVE  F3 VALIDATE  F4 LEVEL",
        "ESC SAVE DISCARD CANCEL"
    };
    unsigned i;
    video_render_game(game);
    fill_rect(18 + draw_pan, 31, 284, 142, COLOR_NIGHT);
    fill_rect(22 + draw_pan, 35, 276, 134, COLOR_PINE);
    draw_text(119 + draw_pan, 42, "KOLOEDIT HELP", COLOR_WHITE, 1);
    for (i = 0; i < 6; ++i)
        draw_text(34 + draw_pan, 60 + (int)i * 14, lines[i], COLOR_GREY_LIGHT, 1);
    draw_text(88 + draw_pan, 151, "F1 CLOSE HELP", COLOR_LEMON, 1);
    render_state = RENDER_PAUSE;
}

void video_render_editor_exit(const GameState *game)
{
    video_render_game(game);
    overlay_box(COLOR_WOOD);
    draw_text(85 + draw_pan, 73, "UNSAVED CHANGES", COLOR_WHITE, 1);
    draw_text(78 + draw_pan, 94, "ENTER SAVE AND QUIT", COLOR_LEMON, 1);
    draw_text(78 + draw_pan, 108, "DELETE DISCARD", COLOR_GREY_LIGHT, 1);
    draw_text(78 + draw_pan, 122, "ESC CANCEL", COLOR_GREY_LIGHT, 1);
    render_state = RENDER_PAUSE;
}

static const Encounter *encounter_for_animal(const LevelData *level, u16 animal_id)
{
    unsigned i;
    for (i = 0; i < level->encounter_count; ++i)
        if (level->encounters[i].animal_id == animal_id) return &level->encounters[i];
    return 0;
}

#define PROPERTY_LABEL_X 51
#define PROPERTY_VALUE_X 160
#define ANIMAL_VALUE_X 173

static void draw_property_name(int y, const char *name)
{
    draw_text(PROPERTY_LABEL_X + draw_pan, y, name, COLOR_GREY_LIGHT, 1);
}

static void draw_property_text(int x, int y, const char *value, int selected)
{
    draw_text(x + draw_pan, y, value, selected ? COLOR_LEMON : COLOR_WHITE, 1);
}

static void draw_property_number(int x, int y, unsigned value, int selected)
{
    draw_number(x + draw_pan, y, value, selected ? COLOR_LEMON : COLOR_WHITE);
}

static void draw_pickup_row(const Pickup *pickup, unsigned row, int y, int selected)
{
    static const char *types[4] = {"RED BERRY", "BLUE BERRY", "SMALL PIE", "BIG PIE"};
    static const char *names[PickupField::COUNT] = {"SUBTYPE", "FLAGS"};
    draw_property_name(y, names[row]);
    if (row == PickupField::SUBTYPE)
        draw_property_text(PROPERTY_VALUE_X, y, types[pickup->type], selected);
    else
        draw_property_number(PROPERTY_VALUE_X, y, pickup->flags, selected);
}

static void draw_tree_row(const Tree *tree, unsigned row, int y, int selected)
{
    static const char *types[3] = {"FIR", "BIRCH", "OAK"};
    static const char *names[TreeField::COUNT] = {"TREE TYPE", "FLAGS", "HEIGHT"};
    draw_property_name(y, names[row]);
    if (row == TreeField::TYPE)
        draw_property_text(PROPERTY_VALUE_X, y, types[tree->type], selected);
    else
        draw_property_number(PROPERTY_VALUE_X, y,
                             row == TreeField::FLAGS ? tree->flags : tree->height,
                             selected);
}

static void draw_level_row(const LevelData *level, unsigned row, int y, int selected)
{
    static const char *names[LevelField::COUNT] = {
        "THEME", "REQUIRED RED", "CLOUD SEED"
    };
    draw_property_name(y, names[row]);
    if (row == LevelField::THEME)
        draw_property_text(PROPERTY_VALUE_X, y, theme_name(level->theme), selected);
    else
        draw_property_number(PROPERTY_VALUE_X, y,
                             row == LevelField::REQUIRED_RED
                                 ? level->required_red : (unsigned)level->cloud_seed,
                             selected);
}

static void draw_animal_row(const LevelData *level, const AnimalSpawn *animal,
                            unsigned row, int y, int selected)
{
    static const char *names[AnimalField::COUNT] = {
        "SUBTYPE", "FLAGS", "DIALOGUE ID", "REWARD", "CORRECT ANSWER",
        "PATROL LEFT", "PATROL RIGHT", "CLIMB TREE", "CLIMB TOP", "CLIMB BASE"
    };
    static const char *types[4] = {"RABBIT", "FOX", "WOLF", "BEAR"};
    static const char *rewards[3] = {"NONE", "BLUE BERRY", "SMALL PIE"};
    const Encounter *encounter = encounter_for_animal(level, animal->id);
    unsigned value;
    draw_property_name(y, names[row]);
    if (row == AnimalField::SUBTYPE) {
        draw_property_text(ANIMAL_VALUE_X, y, types[animal->type], selected);
        return;
    }
    if (row == AnimalField::REWARD) {
        draw_property_text(ANIMAL_VALUE_X, y,
                           rewards[encounter ? encounter->reward : 0], selected);
        return;
    }
    if (row == AnimalField::TREE && animal->tree_id == NO_ID) {
        draw_property_text(ANIMAL_VALUE_X, y, "NONE", selected);
        return;
    }
    switch (row) {
    case AnimalField::FLAGS:        value = animal->flags; break;
    case AnimalField::DIALOGUE:
        value = animal->dialogue_id == NO_ID ? 0 : animal->dialogue_id;
        break;
    case AnimalField::ANSWER:
        value = encounter ? (unsigned)encounter->correct + 1 : 1;
        break;
    case AnimalField::PATROL_LEFT:  value = animal->min_x; break;
    case AnimalField::PATROL_RIGHT: value = animal->max_x; break;
    case AnimalField::TREE:         value = animal->tree_id; break;
    case AnimalField::CLIMB_TOP:    value = animal->climb_min; break;
    default:                             value = animal->climb_max; break;
    }
    draw_property_number(ANIMAL_VALUE_X, y, value, selected);
}

void video_render_editor_properties(const GameState *game, unsigned kind,
                                    unsigned index, unsigned field)
{
    static const char *titles[4] = {
        "PICKUP PROPERTIES", "ANIMAL PROPERTIES",
        "TREE PROPERTIES", "LEVEL PROPERTIES"
    };
    const LevelData *level = &game->assets->level;
    unsigned row, count = assets_property_field_count(kind);
    video_render_game(game);
    fill_rect(25 + draw_pan, 25, 270, 158, COLOR_NIGHT);
    fill_rect(29 + draw_pan, 29, 262, 150, COLOR_PINE);
    draw_text(80 + draw_pan, 34,
              titles[kind <= PropertyKind::LEVEL ? kind : PropertyKind::LEVEL], COLOR_WHITE, 1);
    for (row = 0; row < count; ++row) {
        int y = 50 + (int)row * 12;
        int selected = row == field;
        draw_text(40 + draw_pan, y, selected ? "1" : " ", COLOR_YELLOW, 1);
        if (kind == PropertyKind::PICKUP && index < level->pickup_count)
            draw_pickup_row(&level->pickups[index], row, y, selected);
        else if (kind == PropertyKind::TREE && index < level->tree_count)
            draw_tree_row(&level->trees[index], row, y, selected);
        else if (kind == PropertyKind::LEVEL)
            draw_level_row(level, row, y, selected);
        else if (kind == PropertyKind::ANIMAL && index < level->animal_count)
            draw_animal_row(level, &level->animals[index], row, y, selected);
    }
    draw_text(46 + draw_pan, 169, "ARROWS EDIT  ENTER OK  ESC CANCEL", COLOR_LEMON, 1);
    render_state = RENDER_PAUSE;
}
