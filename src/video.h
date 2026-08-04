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
void video_render_title(const AssetPack *assets, u32 ticks);
void video_render_game(const GameState *game);
void video_render_pause(const GameState *game);
void video_render_win(const GameState *game);
u32 video_frame_crc(void);
u32 video_vram_crc(void);
void video_profile_enable(int enabled);
void video_profile_reset(void);
void video_profile_get(VideoProfile *result);
int video_display_state_valid(void);
int video_write_ppm(const char *path, const AssetPack *assets);

#endif
