#include "assets.h"
#include "game.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_assets(void)
{
    AssetPack pack;
    char error[80];
    assert(assets_load(&pack, "build/KOLOBOK.DAT", error, sizeof(error)));
    assert(pack.map_w == 80);
    assert(pack.map_h == 11);
    assert(pack.berry_count == 8);
    assert(pack.enemy_count == 4);
    assert(pack.tile_count == 6);
    assert(pack.sprite_count == 8);
    assets_free(&pack);
}

static void test_asset_checksum_rejection(void)
{
    FILE *source = fopen("build/KOLOBOK.DAT", "rb");
    FILE *target = fopen("build/CORRUPT.DAT", "wb");
    AssetPack pack;
    char error[80];
    int byte;
    long offset = 0;
    assert(source != NULL && target != NULL);
    while ((byte = fgetc(source)) != EOF) {
        if (offset == 100) byte ^= 1;
        assert(fputc(byte, target) != EOF);
        ++offset;
    }
    assert(fclose(source) == 0);
    assert(fclose(target) == 0);
    assert(!assets_load(&pack, "build/CORRUPT.DAT", error, sizeof(error)));
    assert(strstr(error, "checksum") != NULL);
    assert(remove("build/CORRUPT.DAT") == 0);
}

static void test_movement_and_jump(void)
{
    AssetPack pack;
    GameState game;
    GameInput input;
    char error[80];
    int i;
    assert(assets_load(&pack, "build/KOLOBOK.DAT", error, sizeof(error)));
    game_init(&game, &pack);
    memset(&input, 0, sizeof(input));
    input.right = 1;
    for (i = 0; i < 18; ++i) game_step(&game, &input);
    assert(game.player.vx > 0);
    assert((game.player.x >> KOLO_FP_SHIFT) > pack.home.x + 36);
    input.jump_pressed = 1;
    input.jump_held = 1;
    game_step(&game, &input);
    assert((game.events & KOLO_EVENT_JUMP) != 0);
    assert(game.player.vy < 0);
    input.jump_pressed = 0;
    for (i = 0; i < 6; ++i) game_step(&game, &input);
    input.jump_held = 0;
    game_step(&game, &input);
    assert(game.player.vy >= -350);
    assets_free(&pack);
}

static void test_collect_and_return(void)
{
    AssetPack pack;
    GameState game;
    char error[80];
    unsigned i;
    assert(assets_load(&pack, "build/KOLOBOK.DAT", error, sizeof(error)));
    game_init(&game, &pack);
    game.left_home = 1;
    for (i = 0; i < pack.berry_count; ++i) {
        game.berry_taken[i] = 1;
        ++game.berries_collected;
    }
    game.player.x = (s32)(pack.home.x + 20) << KOLO_FP_SHIFT;
    game_step(&game, &(GameInput){0, 0, 0, 0});
    assert(game.won);
    assert((game.events & KOLO_EVENT_WIN) != 0);
    assets_free(&pack);
}

static void test_checkpoint_persistence(void)
{
    AssetPack pack;
    GameState game;
    char error[80];
    assert(assets_load(&pack, "build/KOLOBOK.DAT", error, sizeof(error)));
    game_init(&game, &pack);
    game.berry_taken[0] = 1;
    game.berries_collected = 1;
    game.checkpoint_x = (s32)pack.checkpoint.x << KOLO_FP_SHIFT;
    game.checkpoint_y = (s32)(pack.checkpoint.y + 2) << KOLO_FP_SHIFT;
    game_respawn(&game);
    assert(game.berry_taken[0] == 1);
    assert(game.berries_collected == 1);
    assert(game.player.x == game.checkpoint_x);
    assert(game.player.invulnerable == 30);
    assets_free(&pack);
}

int main(void)
{
    test_assets();
    test_asset_checksum_rejection();
    test_movement_and_jump();
    test_collect_and_return();
    test_checkpoint_persistence();
    puts("host gameplay tests: PASS");
    return 0;
}
