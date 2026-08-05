#include "assets.h"
#include "game.h"
#include "music.h"
#include "platform.h"
#include "trace.h"
#include "video.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define ARCHIVE_PATH "KOLOBOK.DAT"
#define STAGE_COUNT 3
#define FRAME_RATE 30
#define ERROR_SIZE 96
#define CODEWORD_MAX 8

/* A stall longer than a fifth of a second means the process lost the CPU, so the
 * schedule is rebased instead of sprinting to catch up on missed frames. */
#define PACING_RESYNC_TICKS (CLOCKS_PER_SEC / 5L)

enum Stage { STAGE_GARDEN, STAGE_FOREST, STAGE_DEEP };

typedef enum Mode {
    MODE_PLAY, MODE_SELFTEST, MODE_PLAYTEST, MODE_BENCHMARK, MODE_CAPTURE
} Mode;

enum ExitCode {
    EXIT_OK = 0, EXIT_ASSETS = 2, EXIT_SELFTEST = 3, EXIT_VIDEO = 4,
    EXIT_KEYBOARD = 5, EXIT_BENCHMARK = 6, EXIT_CAPTURE = 7,
    EXIT_RUNTIME = 8, EXIT_PLAYTEST = 9
};

static const char *bank_names[STAGE_COUNT] = {"GARDEN", "FOREST", "DEEP"};
static const char *level_names[STAGE_COUNT] = {"GARDEN.KLV", "SFOREST.KLV", "DFOREST.KLV"};
static const unsigned stage_music[STAGE_COUNT] = {MUSIC_GARDEN, MUSIC_FOREST, MUSIC_DEEP};

static int sound_on = 1;

static int abs_int(int value) { return value < 0 ? -value : value; }

/* CLOCKS_PER_SEC is not a multiple of the frame rate, so the leftover ticks are
 * carried between frames rather than truncated away into a slow clock. */
static void wait_for_frame(clock_t *next, unsigned *remainder)
{
    clock_t now;
    do {
        now = clock();
    } while ((long)(now - *next) < 0);
    if ((long)(now - *next) > PACING_RESYNC_TICKS) *next = now;
    *next += CLOCKS_PER_SEC / FRAME_RATE;
    *remainder += CLOCKS_PER_SEC % FRAME_RATE;
    if (*remainder >= FRAME_RATE) {
        ++*next;
        *remainder -= FRAME_RATE;
    }
}

static void play_events(unsigned events)
{
    if (events & Event::WIN) speaker_play(988, 18);
    else if (events & Event::PACIFY) speaker_play(880, 12);
    else if (events & Event::CHECKPOINT) speaker_play(784, 8);
    else if (events & (Event::BERRY | Event::BLUE | Event::PIE)) speaker_play(1047, 5);
    else if (events & Event::HURT) speaker_play(147, 10);
    else if (events & Event::BOUNCE) speaker_play(659, 4);
    else if (events & Event::JUMP) speaker_play(440, 3);
}

static int load_stage(AssetPack *assets, unsigned stage, char *error, unsigned error_size)
{
    return assets_load_bank(assets, ARCHIVE_PATH, bank_names[stage],
                            level_names[stage], error, error_size);
}

/* The title and intro use the garden map with the INTRO bank's palette. */
static int load_title(AssetPack *assets, char *error, unsigned error_size)
{
    return assets_load_bank(assets, ARCHIVE_PATH, "INTRO",
                            level_names[STAGE_GARDEN], error, error_size);
}

static int selftest_metadata(const AssetPack *assets)
{
    return assets->level.width == 96 && assets->level.height == LEVEL_HEIGHT &&
           assets->level.required_red == 6 && assets_far_memory_active(assets);
}

/* Leaves `game` rolling right with a boost active, which is also the state the
 * page-flip checks below render, so the reported CRC covers a moving frame. */
static int selftest_gameplay(const AssetPack *assets, GameState *game)
{
    GameInput input;
    unsigned i;
    game_init(game, assets);
    memset(&input, 0, sizeof(input));
    input.right = 1;
    for (i = 0; i < 30; ++i) game_step(game, &input);
    if (game->player.vx <= 0 || game->player.hp != FULL_HP ||
        game->player.lives != DEFAULT_LIVES) return 0;
    game_apply_pickup(game, PickupType::BLUE);
    if (game->blue_timer != BLUE_FRAMES) return 0;
    game->player.hp = 50;
    game_apply_pickup(game, PickupType::SMALL_PIE);
    return game->player.hp == FULL_HP && game->player.lives == DEFAULT_LIVES;
}

static int selftest_codewords(void)
{
    return campaign_codeword_stage("repka") == STAGE_GARDEN &&
           campaign_codeword_stage("TEREMOK") == STAGE_FOREST &&
           campaign_codeword_stage("MOROZKO") == STAGE_DEEP &&
           campaign_codeword_stage("NOPE") < 0;
}

static int selftest_track(unsigned track)
{
    unsigned i;
    music_play(track);
    for (i = 0; i < 192; ++i) music_tick();
    return music_debug_ticks() == 192 && music_debug_events() >= 40 &&
           music_debug_voice_mask() == 0x3f;
}

static int selftest_music(void)
{
    if (!music_init(1) || !music_is_detected()) return 0;
    if (!selftest_track(MUSIC_GARDEN) || !selftest_track(MUSIC_DEEP)) {
        music_shutdown();
        return 0;
    }
    music_shutdown();
    return 1;
}

/* Rendering must never touch the page being scanned out. Each check renders one
 * screen and confirms the visible page is byte-identical until video_present
 * flips to the page that was just built. */
static int page_untouched(u32 visible, const char *screen)
{
    if (video_vram_crc() == visible) return 1;
    printf("KOLOBOK SELFTEST FAIL visible %s page modified before present\n", screen);
    return 0;
}

static int selftest_video_pages(const AssetPack *assets, const GameState *game,
                                u32 *first, u32 *second)
{
    u32 visible, rendered, menu_a, menu_b, code_good, code_bad;
    video_vsync_enable(0);

    video_render_menu(assets, 0, 0);
    video_present();
    menu_a = video_vram_crc();
    video_render_menu(assets, 1, 1);
    menu_b = video_vram_crc();
    rendered = video_frame_crc();
    if (menu_b != menu_a || rendered == menu_a) {
        puts("KOLOBOK SELFTEST FAIL visible title page modified before present");
        return 0;
    }
    video_present();
    if (video_vram_crc() != rendered) {
        puts("KOLOBOK SELFTEST FAIL hidden title page was not presented");
        return 0;
    }

    visible = video_vram_crc();
    video_render_game(game);
    if (!page_untouched(visible, "game")) return 0;
    video_present();
    *first = video_vram_crc();

    video_render_game(game);
    if (!page_untouched(*first, "alternating game")) return 0;
    video_present();
    *second = video_vram_crc();
    if (*first != *second || !video_display_state_valid()) return 0;

    visible = video_vram_crc();
    video_render_dialogue(game, 1);
    if (!page_untouched(visible, "dialogue")) return 0;
    video_present();
    if (video_frame_crc() != video_vram_crc()) return 0;

    visible = video_vram_crc();
    video_render_codeword(assets, "REPKA", 0);
    if (!page_untouched(visible, "codeword")) return 0;
    video_present();
    code_good = video_vram_crc();

    video_render_codeword(assets, "WRONG", 1);
    if (!page_untouched(code_good, "alternating codeword")) return 0;
    video_present();
    code_bad = video_vram_crc();
    return code_good != code_bad && video_frame_crc() == video_vram_crc();
}

static int selftest(AssetPack *assets)
{
    GameState game;
    u32 first = 0, second = 0;
    int passed;
    if (!selftest_metadata(assets) || !selftest_gameplay(assets, &game) ||
        !selftest_codewords() || !selftest_music()) return 0;
    if (!video_init(assets)) return 0;
    passed = selftest_video_pages(assets, &game, &first, &second);
    video_shutdown();
    if (!passed) return 0;
    printf("KOLOBOK SELFTEST PASS CRC=%08lX VRAM=%08lX\n", first, second);
    return 1;
}

/* Sweeping the camera over the map by a stride coprime with the map width keeps
 * the benchmark off a single column of tiles, which would flatter the tile cache. */
#define BENCH_FRAMES 60
#define BENCH_CAMERA_STRIDE 61UL

static void bench_place_camera(GameState *game, unsigned frame, int span)
{
    int camera = (int)((unsigned long)frame * BENCH_CAMERA_STRIDE % (unsigned long)span);
    game->camera_x = (s32)camera << FP_SHIFT;
    game->player.x = (s32)(camera + CAMERA_OFFSET) << FP_SHIFT;
}

static unsigned long fps_times_ten(clock_t elapsed)
{
    if (!elapsed) elapsed = 1;
    return (unsigned long)BENCH_FRAMES * CLOCKS_PER_SEC * 10UL / (unsigned long)elapsed;
}

static int benchmark(AssetPack *assets)
{
    GameState game;
    GameInput input;
    VideoProfile profile;
    clock_t started, elapsed, paced_started, paced_elapsed, next;
    unsigned frame, remainder = 0;
    int span = (int)assets->level.width * TILE_SIZE - SCREEN_W;
    game_init(&game, assets);
    memset(&input, 0, sizeof(input));
    input.right = 1;
    if (!video_init(assets)) return 0;
    video_vsync_enable(0);
    video_render_game(&game);
    video_present();

    started = clock();
    for (frame = 0; frame < BENCH_FRAMES; ++frame) {
        game_step(&game, &input);
        bench_place_camera(&game, frame, span);
        video_render_game(&game);
        video_present();
    }
    elapsed = clock() - started;

    video_vsync_enable(1);
    paced_started = clock();
    next = paced_started;
    for (frame = 0; frame < BENCH_FRAMES; ++frame) {
        wait_for_frame(&next, &remainder);
        game_step(&game, &input);
        bench_place_camera(&game, frame, span);
        video_render_game(&game);
        video_present();
    }
    paced_elapsed = clock() - paced_started;

    video_vsync_enable(0);
    video_profile_reset();
    video_profile_enable(1);
    for (frame = 0; frame < BENCH_FRAMES; ++frame) {
        bench_place_camera(&game, frame, span);
        video_render_game(&game);
        video_present();
    }
    video_profile_enable(0);
    video_profile_get(&profile);
    video_shutdown();

    if (!elapsed) elapsed = 1;
    printf("KOLOBOK BENCH frames=%u ticks=%lu hz=%lu fps10=%lu paced10=%lu\n",
           BENCH_FRAMES, (unsigned long)elapsed, (unsigned long)CLOCKS_PER_SEC,
           fps_times_ten(elapsed), fps_times_ten(paced_elapsed));
    printf("KOLOBOK PROFILE frames=%u bg=%lu tiles=%lu sprites=%lu hud=%lu vga=%lu hz=%lu\n",
           profile.frames, profile.background_ticks, profile.tile_ticks,
           profile.sprite_ticks, profile.hud_ticks, profile.present_ticks,
           PROFILE_TIMER_HZ);
    return 1;
}

static int scene_is(const char *kind, const char *name)
{
    return kind != 0 && !stricmp(kind, name);
}

static void render_capture_scene(AssetPack *assets, GameState *game, const char *kind)
{
    GameInput input;
    int frame;
    if (scene_is(kind, "intro")) {
        video_render_intro(assets, 1, 30);
    } else if (scene_is(kind, "dialogue")) {
        game->active_dialogue = 1;
        game->active_encounter = 0;
        video_render_dialogue(game, 1);
    } else if (scene_is(kind, "gameover")) {
        game->game_over = 1;
        game->player.lives = 0;
        video_render_game_over(game);
    } else if (scene_is(kind, "home")) {
        video_render_ending(game, 190);
    } else if (scene_is(kind, "credits")) {
        video_render_credits(game, 260);
    } else if (scene_is(kind, "frozen")) {
        game->enemies[0].frozen = 90;
        video_render_game(game);
    } else {
        memset(&input, 0, sizeof(input));
        input.right = 1;
        for (frame = 0; frame < 20; ++frame) game_step(game, &input);
        video_render_game(game);
    }
}

static int capture_frame(AssetPack *assets, const char *kind)
{
    GameState game;
    u32 crc;
    int written;
    game_init(&game, assets);
    if (!video_init(assets)) return 0;
    video_vsync_enable(0);
    render_capture_scene(assets, &game, kind);
    video_present();
    crc = video_vram_crc();
    written = video_write_ppm("KOLOBOK.PPM", assets);
    video_shutdown();
    if (written) printf("KOLOBOK CAPTURE PASS KOLOBOK.PPM CRC=%08lX\n", crc);
    return written;
}

/* The deterministic campaign bot used by tools/dosbox-playtest.sh. It walks each
 * level start to finish with no human input, so a regression that makes a level
 * uncompletable fails the build instead of waiting for someone to play it. */
#define BOT_NO_TARGET 0xffffu
#define BOT_PICKUP_ROW 8
#define BOT_GIVE_UP_FRAMES 400
#define BOT_FRAMES_PER_COLUMN 75U
#define BOT_GUARDIAN_REACH 48
#define BOT_AIM_SLACK 12
#define BOT_ARRIVAL_SLACK 10
#define BOT_HEARTBEAT_FRAMES 30U

typedef struct BotTarget {
    int x;
    unsigned pickup_index;
    int is_pickup;
} BotTarget;

static int bot_encounter_pending(const GameState *game, u16 animal_id)
{
    const LevelData *level = &game->assets->level;
    unsigned e;
    for (e = 0; e < level->encounter_count; ++e)
        if (!game->encounter_solved[e] && level->encounters[e].animal_id == animal_id)
            return 1;
    return 0;
}

/* Only pickups on the ground row are chased: every required berry sits there, and
 * the bot has no route to anything it would have to climb for. */
static BotTarget bot_nearest_pickup(const GameState *game, const u8 *abandoned,
                                    int player_x)
{
    const LevelData *level = &game->assets->level;
    BotTarget target;
    unsigned best = BOT_NO_TARGET, i;
    target.x = (int)level->exit.x * TILE_SIZE;
    target.pickup_index = BOT_NO_TARGET;
    target.is_pickup = 0;
    for (i = 0; i < level->pickup_count; ++i) {
        int candidate;
        unsigned distance;
        if (game->pickup_taken[i] || abandoned[i]) continue;
        if (level->pickups[i].y != BOT_PICKUP_ROW) continue;
        candidate = (int)level->pickups[i].x * TILE_SIZE + 4;
        distance = (unsigned)abs_int(candidate - player_x);
        if (distance >= best) continue;
        best = distance;
        target.x = candidate;
        target.pickup_index = i;
        target.is_pickup = 1;
    }
    return target;
}

/* Walking to the exit is pointless while a required encounter is unsolved,
 * because game_exit_ready refuses to finish and the bot would idle on the exit
 * tile until the frame budget expired. Once the berries are collected, steer to
 * the guardian instead. */
static int bot_guardian_x(const GameState *game, int fallback_x)
{
    const LevelData *level = &game->assets->level;
    unsigned e, j;
    int target_x = fallback_x;
    for (e = 0; e < level->encounter_count; ++e) {
        if (game->encounter_solved[e] || !level->encounters[e].required) continue;
        for (j = 0; j < level->animal_count; ++j)
            if (game->enemies[j].id == level->encounters[e].animal_id)
                target_x = (int)(game->enemies[j].x >> FP_SHIFT);
    }
    return target_x;
}

/* An animal with an unsolved encounter is a destination, not a threat.
 * game_try_talk only fires within 24px vertically while a full-height jump clears
 * 38px, so a bot that jumps here sails over the guardian and the level can never
 * be completed. Guardians share ground with ordinary animals, so being close to
 * one also cancels evasion entirely: staying on the floor to talk matters more
 * than dodging, and the encounter pacifies the animal anyway. */
static void bot_scan_animals(const GameState *game, int player_x, int direction,
                             int *danger, int *talk_near)
{
    const LevelData *level = &game->assets->level;
    unsigned i;
    for (i = 0; i < level->animal_count; ++i) {
        int dx = (int)(game->enemies[i].x >> FP_SHIFT) - player_x;
        if (bot_encounter_pending(game, game->enemies[i].id)) {
            if (dx > -BOT_GUARDIAN_REACH && dx < BOT_GUARDIAN_REACH) *talk_near = 1;
            continue;
        }
        if (game->enemies[i].pacified) continue;
        if ((direction > 0 && dx > -18 && dx < 68) ||
            (direction < 0 && dx < 18 && dx > -68)) *danger = 1;
    }
}

static int bot_hazard_ahead(const GameState *game, int player_x, int direction)
{
    int probe;
    for (probe = 12; probe <= 64; probe += 8)
        if (game_tile_hazard(game, player_x + direction * probe,
                             LEVEL_HEIGHT * TILE_SIZE - 8)) return 1;
    return 0;
}

/* Only jump from the ground row. Every required pickup sits there, so the bot has
 * no reason to climb, and jumping while already standing on a platform used to
 * walk it up a staircase it could not descend. Standing on the ground row puts
 * the top of the player at (LEVEL_HEIGHT-2)*16 - PLAYER_H. */
static int bot_on_ground_row(const GameState *game, int player_y)
{
    int ground_top = (LEVEL_HEIGHT - 2) * TILE_SIZE - PLAYER_H;
    return game->player.on_ground && player_y >= ground_top - 4;
}

static void bot_drive(const GameState *game, GameInput *input,
                      const BotTarget *target, int player_x, int player_y)
{
    int direction = target->x < player_x ? -1 : 1;
    int danger = 0, talk_near = 0;
    memset(input, 0, sizeof(*input));
    if (direction < 0) input->left = 1;
    else input->right = 1;
    input->jump_held = 1;
    input->talk_pressed = 1;
    bot_scan_animals(game, player_x, direction, &danger, &talk_near);
    if (bot_hazard_ahead(game, player_x, direction)) danger = 1;
    if (target->is_pickup && abs_int(player_x - target->x) < BOT_ARRIVAL_SLACK &&
        !game->player.on_ground)
        input->left = input->right = 0;
    if (!talk_near && bot_on_ground_row(game, player_y) &&
        (danger || (game->player.vx == 0 &&
                    abs_int(player_x - target->x) > BOT_AIM_SLACK)))
        input->jump_pressed = 1;
}

/* Nearest-pickup steering can loop forever when an optional detour sits inside an
 * enemy patrol: the bot is knocked back, re-targets the same item and repeats.
 * Give up on one it has chased for 400 frames without collecting. Red berries are
 * exempt, because the level cannot be completed without them, so giving up
 * guarantees the failure it was meant to avoid; keep trying until the frame budget
 * runs out and report that instead. */
static void bot_track_stall(const GameState *game, const BotTarget *target,
                            u8 *abandoned, unsigned *stall, unsigned *last_target,
                            unsigned frame)
{
    const LevelData *level = &game->assets->level;
    unsigned index = target->pickup_index;
    if (!target->is_pickup || index != *last_target ||
        level->pickups[index].type == PickupType::RED) {
        *last_target = target->is_pickup ? index : BOT_NO_TARGET;
        *stall = 0;
        return;
    }
    if (++*stall < BOT_GIVE_UP_FRAMES) return;
    abandoned[index] = 1;
    *stall = 0;
    KOLO_LOG(("f=%u give up %u at=(%u,%u)", frame, index,
              (unsigned)level->pickups[index].x, (unsigned)level->pickups[index].y));
}

static void bot_report_failure(const GameState *game, unsigned stage, unsigned frame)
{
    printf("KOLOBOK PLAYTEST FAIL stage=%u frame=%u x=%d y=%d vx=%ld vy=%ld "
           "ground=%u red=%u guardian=%u hp=%u lives=%u\n",
           stage, frame,
           (int)(game->player.x >> FP_SHIFT), (int)(game->player.y >> FP_SHIFT),
           game->player.vx, game->player.vy, game->player.on_ground,
           game->red_collected, game->guardian_solved,
           game->player.hp, game->player.lives);
    trace_dump("playtest stage failed");
}

static int playtest_stage(AssetPack *assets, unsigned stage, u8 *hp, u8 *lives)
{
    GameState game;
    GameInput input;
    u8 abandoned[MAX_PICKUPS];
    unsigned frame, budget, stall = 0, last_target = BOT_NO_TARGET;
    game_init_carry(&game, assets, *hp, *lives);
    memset(abandoned, 0, sizeof(abandoned));
    budget = (unsigned)assets->level.width * BOT_FRAMES_PER_COLUMN;
    trace_reset();
    KOLO_LOG(("stage %u start w=%u need=%u hp=%u lv=%u", stage,
              (unsigned)assets->level.width, (unsigned)assets->level.required_red,
              (unsigned)*hp, (unsigned)*lives));
    for (frame = 0; frame < budget && !game.won && !game.game_over; ++frame) {
        int player_x = (int)(game.player.x >> FP_SHIFT);
        int player_y = (int)(game.player.y >> FP_SHIFT);
        BotTarget target;
        if (game.active_dialogue) {
            unsigned encounter = (unsigned)game.active_encounter;
            KOLO_LOG(("f=%u talk enc=%u answer=%u", frame, encounter,
                      (unsigned)assets->level.encounters[encounter].correct));
            game_answer_dialogue(&game, assets->level.encounters[encounter].correct);
            continue;
        }
        target = bot_nearest_pickup(&game, abandoned, player_x);
        if (!target.is_pickup) target.x = bot_guardian_x(&game, target.x);
        bot_drive(&game, &input, &target, player_x, player_y);
        game_step(&game, &input);
        bot_track_stall(&game, &target, abandoned, &stall, &last_target, frame);
        /* One heartbeat a second. The ring holds a minute of history, which is
         * what distinguishes "wandered off" from "wedged in one spot". */
        if (frame % BOT_HEARTBEAT_FRAMES == 0U)
            KOLO_LOG(("f=%u x=%d y=%d g=%u to=%d red=%u hp=%u", frame, player_x,
                      player_y, (unsigned)game.player.on_ground, target.x,
                      (unsigned)game.red_collected, (unsigned)game.player.hp));
    }
    if (!game.won || game.game_over ||
        game.red_collected < assets->level.required_red || !game.guardian_solved) {
        bot_report_failure(&game, stage, frame);
        return 0;
    }
    *hp = game.player.hp;
    *lives = game.player.lives;
    printf("KOLOBOK PLAYTEST LEVEL %u PASS frames=%u hp=%u lives=%u\n",
           stage + 1, frame, *hp, *lives);
    return 1;
}

static int campaign_playtest(AssetPack *assets, char *error, unsigned error_size)
{
    u8 hp = FULL_HP, lives = DEFAULT_LIVES;
    unsigned stage;
    int passed = 1;
    assets_free(assets);
    for (stage = 0; stage < STAGE_COUNT && passed; ++stage) {
        if (!load_stage(assets, stage, error, error_size)) return 0;
        passed = playtest_stage(assets, stage, &hp, &lives);
        assets_free(assets);
    }
    if (passed)
        printf("KOLOBOK PLAYTEST PASS sequential carry hp=%u lives=%u\n", hp, lives);
    return passed;
}

static void read_game_input(GameInput *input)
{
    memset(input, 0, sizeof(*input));
    input->left = (u8)(key_down(Key::LEFT) || key_down(Key::A));
    input->right = (u8)(key_down(Key::RIGHT) || key_down(Key::D));
    input->jump_held = (u8)(key_down(Key::SPACE) || key_down(Key::UP));
    input->jump_pressed = (u8)(key_pressed(Key::SPACE) || key_pressed(Key::UP));
    input->talk_pressed = (u8)key_pressed(Key::ENTER);
}

/* Only the letters the three codewords are spelled from are accepted, which keeps
 * the entry field free of characters the 36-glyph font cannot draw. */
static void append_code_key(char *word, unsigned *length)
{
    static const struct { unsigned key; char letter; } letters[] = {
        {Key::R, 'R'}, {Key::E, 'E'}, {Key::P, 'P'}, {Key::K, 'K'}, {Key::A, 'A'},
        {Key::T, 'T'}, {Key::M, 'M'}, {Key::O, 'O'}, {Key::Z, 'Z'}
    };
    unsigned i;
    if (key_pressed(Key::BACKSPACE)) {
        if (*length) word[--*length] = 0;
        return;
    }
    for (i = 0; i < sizeof(letters) / sizeof(letters[0]); ++i)
        if (key_pressed(letters[i].key) && *length < CODEWORD_MAX) {
            word[(*length)++] = letters[i].letter;
            word[*length] = 0;
        }
}

typedef enum UiState {
    UI_TITLE, UI_CODE, UI_INTRO, UI_PLAY, UI_PAUSE, UI_ENDING, UI_CREDITS
} UiState;

typedef struct App {
    AssetPack assets;
    GameState game;
    unsigned stage;
    char error[ERROR_SIZE];
} App;

/* Each level owns the whole far-memory bank, so a stage change tears the video
 * mode and the current bank down before bringing the next one up. */
static int enter_stage(App *app, unsigned stage, u8 hp, u8 lives)
{
    video_shutdown();
    assets_free(&app->assets);
    app->stage = stage;
    if (!load_stage(&app->assets, stage, app->error, sizeof(app->error))) return 0;
    if (!video_init(&app->assets)) return 0;
    game_init_carry(&app->game, &app->assets, hp, lives);
    return 1;
}

static int enter_title(App *app)
{
    video_shutdown();
    assets_free(&app->assets);
    app->stage = STAGE_GARDEN;
    if (!load_title(&app->assets, app->error, sizeof(app->error))) return 0;
    if (!video_init(&app->assets)) return 0;
    game_init(&app->game, &app->assets);
    music_play(MUSIC_TITLE);
    return 1;
}

/* -selftest takes precedence over -playtest over -benchmark over -capture, so a
 * command line naming several modes behaves the same way it always has. */
static Mode parse_arguments(int argc, char **argv, int *music_requested,
                            const char **capture_kind)
{
    int selftest_flag = 0, playtest_flag = 0, benchmark_flag = 0, capture_flag = 0;
    int i;
    for (i = 1; i < argc; ++i) {
        if (!stricmp(argv[i], "-nosound")) sound_on = 0;
        else if (!stricmp(argv[i], "-nomusic")) *music_requested = 0;
        else if (!stricmp(argv[i], "-selftest")) selftest_flag = 1;
        else if (!stricmp(argv[i], "-playtest")) playtest_flag = 1;
        else if (!stricmp(argv[i], "-benchmark")) benchmark_flag = 1;
        else if (!stricmp(argv[i], "-capture")) {
            capture_flag = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') *capture_kind = argv[++i];
        }
    }
    if (selftest_flag) return MODE_SELFTEST;
    if (playtest_flag) return MODE_PLAYTEST;
    if (benchmark_flag) return MODE_BENCHMARK;
    if (capture_flag) return MODE_CAPTURE;
    return MODE_PLAY;
}

/* Capture scenes and the benchmark each need the bank whose art they show. */
static unsigned stage_for_scene(Mode mode, const char *kind)
{
    if (mode == MODE_BENCHMARK) return STAGE_DEEP;
    if (mode != MODE_CAPTURE) return STAGE_GARDEN;
    if (scene_is(kind, "forest")) return STAGE_FOREST;
    if (scene_is(kind, "deep") || scene_is(kind, "home") ||
        scene_is(kind, "credits") || scene_is(kind, "frozen")) return STAGE_DEEP;
    return STAGE_GARDEN;
}

static int load_for_mode(App *app, Mode mode, const char *capture_kind)
{
    int wants_title = mode == MODE_PLAY || mode == MODE_SELFTEST ||
                      mode == MODE_PLAYTEST ||
                      (mode == MODE_CAPTURE && scene_is(capture_kind, "intro"));
    if (wants_title) return load_title(&app->assets, app->error, sizeof(app->error));
    return load_stage(&app->assets, stage_for_scene(mode, capture_kind),
                      app->error, sizeof(app->error));
}

typedef struct TitleMenu {
    unsigned selection;
    char codeword[CODEWORD_MAX + 1];
    unsigned length;
    int invalid;
} TitleMenu;

/* Returns 0 when the player chose to leave the game. */
static int run_title(App *app, TitleMenu *title, UiState *ui,
                     unsigned *intro_scene, u32 *ui_ticks)
{
    if (key_pressed(Key::UP)) title->selection = title->selection ? title->selection - 1 : 2;
    if (key_pressed(Key::DOWN)) title->selection = (title->selection + 1) % 3;
    if (key_pressed(Key::ESCAPE)) return 0;
    if (key_pressed(Key::ENTER) || key_pressed(Key::SPACE)) {
        if (title->selection == 2) return 0;
        if (title->selection == 1) {
            *ui = UI_CODE;
            title->length = 0;
            title->codeword[0] = 0;
            title->invalid = 0;
        } else {
            app->stage = STAGE_GARDEN;
            game_init(&app->game, &app->assets);
            *ui = UI_INTRO;
            *intro_scene = 0;
            *ui_ticks = 0;
            music_play(MUSIC_GARDEN);
        }
        keyboard_clear_edges();
    }
    video_render_menu(&app->assets, *ui_ticks, title->selection);
    video_present();
    return 1;
}

int main(int argc, char **argv)
{
    App app;
    TitleMenu title;
    GameInput input;
    UiState ui = UI_TITLE;
    Mode mode;
    const char *capture_kind = 0;
    unsigned dialogue_choice = 0, intro_scene = 0, remainder = 0;
    u32 ui_ticks = 0;
    int music_requested = 1, running = 1, result;
    clock_t next;

    setvbuf(stdout, NULL, _IONBF, 0);
    mode = parse_arguments(argc, argv, &music_requested, &capture_kind);
    memset(&app, 0, sizeof(app));
    memset(&title, 0, sizeof(title));
    app.stage = STAGE_GARDEN;
    if (!load_for_mode(&app, mode, capture_kind)) {
        fprintf(stderr, "KOLOBOK: %s\n", app.error);
        return EXIT_ASSETS;
    }
    if (mode == MODE_SELFTEST) {
        result = selftest(&app.assets);
        assets_free(&app.assets);
        return result ? EXIT_OK : EXIT_SELFTEST;
    }
    if (mode == MODE_PLAYTEST) {
        result = campaign_playtest(&app.assets, app.error, sizeof(app.error));
        if (app.assets.blob) assets_free(&app.assets);
        return result ? EXIT_OK : EXIT_PLAYTEST;
    }
    if (mode == MODE_BENCHMARK) {
        result = benchmark(&app.assets);
        assets_free(&app.assets);
        return result ? EXIT_OK : EXIT_BENCHMARK;
    }
    if (mode == MODE_CAPTURE) {
        result = capture_frame(&app.assets, capture_kind);
        assets_free(&app.assets);
        return result ? EXIT_OK : EXIT_CAPTURE;
    }

    if (!video_init(&app.assets)) {
        assets_free(&app.assets);
        fprintf(stderr, "KOLOBOK: cannot initialize Mode X\n");
        return EXIT_VIDEO;
    }
    if (!keyboard_install()) {
        video_shutdown();
        assets_free(&app.assets);
        fprintf(stderr, "KOLOBOK: cannot install keyboard handler\n");
        return EXIT_KEYBOARD;
    }
    speaker_init(sound_on);
    music_init(music_requested);
    music_play(MUSIC_TITLE);
    game_init(&app.game, &app.assets);
    next = clock();

    while (running) {
        wait_for_frame(&next, &remainder);
        speaker_tick();
        music_tick();
        ++ui_ticks;
        if (key_pressed(Key::S)) {
            sound_on = !sound_on;
            speaker_shutdown();
            speaker_init(sound_on);
        }
        if (key_pressed(Key::M)) music_set_enabled(!music_is_enabled());

        if (ui == UI_TITLE) {
            running = run_title(&app, &title, &ui, &intro_scene, &ui_ticks);
            continue;
        }
        if (ui == UI_CODE) {
            if (key_pressed(Key::ESCAPE)) {
                ui = UI_TITLE;
                keyboard_clear_edges();
            } else {
                append_code_key(title.codeword, &title.length);
                if (key_pressed(Key::ENTER)) {
                    int selected = campaign_codeword_stage(title.codeword);
                    if (selected < 0) {
                        title.invalid = 1;
                    } else if (!enter_stage(&app, (unsigned)selected,
                                           FULL_HP, DEFAULT_LIVES)) {
                        break;
                    } else {
                        music_play(stage_music[app.stage]);
                        ui = UI_PLAY;
                        keyboard_clear_edges();
                    }
                }
            }
            video_render_codeword(&app.assets, title.codeword, title.invalid);
            video_present();
            continue;
        }
        if (ui == UI_INTRO) {
            if (key_pressed(Key::ESCAPE)) {
                intro_scene = 4;
                keyboard_clear_edges();
            } else if (key_pressed(Key::ENTER) || ui_ticks >= 135) {
                ++intro_scene;
                ui_ticks = 0;
                keyboard_clear_edges();
            }
            if (intro_scene >= 4) {
                if (!enter_stage(&app, STAGE_GARDEN, FULL_HP, DEFAULT_LIVES)) break;
                ui = UI_PLAY;
            } else {
                video_render_intro(&app.assets, intro_scene, ui_ticks);
                video_present();
                continue;
            }
        }
        if (ui == UI_PAUSE) {
            if (key_pressed(Key::ESCAPE)) running = 0;
            else if (key_pressed(Key::ENTER)) {
                ui = UI_PLAY;
                keyboard_clear_edges();
            }
            video_render_pause(&app.game);
            video_present();
            continue;
        }
        if (ui == UI_ENDING) {
            if (key_pressed(Key::ENTER) && ui_ticks > 180) {
                ui = UI_CREDITS;
                ui_ticks = 0;
            }
            video_render_ending(&app.game, ui_ticks);
            video_present();
            continue;
        }
        if (ui == UI_CREDITS) {
            if ((key_pressed(Key::ENTER) && ui_ticks > 420) || key_pressed(Key::ESCAPE)) {
                if (!enter_title(&app)) break;
                ui = UI_TITLE;
            } else {
                video_render_credits(&app.game, ui_ticks);
                video_present();
            }
            continue;
        }
        if (app.game.game_over) {
            if (key_pressed(Key::ENTER)) {
                if (!enter_title(&app)) break;
                ui = UI_TITLE;
            }
            video_render_game_over(&app.game);
            video_present();
            continue;
        }
        if (app.game.active_dialogue) {
            if (key_pressed(Key::UP))
                dialogue_choice = dialogue_choice ? dialogue_choice - 1 : 2;
            if (key_pressed(Key::DOWN)) dialogue_choice = (dialogue_choice + 1) % 3;
            if (key_pressed(Key::DIGIT_1)) dialogue_choice = 0;
            if (key_pressed(Key::DIGIT_2)) dialogue_choice = 1;
            if (key_pressed(Key::DIGIT_3)) dialogue_choice = 2;
            if (key_pressed(Key::ENTER)) {
                game_answer_dialogue(&app.game, dialogue_choice);
                keyboard_clear_edges();
            }
            video_render_dialogue(&app.game, dialogue_choice);
            video_present();
            continue;
        }
        if (key_pressed(Key::ESCAPE)) {
            ui = UI_PAUSE;
            keyboard_clear_edges();
            continue;
        }
        read_game_input(&input);
        game_step(&app.game, &input);
        play_events(app.game.events);
        if (app.game.won) {
            if (app.stage < STAGE_DEEP) {
                u8 hp = app.game.player.hp, lives = app.game.player.lives;
                if (!enter_stage(&app, app.stage + 1, hp, lives)) break;
                music_play(stage_music[app.stage]);
            } else {
                app.game.blue_timer = 0;
                ui = UI_ENDING;
                ui_ticks = 0;
                music_play(MUSIC_HOME);
            }
        }
        video_render_game(&app.game);
        video_present();
    }

    music_shutdown();
    speaker_shutdown();
    keyboard_remove();
    video_shutdown();
    assets_free(&app.assets);
    if (!running) return EXIT_OK;
    fprintf(stderr, "KOLOBOK: %s\n", app.error);
    return EXIT_RUNTIME;
}
