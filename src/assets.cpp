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

/* The loaders leave by half a dozen failure branches each; releasing on the way
 * out of scope leaves a branch holding nothing but its message. Not copyable,
 * because two owners would release twice. */
class File {
public:
    File(const char *path, const char *mode) { handle = fopen(path, mode); }
    ~File() { if (handle != NULL) fclose(handle); }
    bool ok() const { return handle != NULL; }
    FILE *get() { return handle; }
    /* For a write the flush can fail, so closing early is a checked step. */
    bool close() { FILE *closing = handle; handle = NULL; return fclose(closing) == 0; }
private:
    FILE *handle;
    File(const File &);
    File &operator=(const File &);
};

class Bytes {
public:
    Bytes() : p(NULL) { }
    ~Bytes() { if (p != NULL) free(p); }
    bool alloc(u32 size) { p = (u8 *)malloc((unsigned)size); return p != NULL; }
    u8 *get() { return p; }
    u8 *release() { u8 *owned = p; p = NULL; return owned; }
private:
    u8 *p;
    Bytes(const Bytes &);
    Bytes &operator=(const Bytes &);
};

/* A stage that fails has to give back what the earlier ones took; keep() is the
 * one exit that does not. */
class PackGuard {
public:
    PackGuard(AssetPack *p) : pack(p) { }
    ~PackGuard() { if (pack != NULL) assets_free(pack); }
    void keep() { pack = NULL; }
private:
    AssetPack *pack;
    PackGuard(const PackGuard &);
    PackGuard &operator=(const PackGuard &);
};

/* Cursors over a little-endian byte stream. field() is what lets one record
 * description serve both: the reader fills a mutable field, the writer emits a
 * const one. */
class Reader {
public:
    Reader(const u8 *start) : p(start) { }
    const u8 *at() const { return p; }
    void skip(unsigned count) { p += count; }
    u8 byte() { return *p++; }
    u16 word() { u16 v = (u16)(p[0] | ((u16)p[1] << 8)); p += 2; return v; }
    u32 dword() { u32 low = word(); return low | ((u32)word() << 16); }
    void field(u8 &value) { value = byte(); }
    void field(u16 &value) { value = word(); }
    void field(u32 &value) { value = dword(); }
    void field(Point &value) { value.x = word(); value.y = word(); }
    /* A tree's row and height share one 16-bit field, low byte first. */
    void packed(u16 &low, u8 &high) { u16 v = word(); low = (u8)v; high = (u8)(v >> 8); }
private:
    const u8 *p;
};

class Writer {
public:
    Writer(u8 *start) : p(start) { }
    void byte(u8 value) { *p++ = value; }
    void word(u16 value) { *p++ = (u8)value; *p++ = (u8)(value >> 8); }
    void dword(u32 value) { word((u16)value); word((u16)(value >> 16)); }
    void field(const u8 &value) { byte(value); }
    void field(const u16 &value) { word(value); }
    void field(const u32 &value) { dword(value); }
    void field(const Point &value) { word(value.x); word(value.y); }
    void packed(const u16 &low, const u8 &high) { word((u16)(low | ((u16)high << 8))); }
    void bytes(const void *source, unsigned count)
    {
        memcpy(p, source, count);
        p += count;
    }
private:
    u8 *p;
};

u32 assets_crc32(ConstFarPtr data, u32 length)
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

bool Error::fail(const char *message)
{
    strncpy(text, message, TEXT_SIZE - 1);
    text[TEXT_SIZE - 1] = 0;
    return false;
}

static u16 far_read_u16_at(ConstFarPtr cursor)
{
    return (u16)(cursor[0] | ((u16)cursor[1] << 8));
}

static u32 far_read_u32(ConstFarPtr p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static bool far_equal(ConstFarPtr p, const char *text, unsigned length)
{
    unsigned i;
    for (i = 0; i < length; ++i)
        if (p[i] != (u8)text[i]) return false;
    return true;
}

unsigned assets_property_field_count(unsigned kind)
{
    if (kind == PropertyKind::PICKUP) return PickupField::COUNT;
    if (kind == PropertyKind::ANIMAL) return AnimalField::COUNT;
    if (kind == PropertyKind::TREE) return TreeField::COUNT;
    return LevelField::COUNT;
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

static bool level_counts_in_range(const LevelData *level)
{
    return level->width >= KLV_MIN_WIDTH && level->width <= KLV_MAX_WIDTH &&
           level->height == LEVEL_HEIGHT &&
           level->checkpoint_count <= MAX_CHECKPOINTS &&
           level->pickup_count <= MAX_PICKUPS &&
           level->animal_count <= MAX_ENEMIES &&
           level->tree_count <= MAX_TREES &&
           level->encounter_count <= MAX_ENCOUNTERS;
}

static bool validate_markers(const LevelData *level, Error &error)
{
    unsigned i;
    if (level->start.x >= level->width || level->start.y >= level->height ||
        level->exit.x >= level->width || level->exit.y >= level->height ||
        level->home.x >= level->width || level->home.y >= level->height)
        return error.fail("level marker is outside map");
    for (i = 0; i < level->checkpoint_count; ++i)
        if (level->checkpoints[i].x >= level->width ||
            level->checkpoints[i].y >= level->height)
            return error.fail("checkpoint is outside map");
    return true;
}

static bool validate_pickups(const LevelData *level, Error &error)
{
    unsigned i, j, red = 0;
    for (i = 0; i < level->pickup_count; ++i) {
        const Pickup *pickup = &level->pickups[i];
        if (pickup->type > PickupType::BIG_PIE ||
            pickup->x >= level->width || pickup->y >= level->height)
            return error.fail("invalid pickup record");
        if (pickup->type == PickupType::RED) ++red;
        for (j = 0; j < i; ++j)
            if (level->pickups[j].id == pickup->id)
                return error.fail("duplicate pickup ID");
    }
    if (red < level->required_red)
        return error.fail("not enough red berries");
    return true;
}

static bool validate_animals(const LevelData *level, Error &error)
{
    unsigned i, j;
    for (i = 0; i < level->animal_count; ++i) {
        const AnimalSpawn *animal = &level->animals[i];
        if (animal->type > AnimalType::BEAR ||
            animal->x >= level->width || animal->y >= level->height ||
            animal->min_x > animal->x || animal->max_x < animal->x ||
            animal->max_x >= level->width)
            return error.fail("invalid animal record");
        /* update_bear treats both climb rows as absolute tile rows and drives the
         * bear to them without clamping, so an out-of-range pair walks it off the
         * map. The editor clamps as you type; a hand-written level does not. */
        if (animal->climb_min > animal->climb_max ||
            animal->climb_max >= level->height)
            return error.fail("invalid animal climb range");
        if (animal->dialogue_id != NO_ID && animal->dialogue_id > MAX_DIALOGUE_ID)
            return error.fail("animal dialogue ID out of range");
        for (j = 0; j < i; ++j)
            if (level->animals[j].id == animal->id)
                return error.fail("duplicate animal ID");
    }
    return true;
}

static bool validate_trees(const LevelData *level, Error &error)
{
    unsigned i, j;
    for (i = 0; i < level->tree_count; ++i) {
        const Tree *tree = &level->trees[i];
        if (tree->type > TreeType::OAK || tree->x >= level->width ||
            tree->y >= level->height || !tree->height)
            return error.fail("invalid tree record");
        for (j = 0; j < i; ++j)
            if (level->trees[j].id == tree->id)
                return error.fail("duplicate tree ID");
    }
    return true;
}

static bool validate_cross_references(const LevelData *level, Error &error)
{
    unsigned i, j, required = 0;
    for (i = 0; i < level->animal_count; ++i)
        if (level->animals[i].tree_id != NO_ID &&
            !find_by_id(level->trees, level->tree_count, level->animals[i].tree_id))
            return error.fail("animal refers to missing tree");
    for (i = 0; i < level->encounter_count; ++i) {
        const Encounter *encounter = &level->encounters[i];
        if (!find_by_id(level->animals, level->animal_count, encounter->animal_id))
            return error.fail("encounter refers to missing animal");
        if (encounter->correct >= ANSWER_COUNT || encounter->reward > Reward::SMALL_PIE)
            return error.fail("invalid encounter record");
        for (j = 0; j < i; ++j)
            if (level->encounters[j].id == encounter->id)
                return error.fail("duplicate encounter ID");
        if (encounter->required) ++required;
    }
    /* Exactly one guardian: game_exit_ready gates the exit on the required
     * encounter, so zero makes the level unfinishable and two is ambiguous. */
    if (required != 1)
        return error.fail("level needs exactly one guardian");
    return true;
}

bool level_validate(const LevelData *level, Error &error)
{
    u32 i;
    if (level->width < KLV_MIN_WIDTH || level->width > KLV_MAX_WIDTH ||
        level->height != LEVEL_HEIGHT)
        return error.fail("level must be 32..256 by 11 tiles");
    if (level->theme >= Theme::COUNT || level->map == NULL)
        return error.fail("invalid level theme or tile map");
    if (!level_counts_in_range(level))
        return error.fail("level object limit exceeded");
    if (!validate_markers(level, error)) return false;
    for (i = 0; i < level_map_bytes(level); ++i)
        if (level->map[i] >= Tile::COUNT)
            return error.fail("unknown tile in level");
    if (!validate_pickups(level, error)) return false;
    if (!validate_animals(level, error)) return false;
    if (!validate_trees(level, error)) return false;
    return validate_cross_references(level, error);
}

/* The KLV record layouts, each stated once for both directions: a Reader and a
 * record to fill it, a Writer and a const record to emit it. */
template <class S, class R> static void visit_pickup(S &s, R &pickup)
{
    s.field(pickup.type);
    s.field(pickup.flags);
    s.field(pickup.id);
    s.field(pickup.x);
    s.field(pickup.y);
}

template <class S, class R> static void visit_animal(S &s, R &animal)
{
    s.field(animal.type);
    s.field(animal.flags);
    s.field(animal.id);
    s.field(animal.x);
    s.field(animal.y);
    s.field(animal.min_x);
    s.field(animal.max_x);
    s.field(animal.tree_id);
    s.field(animal.climb_min);
    s.field(animal.climb_max);
    s.field(animal.dialogue_id);
}

template <class S, class R> static void visit_tree(S &s, R &tree)
{
    s.field(tree.type);
    s.field(tree.flags);
    s.field(tree.id);
    s.field(tree.x);
    s.packed(tree.y, tree.height);
}

template <class S, class R> static void visit_encounter(S &s, R &encounter)
{
    s.field(encounter.id);
    s.field(encounter.animal_id);
    s.field(encounter.dialogue_id);
    s.field(encounter.required);
    s.field(encounter.correct);
    s.field(encounter.reward);
    s.field(encounter.retry_frames);
}

/* The markers and the object runs, in the order the body stores them. */
template <class S, class L> static void visit_body_records(S &s, L &level)
{
    unsigned i;
    s.field(level.start);
    s.field(level.exit);
    s.field(level.home);
    for (i = 0; i < level.checkpoint_count; ++i) s.field(level.checkpoints[i]);
    for (i = 0; i < level.pickup_count; ++i) visit_pickup(s, level.pickups[i]);
    for (i = 0; i < level.animal_count; ++i) visit_animal(s, level.animals[i]);
    for (i = 0; i < level.tree_count; ++i) visit_tree(s, level.trees[i]);
    for (i = 0; i < level.encounter_count; ++i) visit_encounter(s, level.encounters[i]);
}

static bool read_level_file(const char *path, Bytes &blob, u32 *size, Error &error)
{
    File file(path, "rb");
    long measured;
    if (!file.ok()) return error.fail("cannot open KLV level");
    if (fseek(file.get(), 0, SEEK_END) ||
        (measured = ftell(file.get())) < KLV_HEADER_SIZE ||
        measured > KLV_MAX_SIZE || fseek(file.get(), 0, SEEK_SET))
        return error.fail("cannot measure KLV level");
    if (!blob.alloc((u32)measured)) return error.fail("not enough memory for level");
    if (fread(blob.get(), 1, (unsigned)measured, file.get()) != (unsigned)measured)
        return error.fail("short read from KLV level");
    *size = (u32)measured;
    return true;
}

/* The metadata half of the header, past the signature, version and checksum. */
template <class S, class L> static void visit_level_metadata(S &s, L &level)
{
    s.field(level.width);
    s.field(level.height);
    s.field(level.theme);
    s.field(level.required_red);
    s.field(level.cloud_seed);
    s.field(level.checkpoint_count);
    s.field(level.pickup_count);
    s.field(level.animal_count);
    s.field(level.tree_count);
    s.field(level.encounter_count);
}

static bool read_level_header(LevelData *level, const u8 *blob, u32 size,
                              Error &error)
{
    Reader r(blob);
    u16 version, header_size;
    u32 crc;
    if (memcmp(blob, KLV_SIGNATURE, 4))
        return error.fail("unsupported KLV signature");
    r.skip(4);
    version = r.word();
    header_size = r.word();
    crc = r.dword();
    if (version != KLV_VERSION || header_size != KLV_HEADER_SIZE)
        return error.fail("unsupported KLV version");
    if (assets_crc32(blob + KLV_HEADER_SIZE, size - KLV_HEADER_SIZE) != crc)
        return error.fail("KLV checksum mismatch");
    visit_level_metadata(r, *level);
    if (!level_counts_in_range(level))
        return error.fail("invalid KLV metadata");
    return true;
}

static void write_level_header(Writer &w, const LevelData *level, u32 crc)
{
    w.bytes(KLV_SIGNATURE, 4);
    w.word(KLV_VERSION);
    w.word(KLV_HEADER_SIZE);
    w.dword(crc);
    visit_level_metadata(w, *level);
}

/* The tile map stays local until the whole payload parses, so a level that fails
 * half way through hands the caller nothing to release. */
bool level_load(LevelData *level, const char *path, Error &error)
{
    Bytes blob, map;
    const u8 *body, *end;
    u32 size, map_bytes;
    memset(level, 0, sizeof(*level));
    if (!read_level_file(path, blob, &size, error)) return false;
    if (!read_level_header(level, blob.get(), size, error)) {
        memset(level, 0, sizeof(*level));
        return false;
    }
    body = blob.get() + KLV_HEADER_SIZE;
    end = blob.get() + size;
    map_bytes = level_map_bytes(level);
    if ((u32)(end - body) < level_body_bytes(level)) {
        memset(level, 0, sizeof(*level));
        return error.fail("truncated KLV payload");
    }
    if (!map.alloc(map_bytes)) return error.fail("not enough memory for tile map");
    memcpy(map.get(), body, (unsigned)map_bytes);
    Reader r(body + map_bytes);
    visit_body_records(r, *level);
    if (r.at() != end) {
        memset(level, 0, sizeof(*level));
        return error.fail("unexpected KLV payload size");
    }
    level->map = map.release();
    if (!level_validate(level, error)) {
        level_free(level);
        return false;
    }
    return true;
}

/* Body first: the header carries a CRC of everything after it. */
static bool build_level_file(const LevelData *level, Bytes &out, u32 body_size)
{
    u32 map_bytes = level_map_bytes(level);
    if (!out.alloc(KLV_HEADER_SIZE + body_size)) return false;
    Writer body(out.get() + KLV_HEADER_SIZE);
    body.bytes(level->map, (unsigned)map_bytes);
    visit_body_records(body, *level);
    Writer header(out.get());
    write_level_header(header, level,
                      assets_crc32(out.get() + KLV_HEADER_SIZE, body_size));
    return true;
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
bool level_save(const LevelData *level, const char *path, Error &error)
{
    char temp[PATH_MAX_LEN], backup[PATH_MAX_LEN];
    LevelData check;
    Bytes contents;
    u32 total;
    bool had_original;
    if (!level_validate(level, error)) return false;
    if (strlen(path) + 5 >= sizeof(temp))
        return error.fail("level filename is too long");
    total = KLV_HEADER_SIZE + level_body_bytes(level);
    if (!build_level_file(level, contents, level_body_bytes(level)))
        return error.fail("not enough memory to save level");

    with_extension(temp, path, ".TMP");
    with_extension(backup, path, ".BAK");
    {
        File file(temp, "wb");
        if (!file.ok()) return error.fail("cannot create temporary level");
        if (fwrite(contents.get(), 1, (unsigned)total, file.get()) !=
            (unsigned)total || !file.close()) {
            remove(temp);
            return error.fail("failed writing temporary level");
        }
    }

    if (!level_load(&check, temp, error)) {
        remove(temp);
        return false;
    }
    level_free(&check);
    remove(backup);
    had_original = rename(path, backup) == 0;
    if (rename(temp, path)) {
        if (had_original) rename(backup, path);
        remove(temp);
        return error.fail("cannot replace level file");
    }
    if (had_original) remove(backup);
    return true;
}

/* Walks one sprite's run-length rows, checking every run stays inside both the
 * declared stream and a row of `row_width` pixels. Advances `cursor` past it. */
static bool check_span_rows(ConstFarPtr *cursor, ConstFarPtr span_end,
                           unsigned row_width, Error &error,
                           const char *what)
{
    unsigned row;
    for (row = 0; row < SPRITE_ROWS; ++row) {
        unsigned run, run_count;
        if (*cursor >= span_end) return error.fail(what);
        run_count = *(*cursor)++;
        for (run = 0; run < run_count; ++run) {
            unsigned start, length;
            if ((u32)(span_end - *cursor) < 2) return error.fail(what);
            start = *(*cursor)++;
            length = *(*cursor)++;
            if (!length || start + length > row_width ||
                (u32)(span_end - *cursor) < length)
                return error.fail(what);
            *cursor += length;
        }
    }
    return true;
}

static bool parse_bank_header(AssetPack *pack, ConstFarPtr *cursor,
                             ConstFarPtr end, Error &error)
{
    ConstFarPtr p = *cursor;
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
    if (version != BANK_VERSION || tile_w != TILE_SIZE || tile_h != TILE_SIZE ||
        pack->tile_count == 0 || pack->tile_count > BANK_MAX_TILES ||
        pack->sprite_count == 0 || pack->sprite_count > MAX_SPRITES)
        return error.fail("unsupported resource bank metadata");
    if ((u32)(end - p) < BANK_PALETTE_SIZE +
                         (u32)pack->tile_count * (TILE_PIXELS + 2UL) + 4UL)
        return error.fail("truncated resource bank");
    pack->palette = (FarPtr)p;
    p += BANK_PALETTE_SIZE;
    pack->tiles = (FarPtr)p;
    p += (u32)pack->tile_count * TILE_PIXELS;
    pack->tile_flags = (FarPtr)p;
    p += pack->tile_count;
    pack->tile_material = (FarPtr)p;
    p += pack->tile_count;
    *cursor = p;
    return true;
}

static bool parse_bank(AssetPack *pack, Error &error)
{
    ConstFarPtr p, end, span_end;
    u16 span_size;
    unsigned i, variant;
    if (pack->blob_size < BANK_MIN_SIZE ||
        !far_equal(pack->blob, BANK_SIGNATURE, BANK_SIGNATURE_SIZE))
        return error.fail("bad resource bank signature");
    if (assets_crc32(pack->blob, pack->blob_size - 4) !=
        far_read_u32(pack->blob + pack->blob_size - 4))
        return error.fail("resource bank checksum mismatch");
    p = pack->blob + BANK_SIGNATURE_SIZE;
    end = pack->blob + pack->blob_size - 4;
    if (!parse_bank_header(pack, &p, end, error)) return false;

    /* Unpacked spans, indexed by sprite. */
    span_size = far_read_u16_at(p);
    p += 2;
    if ((u32)(end - p) < (u32)span_size + 2UL)
        return error.fail("truncated sprite spans");
    span_end = p + span_size;
    for (i = 0; i < pack->sprite_count; ++i) {
        pack->sprite_spans[i] = (FarPtr)p;
        if (!check_span_rows(&p, span_end, TILE_SIZE, error,
                             "invalid sprite span")) return false;
    }
    if (p != span_end)
        return error.fail("unexpected sprite span size");

    /* Mode X spans, pre-shifted per sub-pixel alignment and plane, so a sprite can
     * be blitted one plane at a time without shifting at run time. */
    span_size = far_read_u16_at(p);
    p += 2;
    if ((u32)(end - p) != (u32)span_size)
        return error.fail("unexpected planar span size");
    span_end = p + span_size;
    for (i = 0; i < pack->sprite_count; ++i)
        for (variant = 0; variant < SPRITE_PLANE_VARIANTS; ++variant) {
            pack->sprite_planar_spans[i][variant] = (FarPtr)p;
            if (!check_span_rows(&p, span_end, PLANAR_ROW_WIDTH, error,
                                 "invalid planar span")) return false;
        }
    return p == span_end ? true
                         : error.fail("unexpected planar span data");
}

/* The bank lives above the 64 KiB small-model data segment, so on the DOS target
 * it is staged through a near buffer into a far allocation. */
static bool read_bank_blob(AssetPack *pack, FILE *file, u32 size,
                          Error &error)
{
#ifdef __WATCOMC__
    u8 buffer[1024];
    u32 done = 0;
    unsigned chunk, segment;
    if (_dos_allocmem((unsigned)((size + 15UL) >> 4), &segment) != 0)
        return error.fail("not enough far memory for resource bank");
    pack->bank_segment = (u16)segment;
    pack->blob = (FarPtr)MK_FP(pack->bank_segment, 0);
    while (done < size) {
        chunk = (unsigned)(size - done > sizeof(buffer) ? sizeof(buffer) : size - done);
        if (fread(buffer, 1, chunk, file) != chunk)
            return error.fail("short read from resource bank");
        _fmemcpy(pack->blob + done, buffer, chunk);
        done += chunk;
    }
#else
    pack->blob = (u8 *)malloc((unsigned)size);
    if (!pack->blob)
        return error.fail("not enough memory for resource bank");
    if (fread(pack->blob, 1, (unsigned)size, file) != (unsigned)size)
        return error.fail("short read from resource bank");
#endif
    pack->blob_size = size;
    return true;
}

static bool find_bank(FILE *file, const char *bank_name, u32 *offset, u32 *size,
                      Error &error)
{
    u8 header[DAT_HEADER_SIZE], entry[DAT_ENTRY_SIZE];
    char wanted[DAT_NAME_SIZE + 1];
    u16 count, i;
    memset(wanted, 0, sizeof(wanted));
    strncpy(wanted, bank_name, DAT_NAME_SIZE);
    *offset = *size = 0;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, DAT_SIGNATURE, DAT_NAME_SIZE))
        return error.fail("unsupported KOLOBOK.DAT format");
    Reader index(header + DAT_NAME_SIZE);
    if (index.word() != DAT_VERSION)
        return error.fail("unsupported archive version");
    count = index.word();
    if (!count || count > DAT_MAX_BANKS)
        return error.fail("invalid archive bank count");
    for (i = 0; i < count; ++i) {
        if (fread(entry, 1, sizeof(entry), file) != sizeof(entry))
            return error.fail("truncated archive index");
        if (!strncmp((char *)entry, wanted, DAT_NAME_SIZE)) {
            Reader located(entry + DAT_NAME_SIZE);
            *offset = located.dword();
            *size = located.dword();
        }
    }
    if (!*offset || *size < BANK_MIN_SIZE || *size >= BANK_MAX_SIZE)
        return error.fail("resource bank missing or too large");
    return true;
}

bool assets_load_bank(AssetPack *pack, const char *archive_path, const char *bank_name,
                     const char *level_path, Error &error)
{
    u32 offset, size;
    memset(pack, 0, sizeof(*pack));
    PackGuard guard(pack);
    if (level_path && !level_load(&pack->level, level_path, error)) return false;
    {
        File file(archive_path, "rb");
        if (!file.ok()) return error.fail("cannot open KOLOBOK.DAT");
        if (!find_bank(file.get(), bank_name, &offset, &size, error)) return false;
        if (fseek(file.get(), (long)offset, SEEK_SET))
            return error.fail("cannot seek resource bank");
        if (!read_bank_blob(pack, file.get(), size, error)) return false;
    }
    if (!parse_bank(pack, error)) return false;
    guard.keep();
    return true;
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

bool assets_far_memory_active(const AssetPack *pack)
{
#ifdef __WATCOMC__
    return pack->blob != 0 && pack->bank_segment != 0 && FP_OFF(pack->blob) == 0;
#else
    return pack->blob != 0;
#endif
}
