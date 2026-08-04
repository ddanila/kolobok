#include "assets.h"

#include <stdlib.h>
#include <string.h>

static u16 read_u16(const u8 **cursor)
{
    u16 value = (u16)((*cursor)[0] | ((u16)(*cursor)[1] << 8));
    *cursor += 2;
    return value;
}

static u32 read_u32(const u8 *cursor)
{
    return (u32)cursor[0] | ((u32)cursor[1] << 8) |
           ((u32)cursor[2] << 16) | ((u32)cursor[3] << 24);
}

u32 assets_crc32(const u8 *data, u32 length)
{
    u32 crc = 0xffffffffUL;
    u32 i;
    int bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
    return crc ^ 0xffffffffUL;
}

static int fail(AssetPack *pack, char *error, unsigned error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        strncpy(error, message, error_size - 1);
        error[error_size - 1] = '\0';
    }
    assets_free(pack);
    return 0;
}

int assets_load(AssetPack *pack, const char *path, char *error, unsigned error_size)
{
    FILE *file;
    long size;
    const u8 *cursor;
    const u8 *end;
    u16 version, tile_w, tile_h;
    u32 expected_crc, required;
    u16 span_blob_size, planar_span_blob_size;
    unsigned i;

    memset(pack, 0, sizeof(*pack));
    file = fopen(path, "rb");
    if (file == NULL)
        return fail(pack, error, error_size, "cannot open KOLOBOK.DAT");
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 32 ||
        size > 65520L ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail(pack, error, error_size, "cannot measure KOLOBOK.DAT");
    }
    pack->blob = (u8 *)malloc((unsigned)size);
    if (pack->blob == NULL) {
        fclose(file);
        return fail(pack, error, error_size, "not enough conventional memory for assets");
    }
    pack->blob_size = (u32)size;
    if (fread(pack->blob, 1, (unsigned)size, file) != (unsigned)size) {
        fclose(file);
        return fail(pack, error, error_size, "short read from KOLOBOK.DAT");
    }
    fclose(file);
    if (memcmp(pack->blob, "KOLODAT1", 8) != 0)
        return fail(pack, error, error_size, "bad asset signature");
    expected_crc = read_u32(pack->blob + size - 4);
    if (assets_crc32(pack->blob, (u32)size - 4) != expected_crc)
        return fail(pack, error, error_size, "asset checksum mismatch");

    cursor = pack->blob + 8;
    end = pack->blob + size - 4;
    version = read_u16(&cursor);
    pack->map_w = read_u16(&cursor);
    pack->map_h = read_u16(&cursor);
    pack->tile_count = read_u16(&cursor);
    pack->sprite_count = read_u16(&cursor);
    pack->berry_count = read_u16(&cursor);
    pack->enemy_count = read_u16(&cursor);
    tile_w = read_u16(&cursor);
    tile_h = read_u16(&cursor);
    if (version != 3 || tile_w != 16 || tile_h != 16 ||
        pack->map_w == 0 || pack->map_w > 256 ||
        pack->map_h == 0 || pack->map_h > 256 ||
        pack->tile_count == 0 || pack->tile_count > 64 ||
        pack->sprite_count == 0 || pack->sprite_count > KOLO_MAX_SPRITES ||
        pack->berry_count > KOLO_MAX_BERRIES || pack->enemy_count > KOLO_MAX_ENEMIES)
        return fail(pack, error, error_size, "unsupported asset metadata");
    required = 768UL + (u32)pack->tile_count * 256UL + 2UL;
    if ((u32)(end - cursor) < required)
        return fail(pack, error, error_size, "truncated asset data");
    pack->palette = (u8 *)cursor; cursor += 768;
    pack->tiles = (u8 *)cursor; cursor += (u32)pack->tile_count * 256UL;
    span_blob_size = read_u16(&cursor);
    required += (u32)span_blob_size + 2UL;
    if ((u32)(end - cursor) < (u32)span_blob_size + 2UL)
        return fail(pack, error, error_size, "truncated sprite span data");
    {
        const u8 *generic_end = cursor + span_blob_size;
        const u8 *scan = generic_end;
        planar_span_blob_size = (u16)(scan[0] | ((u16)scan[1] << 8));
    }
    required += (u32)planar_span_blob_size + (u32)pack->map_w * pack->map_h +
        (u32)pack->berry_count * 4UL + (u32)pack->enemy_count * 9UL + 8UL;
    if ((u32)(end - (pack->blob + 26)) != required)
        return fail(pack, error, error_size, "unexpected asset payload size");
    {
        const u8 *span_end = cursor + span_blob_size;
        for (i = 0; i < pack->sprite_count; ++i) {
            unsigned row;
            pack->sprite_spans[i] = (u8 *)cursor;
            for (row = 0; row < 16; ++row) {
                unsigned run, run_count;
                if (cursor >= span_end)
                    return fail(pack, error, error_size, "truncated sprite spans");
                run_count = *cursor++;
                for (run = 0; run < run_count; ++run) {
                    unsigned x, length;
                    if ((u32)(span_end - cursor) < 2UL)
                        return fail(pack, error, error_size, "truncated sprite span");
                    x = *cursor++; length = *cursor++;
                    if (length == 0 || x + length > 16 ||
                        (u32)(span_end - cursor) < length)
                        return fail(pack, error, error_size, "invalid sprite span");
                    cursor += length;
                }
            }
        }
        if (cursor != span_end)
            return fail(pack, error, error_size, "unexpected sprite span size");
    }
    planar_span_blob_size = read_u16(&cursor);
    {
        const u8 *span_end = cursor + planar_span_blob_size;
        for (i = 0; i < pack->sprite_count; ++i) {
            unsigned variant;
            for (variant = 0; variant < 16; ++variant) {
                unsigned row;
                pack->sprite_planar_spans[i][variant] = (u8 *)cursor;
                for (row = 0; row < 16; ++row) {
                    unsigned run, run_count;
                    if (cursor >= span_end)
                        return fail(pack, error, error_size, "truncated planar sprite spans");
                    run_count = *cursor++;
                    for (run = 0; run < run_count; ++run) {
                        unsigned start, length;
                        if ((u32)(span_end - cursor) < 2UL)
                            return fail(pack, error, error_size, "truncated planar sprite span");
                        start = *cursor++; length = *cursor++;
                        if (start > 4 || length == 0 || start + length > 5 ||
                            (u32)(span_end - cursor) < length)
                            return fail(pack, error, error_size, "invalid planar sprite span");
                        cursor += length;
                    }
                }
            }
        }
        if (cursor != span_end)
            return fail(pack, error, error_size, "unexpected planar sprite span size");
    }
    pack->map = (u8 *)cursor; cursor += (u32)pack->map_w * pack->map_h;
    for (i = 0; i < pack->berry_count; ++i) {
        pack->berries[i].x = read_u16(&cursor);
        pack->berries[i].y = read_u16(&cursor);
    }
    for (i = 0; i < pack->enemy_count; ++i) {
        pack->enemies[i].type = *cursor++;
        pack->enemies[i].x = read_u16(&cursor);
        pack->enemies[i].y = read_u16(&cursor);
        pack->enemies[i].min_x = read_u16(&cursor);
        pack->enemies[i].max_x = read_u16(&cursor);
    }
    pack->checkpoint.x = read_u16(&cursor);
    pack->checkpoint.y = read_u16(&cursor);
    pack->home.x = read_u16(&cursor);
    pack->home.y = read_u16(&cursor);
    return 1;
}

void assets_free(AssetPack *pack)
{
    if (pack->blob != NULL)
        free(pack->blob);
    memset(pack, 0, sizeof(*pack));
}
