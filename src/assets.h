#ifndef KOLOBOK_ASSETS_H
#define KOLOBOK_ASSETS_H

#include <stdio.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed long s32;

#define KOLO_TILE_SIZE 16
#define KOLO_MAX_BERRIES 8
#define KOLO_MAX_ENEMIES 8
#define KOLO_MAX_SPRITES 8

typedef struct KoloPoint {
    u16 x;
    u16 y;
} KoloPoint;

typedef struct KoloEnemySpawn {
    u8 type;
    u16 x;
    u16 y;
    u16 min_x;
    u16 max_x;
} KoloEnemySpawn;

typedef struct AssetPack {
    u8 *blob;
    u32 blob_size;
    u16 map_w;
    u16 map_h;
    u16 tile_count;
    u16 sprite_count;
    u8 *palette;
    u8 *tiles;
    u8 *sprite_spans[KOLO_MAX_SPRITES];
    u8 *sprite_planar_spans[KOLO_MAX_SPRITES][16];
    u8 *map;
    u16 berry_count;
    KoloPoint berries[KOLO_MAX_BERRIES];
    u16 enemy_count;
    KoloEnemySpawn enemies[KOLO_MAX_ENEMIES];
    KoloPoint checkpoint;
    KoloPoint home;
} AssetPack;

int assets_load(AssetPack *pack, const char *path, char *error, unsigned error_size);
void assets_free(AssetPack *pack);
u32 assets_crc32(const u8 *data, u32 length);

#endif
