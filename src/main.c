#include "assets.h"
#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

static int sound_on = 1;

static void play_events(unsigned events)
{
    if (events & KOLO_EVENT_WIN) speaker_play(988, 18);
    else if (events & KOLO_EVENT_CHECKPOINT) speaker_play(784, 8);
    else if (events & KOLO_EVENT_BERRY) speaker_play(1047, 5);
    else if (events & KOLO_EVENT_HURT) speaker_play(147, 10);
    else if (events & KOLO_EVENT_BOUNCE) speaker_play(659, 4);
    else if (events & KOLO_EVENT_JUMP) speaker_play(440, 3);
}

static int selftest(AssetPack *assets)
{
    GameState game;
    GameState completion;
    GameInput input;
    u32 crc;
    int i;
    if (assets->map_w != 80 || assets->map_h != 11 || assets->berry_count != 8)
        return 0;
    game_init(&game, assets);
    if (!game_tile_solid(&game, 8, 9 * 16) || !game_tile_hazard(&game, 14 * 16, 10 * 16))
        return 0;
    memset(&input, 0, sizeof(input));
    input.right = 1;
    for (i = 0; i < 75; ++i) {
        input.jump_pressed = (i == 12);
        input.jump_held = (i >= 12 && i < 21);
        game_step(&game, &input);
    }
    if ((game.player.x >> KOLO_FP_SHIFT) <= 60 || game.respawns != 0)
        return 0;
    game_init(&completion, assets);
    completion.left_home = 1;
    completion.player.invulnerable = 255;
    memset(&input, 0, sizeof(input));
    for (i = 0; i < assets->berry_count; ++i) {
        completion.player.x = (s32)assets->berries[i].x << KOLO_FP_SHIFT;
        completion.player.y = (s32)assets->berries[i].y << KOLO_FP_SHIFT;
        completion.player.vx = completion.player.vy = 0;
        game_step(&completion, &input);
    }
    if (completion.berries_collected != assets->berry_count)
        return 0;
    completion.player.x = (s32)(assets->home.x + 20) << KOLO_FP_SHIFT;
    completion.player.y = (s32)assets->home.y << KOLO_FP_SHIFT;
    completion.player.vx = completion.player.vy = 0;
    game_step(&completion, &input);
    if (!completion.won || !(completion.events & KOLO_EVENT_WIN))
        return 0;
    if (!video_init(assets)) return 0;
    video_render_game(&game);
    crc = video_frame_crc();
    video_shutdown();
    if (crc != 0x5bd3ecb5UL) {
        printf("KOLOBOK SELFTEST FAIL CRC=%08lX EXPECTED=5BD3ECB5\n", crc);
        return 0;
    }
    printf("KOLOBOK SELFTEST PASS CRC=%08lX\n", crc);
    return 1;
}

static void read_game_input(GameInput *input)
{
    input->left = (u8)(key_down(KEY_LEFT) || key_down(KEY_A));
    input->right = (u8)(key_down(KEY_RIGHT) || key_down(KEY_D));
    input->jump_held = (u8)(key_down(KEY_SPACE) || key_down(KEY_UP));
    input->jump_pressed = (u8)(key_pressed(KEY_SPACE) || key_pressed(KEY_UP));
}

int main(int argc, char **argv)
{
    AssetPack assets;
    GameState game;
    GameInput input;
    char error[80];
    int running = 1, title = 1, paused = 0;
    unsigned cadence = 0;
    u32 title_ticks = 0;
    int test_mode = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (stricmp(argv[i], "-nosound") == 0) sound_on = 0;
        else if (stricmp(argv[i], "-selftest") == 0) test_mode = 1;
    }
    if (!assets_load(&assets, "KOLOBOK.DAT", error, sizeof(error))) {
        fprintf(stderr, "KOLOBOK: %s\n", error);
        return 2;
    }
    if (test_mode) {
        int passed = selftest(&assets);
        assets_free(&assets);
        return passed ? 0 : 3;
    }
    if (!video_init(&assets)) {
        assets_free(&assets);
        fprintf(stderr, "KOLOBOK: cannot allocate 64 KB video buffer\n");
        return 4;
    }
    if (!keyboard_install()) {
        video_shutdown(); assets_free(&assets);
        fprintf(stderr, "KOLOBOK: cannot install keyboard handler\n");
        return 5;
    }
    speaker_init(sound_on);
    game_init(&game, &assets);
    video_render_title(&assets, 0); video_present();

    while (running) {
        wait_vblank();
        cadence += 30;
        if (cadence < 70) continue;
        cadence -= 70;
        speaker_tick();
        if (key_pressed(KEY_S)) {
            sound_on = !sound_on;
            speaker_shutdown(); speaker_init(sound_on);
        }
        if (title) {
            ++title_ticks;
            if (key_pressed(KEY_ESCAPE)) running = 0;
            if (key_pressed(KEY_ENTER) || key_pressed(KEY_SPACE)) {
                game_init(&game, &assets); title = 0; keyboard_clear_edges();
            }
            video_render_title(&assets, title_ticks); video_present();
            continue;
        }
        if (game.won) {
            if (key_pressed(KEY_ESCAPE)) running = 0;
            if (key_pressed(KEY_ENTER)) {
                game_init(&game, &assets); title = 1; keyboard_clear_edges();
            }
            video_render_win(&game); video_present();
            continue;
        }
        if (paused) {
            if (key_pressed(KEY_ESCAPE)) running = 0;
            else if (key_pressed(KEY_ENTER)) { paused = 0; keyboard_clear_edges(); }
            video_render_pause(&game); video_present();
            continue;
        }
        if (key_pressed(KEY_ESCAPE)) {
            paused = 1; keyboard_clear_edges();
            continue;
        }
        read_game_input(&input);
        game_step(&game, &input);
        play_events(game.events);
        video_render_game(&game);
        video_present();
    }
    speaker_shutdown();
    keyboard_remove();
    video_shutdown();
    assets_free(&assets);
    return 0;
}
