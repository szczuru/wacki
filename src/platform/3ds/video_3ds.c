/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds.c — 3DS dual-screen video backend.
 *
 * Uses real SDL2 from devkitPro portlibs (software rendering) with custom
 * dual-screen layout:
 * - Top screen (400x240): Main game view, scaled down from 640x480
 * - Bottom screen (320x240): Zoomed view around cursor + touch input
 *
 * SDL2 for 3DS renders to the top screen by default. We handle the bottom
 * screen manually by blitting a zoomed region to a separate surface.
 *
 * NOTE: This is NOT using citro2d/citro3d - pure SDL2 software rendering. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"

#include <SDL.h>
#include <3ds.h>
#include <string.h>

#define TOP_SCREEN_W    400
#define TOP_SCREEN_H    240
#define BOTTOM_SCREEN_W 320
#define BOTTOM_SCREEN_H 240
#define GAME_W          640
#define GAME_H          480

/* SDL objects */
static SDL_Window   *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_game_texture = NULL;  /* Full 640x480 game buffer */

/* Bottom screen rendering */
static SDL_Surface *s_bottom_surface = NULL;

/* Zoom level (extern from gamepad) */
extern int platform_3ds_get_zoom_level(void);

/* Mouse cursor position (from wacki.h) */
extern int16_t g_mouse_x, g_mouse_y;

unsigned plat_video_sdl_init_flags(void)
{
    return SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER;
}

int plat_video_init(int w, int h, const char *title)
{
    (void)title; /* 3DS has no window title bar */
    
    LOG_INFO("3ds-video", "Initializing dual-screen display (game: %dx%d)", w, h);
    
    /* Initialize 3DS graphics hardware */
    gfxInitDefault();
    gfxSet3D(false); /* Disable stereoscopic 3D */
    
    /* Create SDL window for top screen (SDL2 renders here by default) */
    s_window = SDL_CreateWindow("Wacki",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 TOP_SCREEN_W, TOP_SCREEN_H,
                                 SDL_WINDOW_SHOWN);
    if (!s_window) {
        LOG_ERROR("3ds-video", "SDL_CreateWindow failed: %s", SDL_GetError());
        return 0;
    }
    
    /* Create software renderer (SDL2 for 3DS uses software rendering) */
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    if (!s_renderer) {
        LOG_ERROR("3ds-video", "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(s_window);
        return 0;
    }
    
    /* Create streaming texture for game framebuffer (640x480) */
    s_game_texture = SDL_CreateTexture(s_renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       w, h);
    if (!s_game_texture) {
        LOG_ERROR("3ds-video", "SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        return 0;
    }
    
    /* For bottom screen, we'll create a separate surface
     * Note: Actual blitting to bottom screen framebuffer is TODO */
    s_bottom_surface = SDL_CreateRGBSurface(0,
                                            BOTTOM_SCREEN_W,
                                            BOTTOM_SCREEN_H,
                                            32,
                                            0xFF000000, /* R */
                                            0x00FF0000, /* G */
                                            0x0000FF00, /* B */
                                            0x000000FF  /* A */);
    if (!s_bottom_surface) {
        LOG_ERROR("3ds-video", "SDL_CreateRGBSurface failed: %s", SDL_GetError());
        SDL_DestroyTexture(s_game_texture);
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        return 0;
    }
    
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* Nearest neighbor */
    
    LOG_INFO("3ds-video", "Dual-screen initialized successfully");
    return 1;
}

/* Helper: Draw crosshair on surface */
static void draw_crosshair(SDL_Surface *surf, int x, int y)
{
    if (!surf || x < 0 || y < 0 || x >= surf->w || y >= surf->h) return;
    
    if (SDL_MUSTLOCK(surf)) SDL_LockSurface(surf);
    
    Uint32 yellow = SDL_MapRGBA(surf->format, 255, 255, 0, 255);
    int size = 6;
    int thickness = 2;
    
    /* Horizontal line */
    for (int dy = -thickness/2; dy <= thickness/2; dy++) {
        for (int dx = -size; dx <= size; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < surf->w && py >= 0 && py < surf->h) {
                Uint32 *pixels = (Uint32 *)surf->pixels;
                pixels[py * surf->w + px] = yellow;
            }
        }
    }
    
    /* Vertical line */
    for (int dy = -size; dy <= size; dy++) {
        for (int dx = -thickness/2; dx <= thickness/2; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < surf->w && py >= 0 && py < surf->h) {
                Uint32 *pixels = (Uint32 *)surf->pixels;
                pixels[py * surf->w + px] = yellow;
            }
        }
    }
    
    if (SDL_MUSTLOCK(surf)) SDL_UnlockSurface(surf);
}

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!s_renderer || !s_game_texture || !shadow || !pal) return;
    
    /* ===== 1. Update game texture (convert 8bpp + palette to ARGB32) ===== */
    void *pixels = NULL;
    int pitch = 0;
    
    if (SDL_LockTexture(s_game_texture, NULL, &pixels, &pitch) == 0) {
        uint32_t *out = (uint32_t *)pixels;
        int stride = pitch / 4;
        
        /* Convert indexed color to ARGB */
        for (int y = 0; y < h; ++y) {
            uint32_t *row = out + y * stride;
            const uint8_t *src = shadow + y * w;
            for (int x = 0; x < w; ++x) {
                const uint8_t *rgb = pal + src[x] * 3;
                /* ARGB8888 format */
                row[x] = 0xFF000000u | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
            }
        }
        SDL_UnlockTexture(s_game_texture);
    }
    
    /* ===== 2. TOP SCREEN: Scaled game view ===== */
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    
    /* Scale 640x480 down to 320x240 (0.5x), center on 400x240 screen
     * This gives 40px black bars on left and right */
    SDL_Rect top_dst = {
        (TOP_SCREEN_W - 320) / 2,  /* x = 40 */
        0,                          /* y = 0 */
        320,                        /* w */
        240                         /* h */
    };
    SDL_RenderCopy(s_renderer, s_game_texture, NULL, &top_dst);
    SDL_RenderPresent(s_renderer);
    
    /* ===== 3. BOTTOM SCREEN: Zoomed view (prepared but not displayed yet) ===== */
    if (s_bottom_surface) {
        /* Get current zoom level (0=1x, 1=2x, 2=4x, 3=8x) */
        int zoom = platform_3ds_get_zoom_level();
        float zoom_factor = 1.0f / (float)(1 << zoom);
        
        /* Calculate source region in game coordinates */
        int view_w = (int)(BOTTOM_SCREEN_W * zoom_factor);
        int view_h = (int)(BOTTOM_SCREEN_H * zoom_factor);
        
        int view_x = g_mouse_x - view_w / 2;
        int view_y = g_mouse_y - view_h / 2;
        
        /* Clamp to game bounds */
        if (view_x < 0) view_x = 0;
        if (view_y < 0) view_y = 0;
        if (view_x + view_w > w) view_x = w - view_w;
        if (view_y + view_h > h) view_y = h - view_h;
        
        /* Lock texture to read pixels */
        void *tex_pixels = NULL;
        int tex_pitch = 0;
        
        if (SDL_LockTexture(s_game_texture, NULL, &tex_pixels, &tex_pitch) == 0) {
            /* Clear bottom surface */
            SDL_FillRect(s_bottom_surface, NULL, 0);
            
            if (SDL_MUSTLOCK(s_bottom_surface)) SDL_LockSurface(s_bottom_surface);
            
            uint32_t *tex_data = (uint32_t *)tex_pixels;
            uint32_t *bottom_data = (uint32_t *)s_bottom_surface->pixels;
            int tex_stride = tex_pitch / 4;
            
            /* Simple nearest-neighbor scaling - blit zoomed region */
            for (int dy = 0; dy < BOTTOM_SCREEN_H; dy++) {
                for (int dx = 0; dx < BOTTOM_SCREEN_W; dx++) {
                    /* Map bottom screen pixel to game coordinates */
                    int src_x = view_x + (int)(dx * zoom_factor);
                    int src_y = view_y + (int)(dy * zoom_factor);
                    
                    /* Bounds check */
                    if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h) {
                        bottom_data[dy * BOTTOM_SCREEN_W + dx] = 
                            tex_data[src_y * tex_stride + src_x];
                    }
                }
            }
            
            if (SDL_MUSTLOCK(s_bottom_surface)) SDL_UnlockSurface(s_bottom_surface);
            SDL_UnlockTexture(s_game_texture);
            
            /* Draw crosshair at center */
            draw_crosshair(s_bottom_surface, BOTTOM_SCREEN_W / 2, BOTTOM_SCREEN_H / 2);
            
            /* TODO: Blit s_bottom_surface to actual 3DS bottom screen framebuffer
             * This requires:
             * 1. Get bottom framebuffer: gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, ...)
             * 2. Convert ARGB8888 to RGB565 (3DS framebuffer format)
             * 3. Handle screen rotation (3DS screens are physically rotated 90°)
             * 4. memcpy with proper stride
             * 
             * For now, bottom screen will show black but the surface is prepared. */
        }
    }
    
    /* Swap buffers and wait for VBlank */
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void plat_video_shutdown(void)
{
    if (s_bottom_surface) {
        SDL_FreeSurface(s_bottom_surface);
        s_bottom_surface = NULL;
    }
    
    if (s_game_texture) {
        SDL_DestroyTexture(s_game_texture);
        s_game_texture = NULL;
    }
    
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    
    gfxExit();
    
    LOG_INFO("3ds-video", "Video subsystem shut down");
}

void plat_video_toggle_fullscreen(void)
{
    /* No-op on 3DS - always fullscreen */
}

void plat_video_message_box(const char *title, const char *body)
{
    /* 3DS has no native message box - log to console/file instead */
    LOG_INFO("msgbox", "%s: %s", title, body);
}

void plat_apply_video_prefs(void)
{
    /* No configurable video preferences on 3DS */
}

/* Platform-specific: Get screen dimensions for mouse bounds */
void platform_video_get_present_state(int *stretch, int *win_w, int *win_h,
                                     int *fb_w, int *fb_h)
{
    if (stretch) *stretch = 1;
    if (win_w)   *win_w = TOP_SCREEN_W;
    if (win_h)   *win_h = TOP_SCREEN_H;
    if (fb_w)    *fb_w = GAME_W;
    if (fb_h)    *fb_h = GAME_H;
}
