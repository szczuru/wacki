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
    if (src_y + src_h > GAME_HEIGHT) src_y = GAME_HEIGHT - src_h;
    
    /* Scale up to fill bottom screen */
    float x_ratio = (float)src_w / BOTTOM_SCREEN_WIDTH;
    float y_ratio = (float)src_h / BOTTOM_SCREEN_HEIGHT;
    
    for (int y = 0; y < BOTTOM_SCREEN_HEIGHT; y++) {
        int sy = src_y + (int)(y * y_ratio);
        if (sy >= GAME_HEIGHT) sy = GAME_HEIGHT - 1;
        
        for (int x = 0; x < BOTTOM_SCREEN_WIDTH; x++) {
            int sx = src_x + (int)(x * x_ratio);
            if (sx >= GAME_WIDTH) sx = GAME_WIDTH - 1;
            
            u8 idx = shadow[sy * GAME_WIDTH + sx];
            const u8 *rgb = pal + idx * 3;
            out[y * BOTTOM_SCREEN_WIDTH + x] = rgb888_to_rgb565(rgb[0], rgb[1], rgb[2]);
        }
    }
}

/* Handle touch screen input */
static void handle_touch_input(void)
{
    touchPosition touch;
    u32 kHeld = hidKeysHeld();
    
    if (kHeld & KEY_TOUCH) {
        hidTouchRead(&touch);
        
        /* Map touch coordinates to game coordinates based on zoom level */
        int zoom_level = platform_3ds_get_zoom_level();
        int src_w = BOTTOM_SCREEN_WIDTH >> zoom_level;
        int src_h = BOTTOM_SCREEN_HEIGHT >> zoom_level;
        
        /* Calculate offset of zoomed region */
        int src_x = g_mouse_x - src_w / 2;
        int src_y = g_mouse_y - src_h / 2;
        if (src_x < 0) src_x = 0;
        if (src_y < 0) src_y = 0;
        if (src_x + src_w > GAME_WIDTH) src_x = GAME_WIDTH - src_w;
        if (src_y + src_h > GAME_HEIGHT) src_y = GAME_HEIGHT - src_h;
        
        /* Map touch position to game coordinates */
        float x_ratio = (float)src_w / BOTTOM_SCREEN_WIDTH;
        float y_ratio = (float)src_h / BOTTOM_SCREEN_HEIGHT;
        
        int game_x = src_x + (int)(touch.px * x_ratio);
        int game_y = src_y + (int)(touch.py * y_ratio);
        
        /* Clamp to game bounds */
        if (game_x < 0) game_x = 0;
        if (game_x >= GAME_WIDTH) game_x = GAME_WIDTH - 1;
        if (game_y < 0) game_y = 0;
        if (game_y >= GAME_HEIGHT) game_y = GAME_HEIGHT - 1;
        
        g_mouse_x = game_x;
        g_mouse_y = game_y;
        
        /* Detect tap (touch down edge) */
        if (!s_touch_active) {
            g_lmb_clicked = 1;
        }
        
        s_touch_active = 1;
        s_last_touch_x = touch.px;
        s_last_touch_y = touch.py;
    } else {
        s_touch_active = 0;
    }
}

unsigned plat_video_sdl_init_flags(void)
{
    /* 3DS doesn't use SDL for video */
    return 0;
}

int plat_video_init(int w, int h, const char *title)
{
    (void)title;
    
    if (w != GAME_WIDTH || h != GAME_HEIGHT) {
        LOG_INFO("video", "Warning: game resolution %dx%d != expected %dx%d",
                 w, h, GAME_WIDTH, GAME_HEIGHT);
    }
    
    /* Initialize graphics */
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    
    /* Create render targets */
    s_top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    
    if (!s_top_target || !s_bottom_target) {
        LOG_INFO("video", "Failed to create render targets");
        return 0;
    }
    
    /* Allocate framebuffers */
    s_top_pixels = (u16*)linearAlloc(TOP_SCREEN_WIDTH * TOP_SCREEN_HEIGHT * sizeof(u16));
    s_bottom_pixels = (u16*)linearAlloc(BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * sizeof(u16));
    
    if (!s_top_pixels || !s_bottom_pixels) {
        LOG_INFO("video", "Failed to allocate framebuffers");
        return 0;
    }
    
    /* Clear framebuffers */
    memset(s_top_pixels, 0, TOP_SCREEN_WIDTH * TOP_SCREEN_HEIGHT * sizeof(u16));
    memset(s_bottom_pixels, 0, BOTTOM_SCREEN_WIDTH * BOTTOM_SCREEN_HEIGHT * sizeof(u16));
    
    /* Initialize tint (no tinting) */
    C2D_PlainImageTint(&s_tint, C2D_Color32(255, 255, 255, 255), 1.0f);
    
    LOG_INFO("platform", "3DS dual-screen video initialized: top=%dx%d, bottom=%dx%d",
             TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
             BOTTOM_SCREEN_WIDTH, BOTTOM_SCREEN_HEIGHT);
    
    return 1;
}

void plat_video_present(const uint8_t *shadow, const uint8_t *palette_rgb,
                        int w, int h)
{
    if (!shadow || !palette_rgb || !s_top_pixels || !s_bottom_pixels) return;
    
    /* Handle touch input first */
    handle_touch_input();
    
    /* Convert game output to top screen (scaled) */
    convert_to_rgb565_scaled(shadow, palette_rgb, s_top_pixels,
                             TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                             w, h);
    
    /* Extract zoomed region for bottom screen */
    extract_zoom_region(shadow, palette_rgb, s_bottom_pixels,
                       g_mouse_x, g_mouse_y);
    
    /* Render top screen */
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    C2D_TargetClear(s_top_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(s_top_target);
    
    /* Draw top screen image */
    Tex3DS_SubTexture sub_top;
    sub_top.width = TOP_SCREEN_WIDTH;
    sub_top.height = TOP_SCREEN_HEIGHT;
    sub_top.left = 0.0f;
    sub_top.top = 1.0f;
    sub_top.right = 1.0f;
    sub_top.bottom = 0.0f;
    
    s_top_image.tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    C3D_TexInit(s_top_image.tex, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, GPU_RGB565);
    C3D_TexUpload(s_top_image.tex, s_top_pixels);
    s_top_image.subtex = &sub_top;
    
    C2D_DrawImageAt(s_top_image, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
    
    C3D_TexDelete(s_top_image.tex);
    free(s_top_image.tex);
    
    /* Render bottom screen */
    C2D_TargetClear(s_bottom_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(s_bottom_target);
    
    /* Draw bottom screen image */
    Tex3DS_SubTexture sub_bottom;
    sub_bottom.width = BOTTOM_SCREEN_WIDTH;
    sub_bottom.height = BOTTOM_SCREEN_HEIGHT;
    sub_bottom.left = 0.0f;
    sub_bottom.top = 1.0f;
    sub_bottom.right = 1.0f;
    sub_bottom.bottom = 0.0f;
    
    s_bottom_image.tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    C3D_TexInit(s_bottom_image.tex, BOTTOM_SCREEN_WIDTH, BOTTOM_SCREEN_HEIGHT, GPU_RGB565);
    C3D_TexUpload(s_bottom_image.tex, s_bottom_pixels);
    s_bottom_image.subtex = &sub_bottom;
    
    C2D_DrawImageAt(s_bottom_image, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
    
    /* Draw cursor crosshair on bottom screen */
    int zoom_level = platform_3ds_get_zoom_level();
    int cursor_screen_x = BOTTOM_SCREEN_WIDTH / 2;
    int cursor_screen_y = BOTTOM_SCREEN_HEIGHT / 2;
    
    /* Draw simple crosshair */
    C2D_DrawRectSolid(cursor_screen_x - 5, cursor_screen_y, 0.9f, 10, 1,
                      C2D_Color32(255, 255, 0, 255));
    C2D_DrawRectSolid(cursor_screen_x, cursor_screen_y - 5, 0.9f, 1, 10,
                      C2D_Color32(255, 255, 0, 255));
    
    C3D_TexDelete(s_bottom_image.tex);
    free(s_bottom_image.tex);
    
    C3D_FrameEnd(0);
}

void plat_video_shutdown(void)
{
    if (s_top_pixels) {
        linearFree(s_top_pixels);
        s_top_pixels = NULL;
    }
    if (s_bottom_pixels) {
        linearFree(s_bottom_pixels);
        s_bottom_pixels = NULL;
    }
    
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    
    LOG_INFO("platform", "3DS video shutdown");
}

void plat_video_toggle_fullscreen(void)
{
    /* 3DS is always fullscreen */
}

void plat_video_message_box(const char *title, const char *body)
{
    /* 3DS doesn't have native message boxes - log instead */
    LOG_INFO("msgbox", "%s: %s", title, body);
}
