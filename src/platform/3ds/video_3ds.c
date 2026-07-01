/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds.c — video-output HAL, Nintendo 3DS dual-screen backend.
 *
 * Top screen (400x240): Main game display, scaled from 640x480
 * Bottom screen (320x240): Zoomed view around cursor + touch input
 *
 * The game renders to 640x480 8-bpp shadow buffer. We:
 * 1. Convert to RGB565 for top screen (full game, scaled to fit 400x240)
 * 2. Extract zoomed region around cursor for bottom screen (320x240)
 * 3. Handle touch input on bottom screen to move cursor
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdlib.h>

/* Forward declaration from gamepad */
extern int platform_3ds_get_zoom_level(void);

/* External globals from engine */
extern int16_t g_mouse_x, g_mouse_y;
extern int g_lmb_clicked, g_rmb_clicked;

/* Screen dimensions */
#define TOP_SCREEN_WIDTH  400
#define TOP_SCREEN_HEIGHT 240
#define BOTTOM_SCREEN_WIDTH  320
#define BOTTOM_SCREEN_HEIGHT 240

/* Game framebuffer dimensions */
#define GAME_WIDTH  640
#define GAME_HEIGHT 480

/* Citro2D/3D contexts */
static C3D_RenderTarget *s_top_target = NULL;
static C3D_RenderTarget *s_bottom_target = NULL;
static C2D_Image s_top_image;
static C2D_Image s_bottom_image;
static C2D_ImageTint s_tint;

/* RGB565 framebuffers for converted game output */
static u16 *s_top_pixels = NULL;
static u16 *s_bottom_pixels = NULL;

/* Shadow copy of game state for rendering */
static u8 s_shadow_copy[GAME_WIDTH * GAME_HEIGHT];
static u8 s_palette_copy[256 * 3];

/* Touch state */
static int s_touch_active = 0;
static int s_last_touch_x = 0, s_last_touch_y = 0;

/* Convert RGB888 to RGB565 */
static inline u16 rgb888_to_rgb565(u8 r, u8 g, u8 b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/* Convert 8-bpp indexed shadow buffer to RGB565 using palette */
static void convert_to_rgb565_scaled(const u8 *shadow, const u8 *pal,
                                      u16 *out, int out_w, int out_h,
                                      int src_w, int src_h)
{
    /* Simple nearest-neighbor scaling for performance */
    float x_ratio = (float)src_w / out_w;
    float y_ratio = (float)src_h / out_h;
    
    for (int y = 0; y < out_h; y++) {
        int src_y = (int)(y * y_ratio);
        if (src_y >= src_h) src_y = src_h - 1;
        
        for (int x = 0; x < out_w; x++) {
            int src_x = (int)(x * x_ratio);
            if (src_x >= src_w) src_x = src_w - 1;
            
            u8 idx = shadow[src_y * src_w + src_x];
            const u8 *rgb = pal + idx * 3;
            out[y * out_w + x] = rgb888_to_rgb565(rgb[0], rgb[1], rgb[2]);
        }
    }
}

/* Extract zoomed region around cursor for bottom screen */
static void extract_zoom_region(const u8 *shadow, const u8 *pal,
                                u16 *out, int cursor_x, int cursor_y)
{
    int zoom_level = platform_3ds_get_zoom_level();
    
    /* Calculate source region size based on zoom level */
    /* zoom_level 0 = 100% (320x240), 1 = 50% (160x120), 2 = 25% (80x60), 3 = 12.5% (40x30) */
    int src_w = BOTTOM_SCREEN_WIDTH >> zoom_level;
    int src_h = BOTTOM_SCREEN_HEIGHT >> zoom_level;
    
    /* Center on cursor */
    int src_x = cursor_x - src_w / 2;
    int src_y = cursor_y - src_h / 2;
    
    /* Clamp to game bounds */
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x + src_w > GAME_WIDTH) src_x = GAME_WIDTH - src_w;
    if
