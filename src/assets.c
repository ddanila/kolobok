#include "assets.h"

#include <stdlib.h>
#include <string.h>
#ifdef __WATCOMC__
#include <dos.h>
#include <i86.h>
#endif

/* KLV level file: a 32-byte header holding "KLV4", the version, the header size,
 * a CRC-32 of everything after the header, and the level metadata; then the body,
 * which is the tile map, the three markers, and the object records below. Record
 * sizes appear once here so the reader, the writer and the size check agree. */
#define KLV_SIGNATURE "KLV4"
#define KLV_VERSION 4
#define KLV_HEADER_SIZE 32
#define KLV_MAX_SIZE 8192
#define KLV_POINT_SIZE 4
#define KLV_MARKER_COUNT 3
#define KLV_PICKUP_SIZE 8
#define KLV_ANIMAL_SIZE 20
#define KLV_TREE_SIZE 8
#define KLV_ENCOUNTER_SIZE 10

#define KLV_MIN_WIDTH 32
#define KLV_MAX_WIDTH 256

/* KOLOBOK.DAT archive: "KOLODAT4", a version and a bank count, then one 16-byte
 * index entry per bank (8-byte name, u32 offset, u32 size). */
#define DAT_SIGNATURE "KOLODAT4"
#define DAT_VERSION 4
#define DAT_HEADER_SIZE 12
#define DAT_ENTRY_SIZE 16
#define DAT_NAME_SIZE 8
#define DAT_MAX_BANKS 16

/* Resource bank: "KBANK4\0\0", metadata, a 768-byte palette, planar tiles, the
 * two per-tile tables, then the sprite span streams. */
#define BANK_SIGNATURE "KBANK4\0\0"
#define BANK_SIGNATURE_SIZE 8
#define BANK_VERSION 4
#define BANK_MIN_SIZE 24
#define BANK_MAX_SIZE 61440UL
#define BANK_PALETTE_SIZE 768
#define BANK_MAX_TILES 16
#define TILE_PIXELS 256
#define SPRITE_ROWS 16
#define SPRITE_PLANE_VARIANTS 16
#define PLANAR_ROW_WIDTH 5
#define PATH_MAX_LEN 132

static u16 read_u16(const u8 **cursor)
{
    u16 value = (u16)((*cursor)[0] | ((u16)(*cursor)[1] << 8));
    *cursor += 2;
    return value;
}

static u32 read_u32_at(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void read_point(const u8 **cursor, KoloPoint *point)
{
    point->x = read_u16(cursor);
    point->y = read_u16(cursor);
}

static void write_u16(FILE *file, u16 value)
{
    fputc(value & 255, file);
    fputc(value >> 8, file);
}

static void write_u32(FILE *file, u32 value)
{
    fputc((int)(value & 255), file);
    fputc((int)((value >> 8) & 255), file);
    fputc((int)((value >> 16) & 255), file);
    fputc((int)((value >> 24) & 255), file);
}

static void put_u16(u8 **p, u16 value)
{
    *(*p)++ = (u8)value;
    *(*p)++ = (u8)(value >> 8);
}

static void put_point(u8 **p, KoloPoint point)
{
    put_u16(p, point.x);
    put_u16(p, point.y);
}

u32 assets_crc32(KoloConstFarPtr data, u32 length)
{
    u32 crc = 0xffffffffUL, i;
    int bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
    return crc ^ 0xffffffffUL;
}

static int set_error(char *error, unsigned size, const char *message)
{
    if (error != NULL && size) {
        strncpy(error, message, size - 1);
        error[size - 1] = 0;
    }
    return 0;
}

static u16 far_read_u16_at(KoloConstFarPtr cursor)
{
    return (u16)(cursor[0] | ((u16)cursor[1] << 8));
}

static u32 far_read_u32(KoloConstFarPtr p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int far_equal(KoloConstFarPtr p, const char *text, unsigned length)
{
    unsigned i;
    for (i = 0; i < length; ++i)
        if (p[i] != (u8)text[i]) return 0;
    return 1;
}

unsigned kolo_property_field_count(unsigned kind)
{
    if (kind == KOLO_PROP_PICKUP) return KOLO_PICKUP_FIELD_COUNT;
    if (kind == KOLO_PROP_ANIMAL) return KOLO_ANIMAL_FIELD_COUNT;
    if (kind == KOLO_PROP_TREE) return KOLO_TREE_FIELD_COUNT;
    return KOLO_LEVEL_FIELD_COUNT;
}

void level_free(LevelData *level)
{
    if (level->map != NULL) free(level->map);
    memset(level, 0, sizeof(*level));
}

static u32 level_map_bytes(const LevelData *level)
{
    return (u32)level->width * level->height;
}

static u32 level_object_bytes(const LevelData *level)
{
    return (u32)level->checkpoint_count * KLV_POINT_SIZE +
           (u32)level->pickup_count * KLV_PICKUP_SIZE +
           (u32)level->animal_count * KLV_ANIMAL_SIZE +
           (u32)level->tree_count * KLV_TREE_SIZE +
           (u32)level->encounter_count * KLV_ENCOUNTER_SIZE;
}

static u32 level_body_bytes(const LevelData *level)
{
    return level_map_bytes(level) + KLV_MARKER_COUNT * KLV_POINT_SIZE +
           level_object_bytes(level);
}

static int level_counts_in_range(const LevelData *level)
{
    return level->width >= KLV_MIN_WIDTH && level->width <= KLV_MAX_WIDTH &&
           level->height == KOLO_LEVEL_HEIGHT &&
           level->checkpoint_count <= KOLO_MAX_CHECKPOINTS &&
           level->pickup_count <= KOLO_MAX_PICKUPS &&
           level->animal_count <= KOLO_MAX_ENEMIES &&
           level->tree_count <= KOLO_MAX_TREES &&
           level->encounter_count <= KOLO_MAX_ENCOUNTERS;
}

static int validate_markers(const LevelData *level, char *error, unsigned error_size)
{
    unsigned i;
    if (level->start.x >= level->width || level->start.y >= level->height ||
        level->exit.x >= level->width || level->exit.y >= level->height ||
        level->home.x >= level->width || level->home.y >= level->height)
        return set_error(error, error_size, "level marker is outside map");
    for (i = 0; i < level->checkpoint_count; ++i)
        if (level->checkpoints[i].x >= level->width ||
            level->checkpoints[i].y >= level->height)
            return set_error(error, error_size, "checkpoint is outside map");
    return 1;
}

static int validate_pickups(const LevelData *level, char *error, unsigned error_size)
{
    unsigned i, j, red = 0;
    for (i = 0; i < level->pickup_count; ++i) {
        const KoloPickup *pickup = &level->pickups[i];
        if (pickup->type > KOLO_PICKUP_BIG_PIE ||
            pickup->x >= level->width || pickup->y >= level->height)
            return set_error(error, error_size, "invalid pickup record");
        if (pickup->type == KOLO_PICKUP_RED) ++red;
        for (j = 0; j < i; ++j)
            if (level->pickups[j].id == pickup->id)
                return set_error(error, error_size, "duplicate pickup ID");
    }
    if (red < level->required_red)
        return set_error(error, error_size, "not enough red berries");
    return 1;
}

static int validate_animals(const LevelData *level, char *error, unsigned error_size)
{
    unsigned i, j;
    for (i = 0; i < level->animal_count; ++i) {
        const KoloAnimalSpawn *animal = &level->animals[i];
        if (animal->type > KOLO_ANIMAL_BEAR ||
            animal->x >= level->width || animal->y >= level->height ||
            animal->min_x > animal->x || animal->max_x < animal->x ||
            animal->max_x >= level->width)
            return set_error(error, error_size, "invalid animal record");
        for (j = 0; j < i; ++j)
            if (level->animals[j].id == animal->id)
                return set_error(error, error_size, "duplicate animal ID");
    }
    return 1;
}

static int validate_trees(const LevelData *level, char *error, unsigned error_size)
{
    unsigned i, j;
    for (i = 0; i < level->tree_count; ++i) {
        const KoloTree *tree = &level->trees[i];
        if (tree->type > KOLO_TREE_OAK || tree->x >= level->width ||
            tree->y >= level->height || !tree->height)
            return set_error(error, error_size, "invalid tree record");
        for (j = 0; j < i; ++j)
            if (level->trees[j].id == tree->id)
                return set_error(error, error_size, "duplicate tree ID");
    }
    return 1;
}

static int tree_exists(const LevelData *level, u16 tree_id)
{
    unsigned i;
    for (i = 0; i < level->tree_count; ++i)
        if (level->trees[i].id == tree_id) return 1;
    return 0;
}

static int animal_exists(const LevelData *level, u16 animal_id)
{
    unsigned i;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].id == animal_id) return 1;
    return 0;
}

static int validate_cross_references(const LevelData *level, char *error,
                                     unsigned error_size)
{
    unsigned i, j, required = 0;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].tree_id != KOLO_NO_ID &&
            !tree_exists(level, level->animals[i].tree_id))
            return set_error(error, error_size, "animal refers to missing tree");
    for (i = 0; i < level->encounter_count; ++i) {
        const KoloEncounter *encounter = &level->encounters[i];
        if (!animal_exists(level, encounter->animal_id))
            return set_error(error, error_size, "encounter refers to missing animal");
        if (encounter->correct > 2 || encounter->reward > KOLO_REWARD_SMALL_PIE)
            return set_error(error, error_size, "invalid encounter record");
        for (j = 0; j < i; ++j)
            if (level->encounters[j].id == encounter->id)
                return set_error(error, error_size, "duplicate encounter ID");
        if (encounter->required) ++required;
    }
    /* Exactly one guardian: game_exit_ready gates the exit on the required
     * encounter, so zero makes the level unfinishable and two is ambiguous. */
    if (required != 1)
        return set_error(error, error_size, "level needs exactly one guardian");
    return 1;
}

int level_validate(const LevelData *level, char *error, unsigned error_size)
{
    u32 i;
    if (level->width < KLV_MIN_WIDTH || level->width > KLV_MAX_WIDTH ||
        level->height != KOLO_LEVEL_HEIGHT)
        return set_error(error, error_size, "level must be 32..256 by 11 tiles");
    if (level->theme > KOLO_THEME_DEEP || level->map == NULL)
        return set_error(error, error_size, "invalid level theme or tile map");
    if (!level_counts_in_range(level))
        return set_error(error, error_size, "level object limit exceeded");
    if (!validate_markers(level, error, error_size)) return 0;
    for (i = 0; i < level_map_bytes(level); ++i)
        if (level->map[i] >= KOLO_TILE_COUNT)
            return set_error(error, error_size, "unknown tile in level");
    if (!validate_pickups(level, error, error_size)) return 0;
    if (!validate_animals(level, error, error_size)) return 0;
    if (!validate_trees(level, error, error_size)) return 0;
    return validate_cross_references(level, error, error_size);
}

static void read_pickup(const u8 **p, KoloPickup *pickup)
{
    pickup->type = *(*p)++;
    pickup->flags = *(*p)++;
    pickup->id = read_u16(p);
    pickup->x = read_u16(p);
    pickup->y = read_u16(p);
}

static void read_animal(const u8 **p, KoloAnimalSpawn *animal)
{
    animal->type = *(*p)++;
    animal->flags = *(*p)++;
    animal->id = read_u16(p);
    animal->x = read_u16(p);
    animal->y = read_u16(p);
    animal->min_x = read_u16(p);
    animal->max_x = read_u16(p);
    animal->tree_id = read_u16(p);
    animal->climb_min = read_u16(p);
    animal->climb_max = read_u16(p);
    animal->dialogue_id = read_u16(p);
}

/* A tree's row and height share one 16-bit field, low byte first. */
static void read_tree(const u8 **p, KoloTree *tree)
{
    u16 row_and_height;
    tree->type = *(*p)++;
    tree->flags = *(*p)++;
    tree->id = read_u16(p);
    tree->x = read_u16(p);
    row_and_height = read_u16(p);
    tree->y = (u8)row_and_height;
    tree->height = (u8)(row_and_height >> 8);
}

static void read_encounter(const u8 **p, KoloEncounter *encounter)
{
    encounter->id = read_u16(p);
    encounter->animal_id = read_u16(p);
    encounter->dialogue_id = *(*p)++;
    encounter->required = *(*p)++;
    encounter->correct = *(*p)++;
    encounter->reward = *(*p)++;
    encounter->retry_frames = read_u16(p);
}

static void write_pickup(u8 **p, const KoloPickup *pickup)
{
    *(*p)++ = pickup->type;
    *(*p)++ = pickup->flags;
    put_u16(p, pickup->id);
    put_u16(p, pickup->x);
    put_u16(p, pickup->y);
}

static void write_animal(u8 **p, const KoloAnimalSpawn *animal)
{
    *(*p)++ = animal->type;
    *(*p)++ = animal->flags;
    put_u16(p, animal->id);
    put_u16(p, animal->x);
    put_u16(p, animal->y);
    put_u16(p, animal->min_x);
    put_u16(p, animal->max_x);
    put_u16(p, animal->tree_id);
    put_u16(p, animal->climb_min);
    put_u16(p, animal->climb_max);
    put_u16(p, animal->dialogue_id);
}

static void write_tree(u8 **p, const KoloTree *tree)
{
    *(*p)++ = tree->type;
    *(*p)++ = tree->flags;
    put_u16(p, tree->id);
    put_u16(p, tree->x);
    put_u16(p, (u16)(tree->y | ((u16)tree->height << 8)));
}

static void write_encounter(u8 **p, const KoloEncounter *encounter)
{
    put_u16(p, encounter->id);
    put_u16(p, encounter->animal_id);
    *(*p)++ = encounter->dialogue_id;
    *(*p)++ = encounter->required;
    *(*p)++ = encounter->correct;
    *(*p)++ = encounter->reward;
    put_u16(p, encounter->retry_frames);
}

static u8 *read_level_file(const char *path, u32 *size, char *error,
                           unsigned error_size)
{
    FILE *file = fopen(path, "rb");
    long measured;
    u8 *blob;
    if (!file) {
        set_error(error, error_size, "cannot open KLV level");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) || (measured = ftell(file)) < KLV_HEADER_SIZE ||
        measured > KLV_MAX_SIZE || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        set_error(error, error_size, "cannot measure KLV level");
        return NULL;
    }
    blob = (u8 *)malloc((unsigned)measured);
    if (!blob) {
        fclose(file);
        set_error(error, error_size, "not enough memory for level");
        return NULL;
    }
    if (fread(blob, 1, (unsigned)measured, file) != (unsigned)measured) {
        fclose(file);
        free(blob);
        set_error(error, error_size, "short read from KLV level");
        return NULL;
    }
    fclose(file);
    *size = (u32)measured;
    return blob;
}

static int read_level_header(LevelData *level, const u8 *blob, u32 size,
                            char *error, unsigned error_size)
{
    const u8 *p = blob + 4;
    u16 version = read_u16(&p);
    u16 header_size = read_u16(&p);
    u32 crc = read_u32_at(p);
    p += 4;
    if (memcmp(blob, KLV_SIGNATURE, 4))
        return set_error(error, error_size, "unsupported KLV signature");
    if (version != KLV_VERSION || header_size != KLV_HEADER_SIZE)
        return set_error(error, error_size, "unsupported KLV version");
    if (assets_crc32(blob + KLV_HEADER_SIZE, size - KLV_HEADER_SIZE) != crc)
        return set_error(error, error_size, "KLV checksum mismatch");
    level->width = read_u16(&p);
    level->height = read_u16(&p);
    level->theme = *p++;
    level->required_red = *p++;
    level->cloud_seed = read_u32_at(p);
    p += 4;
    level->checkpoint_count = read_u16(&p);
    level->pickup_count = read_u16(&p);
    level->animal_count = read_u16(&p);
    level->tree_count = read_u16(&p);
    level->encounter_count = read_u16(&p);
    if (!level_counts_in_range(level))
        return set_error(error, error_size, "invalid KLV metadata");
    return 1;
}

int level_load(LevelData *level, const char *path, char *error, unsigned error_size)
{
    u8 *blob;
    const u8 *p, *end;
    u32 size, map_bytes;
    unsigned i;
    memset(level, 0, sizeof(*level));
    blob = read_level_file(path, &size, error, error_size);
    if (!blob) return 0;
    if (!read_level_header(level, blob, size, error, error_size)) {
        free(blob);
        memset(level, 0, sizeof(*level));
        return 0;
    }
    p = blob + KLV_HEADER_SIZE;
    end = blob + size;
    map_bytes = level_map_bytes(level);
    if ((u32)(end - p) < level_body_bytes(level)) {
        free(blob);
        memset(level, 0, sizeof(*level));
        return set_error(error, error_size, "truncated KLV payload");
    }
    level->map = (u8 *)malloc((unsigned)map_bytes);
    if (!level->map) {
        free(blob);
        return set_error(error, error_size, "not enough memory for tile map");
    }
    memcpy(level->map, p, (unsigned)map_bytes);
    p += map_bytes;
    read_point(&p, &level->start);
    read_point(&p, &level->exit);
    read_point(&p, &level->home);
    for (i = 0; i < level->checkpoint_count; ++i) read_point(&p, &level->checkpoints[i]);
    for (i = 0; i < level->pickup_count; ++i) read_pickup(&p, &level->pickups[i]);
    for (i = 0; i < level->animal_count; ++i) read_animal(&p, &level->animals[i]);
    for (i = 0; i < level->tree_count; ++i) read_tree(&p, &level->trees[i]);
    for (i = 0; i < level->encounter_count; ++i) read_encounter(&p, &level->encounters[i]);
    if (p != end) {
        free(blob);
        level_free(level);
        return set_error(error, error_size, "unexpected KLV payload size");
    }
    free(blob);
    if (!level_validate(level, error, error_size)) {
        level_free(level);
        return 0;
    }
    return 1;
}

static u8 *build_level_body(const LevelData *level, u32 body_size)
{
    u8 *body = (u8 *)malloc((unsigned)body_size);
    u8 *p = body;
    unsigned i;
    if (!body) return NULL;
    memcpy(p, level->map, (unsigned)level_map_bytes(level));
    p += level_map_bytes(level);
    put_point(&p, level->start);
    put_point(&p, level->exit);
    put_point(&p, level->home);
    for (i = 0; i < level->checkpoint_count; ++i) put_point(&p, level->checkpoints[i]);
    for (i = 0; i < level->pickup_count; ++i) write_pickup(&p, &level->pickups[i]);
    for (i = 0; i < level->animal_count; ++i) write_animal(&p, &level->animals[i]);
    for (i = 0; i < level->tree_count; ++i) write_tree(&p, &level->trees[i]);
    for (i = 0; i < level->encounter_count; ++i) write_encounter(&p, &level->encounters[i]);
    return body;
}

static void write_level_header(FILE *file, const LevelData *level, u32 crc)
{
    fwrite(KLV_SIGNATURE, 1, 4, file);
    write_u16(file, KLV_VERSION);
    write_u16(file, KLV_HEADER_SIZE);
    write_u32(file, crc);
    write_u16(file, level->width);
    write_u16(file, level->height);
    fputc(level->theme, file);
    fputc(level->required_red, file);
    write_u32(file, level->cloud_seed);
    write_u16(file, level->checkpoint_count);
    write_u16(file, level->pickup_count);
    write_u16(file, level->animal_count);
    write_u16(file, level->tree_count);
    write_u16(file, level->encounter_count);
}

/* Replaces the extension so KOLO.KLV becomes KOLO.TMP rather than KOLO.KLV.TMP,
 * which an 8.3 filesystem would truncate. A dot inside a directory name is not an
 * extension, so only one after the last separator counts. */
static void with_extension(char *out, const char *path, const char *extension)
{
    char *dot, *slash;
    strcpy(out, path);
    dot = strrchr(out, '.');
    slash = strrchr(out, '/');
    if (dot != NULL && (slash == NULL || dot > slash)) *dot = 0;
    strcat(out, extension);
}

/* Writes to a temporary file, reloads it to prove it parses, and only then swaps
 * it into place over a backup, so a failure never destroys the previous level. */
int level_save(const LevelData *level, const char *path, char *error,
               unsigned error_size)
{
    char temp[PATH_MAX_LEN], backup[PATH_MAX_LEN];
    LevelData check;
    FILE *file;
    u8 *body;
    u32 body_size, crc;
    int had_original;
    if (!level_validate(level, error, error_size)) return 0;
    if (strlen(path) + 5 >= sizeof(temp))
        return set_error(error, error_size, "level filename is too long");
    body_size = level_body_bytes(level);
    body = build_level_body(level, body_size);
    if (!body) return set_error(error, error_size, "not enough memory to save level");
    crc = assets_crc32(body, body_size);

    with_extension(temp, path, ".TMP");
    with_extension(backup, path, ".BAK");
    file = fopen(temp, "wb");
    if (!file) {
        free(body);
        return set_error(error, error_size, "cannot create temporary level");
    }
    write_level_header(file, level, crc);
    if (fwrite(body, 1, (unsigned)body_size, file) != (unsigned)body_size ||
        fclose(file)) {
        free(body);
        remove(temp);
        return set_error(error, error_size, "failed writing temporary level");
    }
    free(body);

    if (!level_load(&check, temp, error, error_size)) {
        remove(temp);
        return 0;
    }
    level_free(&check);
    remove(backup);
    had_original = rename(path, backup) == 0;
    if (rename(temp, path)) {
        if (had_original) rename(backup, path);
        remove(temp);
        return set_error(error, error_size, "cannot replace level file");
    }
    if (had_original) remove(backup);
    return 1;
}

/* Walks one sprite's run-length rows, checking every run stays inside both the
 * declared stream and a row of `row_width` pixels. Advances `cursor` past it. */
static int check_span_rows(KoloConstFarPtr *cursor, KoloConstFarPtr span_end,
                           unsigned row_width, char *error, unsigned error_size,
                           const char *what)
{
    unsigned row;
    for (row = 0; row < SPRITE_ROWS; ++row) {
        unsigned run, run_count;
        if (*cursor >= span_end) return set_error(error, error_size, what);
        run_count = *(*cursor)++;
        for (run = 0; run < run_count; ++run) {
            unsigned start, length;
            if ((u32)(span_end - *cursor) < 2) return set_error(error, error_size, what);
            start = *(*cursor)++;
            length = *(*cursor)++;
            if (!length || start + length > row_width ||
                (u32)(span_end - *cursor) < length)
                return set_error(error, error_size, what);
            *cursor += length;
        }
    }
    return 1;
}

static int parse_bank_header(AssetPack *pack, KoloConstFarPtr *cursor,
                             KoloConstFarPtr end, char *error, unsigned error_size)
{
    KoloConstFarPtr p = *cursor;
    u16 version = far_read_u16_at(p);
    u16 tile_w, tile_h;
    p += 2;
    pack->theme = far_read_u16_at(p);
    p += 2;
    pack->tile_count = far_read_u16_at(p);
    p += 2;
    pack->sprite_count = far_read_u16_at(p);
    p += 2;
    tile_w = far_read_u16_at(p);
    p += 2;
    tile_h = far_read_u16_at(p);
    p += 2;
    if (version != BANK_VERSION || tile_w != KOLO_TILE_SIZE || tile_h != KOLO_TILE_SIZE ||
        pack->tile_count == 0 || pack->tile_count > BANK_MAX_TILES ||
        pack->sprite_count == 0 || pack->sprite_count > KOLO_MAX_SPRITES)
        return set_error(error, error_size, "unsupported resource bank metadata");
    if ((u32)(end - p) < BANK_PALETTE_SIZE +
                         (u32)pack->tile_count * (TILE_PIXELS + 2UL) + 4UL)
        return set_error(error, error_size, "truncated resource bank");
    pack->palette = (KoloFarPtr)p;
    p += BANK_PALETTE_SIZE;
    pack->tiles = (KoloFarPtr)p;
    p += (u32)pack->tile_count * TILE_PIXELS;
    pack->tile_flags = (KoloFarPtr)p;
    p += pack->tile_count;
    pack->tile_material = (KoloFarPtr)p;
    p += pack->tile_count;
    *cursor = p;
    return 1;
}

static int parse_bank(AssetPack *pack, char *error, unsigned error_size)
{
    KoloConstFarPtr p, end, span_end;
    u16 span_size;
    unsigned i, variant;
    if (pack->blob_size < BANK_MIN_SIZE ||
        !far_equal(pack->blob, BANK_SIGNATURE, BANK_SIGNATURE_SIZE))
        return set_error(error, error_size, "bad resource bank signature");
    if (assets_crc32(pack->blob, pack->blob_size - 4) !=
        far_read_u32(pack->blob + pack->blob_size - 4))
        return set_error(error, error_size, "resource bank checksum mismatch");
    p = pack->blob + BANK_SIGNATURE_SIZE;
    end = pack->blob + pack->blob_size - 4;
    if (!parse_bank_header(pack, &p, end, error, error_size)) return 0;

    /* Unpacked spans, indexed by sprite. */
    span_size = far_read_u16_at(p);
    p += 2;
    if ((u32)(end - p) < (u32)span_size + 2UL)
        return set_error(error, error_size, "truncated sprite spans");
    span_end = p + span_size;
    for (i = 0; i < pack->sprite_count; ++i) {
        pack->sprite_spans[i] = (KoloFarPtr)p;
        if (!check_span_rows(&p, span_end, KOLO_TILE_SIZE, error, error_size,
                             "invalid sprite span")) return 0;
    }
    if (p != span_end)
        return set_error(error, error_size, "unexpected sprite span size");

    /* Mode X spans, pre-shifted per sub-pixel alignment and plane, so a sprite can
     * be blitted one plane at a time without shifting at run time. */
    span_size = far_read_u16_at(p);
    p += 2;
    if ((u32)(end - p) != (u32)span_size)
        return set_error(error, error_size, "unexpected planar span size");
    span_end = p + span_size;
    for (i = 0; i < pack->sprite_count; ++i)
        for (variant = 0; variant < SPRITE_PLANE_VARIANTS; ++variant) {
            pack->sprite_planar_spans[i][variant] = (KoloFarPtr)p;
            if (!check_span_rows(&p, span_end, PLANAR_ROW_WIDTH, error, error_size,
                                 "invalid planar span")) return 0;
        }
    return p == span_end ? 1
                         : set_error(error, error_size, "unexpected planar span data");
}

/* The bank lives above the 64 KiB small-model data segment, so on the DOS target
 * it is staged through a near buffer into a far allocation. */
static int read_bank_blob(AssetPack *pack, FILE *file, u32 size,
                          char *error, unsigned error_size)
{
#ifdef __WATCOMC__
    u8 buffer[1024];
    u32 done = 0;
    unsigned chunk, segment;
    if (_dos_allocmem((unsigned)((size + 15UL) >> 4), &segment) != 0)
        return set_error(error, error_size, "not enough far memory for resource bank");
    pack->bank_segment = (u16)segment;
    pack->blob = (KoloFarPtr)MK_FP(pack->bank_segment, 0);
    while (done < size) {
        chunk = (unsigned)(size - done > sizeof(buffer) ? sizeof(buffer) : size - done);
        if (fread(buffer, 1, chunk, file) != chunk)
            return set_error(error, error_size, "short read from resource bank");
        _fmemcpy(pack->blob + done, buffer, chunk);
        done += chunk;
    }
#else
    pack->blob = (u8 *)malloc((unsigned)size);
    if (!pack->blob)
        return set_error(error, error_size, "not enough memory for resource bank");
    if (fread(pack->blob, 1, (unsigned)size, file) != (unsigned)size)
        return set_error(error, error_size, "short read from resource bank");
#endif
    pack->blob_size = size;
    return 1;
}

static int find_bank(FILE *file, const char *bank_name, u32 *offset, u32 *size,
                     char *error, unsigned error_size)
{
    u8 header[DAT_HEADER_SIZE], entry[DAT_ENTRY_SIZE];
    char wanted[DAT_NAME_SIZE + 1];
    u16 count, i;
    memset(wanted, 0, sizeof(wanted));
    strncpy(wanted, bank_name, DAT_NAME_SIZE);
    *offset = *size = 0;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, DAT_SIGNATURE, DAT_NAME_SIZE))
        return set_error(error, error_size, "unsupported KOLOBOK.DAT format");
    if ((header[8] | ((u16)header[9] << 8)) != DAT_VERSION)
        return set_error(error, error_size, "unsupported archive version");
    count = (u16)(header[10] | ((u16)header[11] << 8));
    if (!count || count > DAT_MAX_BANKS)
        return set_error(error, error_size, "invalid archive bank count");
    for (i = 0; i < count; ++i) {
        if (fread(entry, 1, sizeof(entry), file) != sizeof(entry))
            return set_error(error, error_size, "truncated archive index");
        if (!strncmp((char *)entry, wanted, DAT_NAME_SIZE)) {
            *offset = read_u32_at(entry + 8);
            *size = read_u32_at(entry + 12);
        }
    }
    if (!*offset || *size < BANK_MIN_SIZE || *size >= BANK_MAX_SIZE)
        return set_error(error, error_size, "resource bank missing or too large");
    return 1;
}

int assets_load_bank(AssetPack *pack, const char *archive_path, const char *bank_name,
                     const char *level_path, char *error, unsigned error_size)
{
    FILE *file;
    u32 offset, size;
    memset(pack, 0, sizeof(*pack));
    if (level_path && !level_load(&pack->level, level_path, error, error_size)) return 0;
    file = fopen(archive_path, "rb");
    if (!file) {
        assets_free(pack);
        return set_error(error, error_size, "cannot open KOLOBOK.DAT");
    }
    if (!find_bank(file, bank_name, &offset, &size, error, error_size)) {
        fclose(file);
        assets_free(pack);
        return 0;
    }
    if (fseek(file, (long)offset, SEEK_SET)) {
        fclose(file);
        assets_free(pack);
        return set_error(error, error_size, "cannot seek resource bank");
    }
    if (!read_bank_blob(pack, file, size, error, error_size)) {
        fclose(file);
        assets_free(pack);
        return 0;
    }
    fclose(file);
    if (!parse_bank(pack, error, error_size)) {
        assets_free(pack);
        return 0;
    }
    return 1;
}

void assets_free(AssetPack *pack)
{
    level_free(&pack->level);
#ifdef __WATCOMC__
    if (pack->bank_segment) _dos_freemem(pack->bank_segment);
#else
    if (pack->blob) free(pack->blob);
#endif
    memset(pack, 0, sizeof(*pack));
}

int assets_far_memory_active(const AssetPack *pack)
{
#ifdef __WATCOMC__
    return pack->blob != 0 && pack->bank_segment != 0 && FP_OFF(pack->blob) == 0;
#else
    return pack->blob != 0;
#endif
}
