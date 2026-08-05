#ifndef KOLOBOK_VIDEO_H
#define KOLOBOK_VIDEO_H

#include "game.h"

typedef struct VideoProfile {
    u32 background_ticks;
    u32 tile_ticks;
    u32 sprite_ticks;
    u32 hud_ticks;
    u32 present_ticks;
    u16 frames;
} VideoProfile;

int video_init(const AssetPack *assets);
void video_shutdown(void);
void video_present(void);
void video_vsync_enable(int enabled);
void video_render_menu(const AssetPack *assets, unsigned selection);
void video_render_codeword(const AssetPack *assets, const char *word, int invalid);
void video_render_game(const GameState *game);
void video_render_pause(const GameState *game);
void video_render_win(const GameState *game);
void video_render_dialogue(const GameState *game, unsigned selection);
void video_render_game_over(const GameState *game);
void video_render_intro(const AssetPack *assets, unsigned scene, u32 ticks);
void video_render_ending(const GameState *game, u32 ticks);
void video_render_credits(const GameState *game, u32 ticks);
void video_render_editor(const GameState *game, unsigned cursor_x, unsigned cursor_y,
                         unsigned layer, unsigned tool, int dirty, int valid);
void video_render_editor_help(const GameState *game);
void video_render_editor_exit(const GameState *game);
void video_render_editor_properties(const GameState *game, unsigned kind,
                                    unsigned index, unsigned field);
u32 video_frame_crc(void);
u32 video_vram_crc(void);
void video_profile_enable(int enabled);
void video_profile_reset(void);
void video_profile_get(VideoProfile *result);
int video_display_state_valid(void);
int video_write_ppm(const char *path, const AssetPack *assets);

#endif
