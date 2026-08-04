#ifndef KOLOBOK_VIDEO_H
#define KOLOBOK_VIDEO_H

#include "game.h"

int video_init(const AssetPack *assets);
void video_shutdown(void);
void video_present(void);
void video_render_title(const AssetPack *assets, u32 ticks);
void video_render_game(const GameState *game);
void video_render_pause(const GameState *game);
void video_render_win(const GameState *game);
u32 video_frame_crc(void);
u32 video_vram_crc(void);

#endif
