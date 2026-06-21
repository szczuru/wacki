/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/sdl/video_sdl.c — video-output HAL, SDL backend.
 *
 * Desktop + handheld implementation of plat_video_* (wacki/platform/video.h):
 * an SDL window + accelerated renderer + streaming ARGB8888 texture. The
 * engine's 8-bpp shadow buffer is expanded through the palette LUT straight
 * into the locked texture each frame. The PS2 backend (gsKit hardware-palette
 * present) lives in src/platform/ps2/video_ps2.c.
 *
 * The cross-platform input/event pump + the public Platform* entry points
 * stay in src/platform/sdl/platform_sdl.c, which calls into this HAL.
 *
 * g_aspect_mode ("stretch" | "4:3") — added for fixed-panel handhelds whose
 * screen isn't 4:3 (Nintendo Switch's 16:9 panel being the motivating case;
 * harmless/no visual difference on a display that's already ~4:3, like most
 * other handhelds here). See apply_aspect_mode() below for why "stretch"
 * needs to disable SDL_RenderSetLogicalSize entirely rather than just
 * passing it a different size — SDL's own docs guarantee that function
 * always preserves the logical aspect ratio (letterboxing on a mismatch),
 * never disproportionately stretches, so genuine edge-to-edge fill has to
 * bypass it and blit with an explicit full-window destination rect instead. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include "sdl_internal.h"           /* platform_video_toggle_aspect_mode + the
                                     * present-state getter platform_sdl.c's
                                     * mouse/touch handlers need */
#ifdef __APPLE__
#include "wacki/platform/macos.h"   /* PlatformSetupMacMenu */
#endif
#ifdef __ANDROID__
#include "wacki/platform/android_touch.h"   /* on-screen touch overlay */
#endif

#include <SDL.h>
#include <stdint.h>
#include <string.h>

/* ARGB8888 pixel layout (matches SDL_PIXELFORMAT_ARGB8888): alpha is the top
 * byte, then R / G / B descending. */
#define ARGB_BYTES_PER_PIXEL        4
#define ARGB_ALPHA_OPAQUE_SHIFTED   (0xFFu << 24)
#define ARGB_R_SHIFT                16
#define ARGB_G_SHIFT                8

/* Mirrors the 3-byte RGB entries in the .PAL file. */
#define PALETTE_BYTES_PER_ENTRY     3

/* Default scale factor when --scale wasn't given (1× = native 640×480). */
#define DEFAULT_SCALE_FACTOR        1

static SDL_Window   *s_win = NULL;
static SDL_Renderer *s_ren = NULL;
static SDL_Texture  *s_tex = NULL;
static uint32_t      s_pixels32[640 * 480];
static int            s_fb_w = 0, s_fb_h = 0;   /* the engine's framebuffer size */

/* "stretch" (default) or "4:3" — persisted via wacki.cfg (config.c) like
 * every other display knob. Writable buffer (not a string literal) so
 * ConfigLoad can overwrite it via sscanf. */
char g_aspect_mode[16] = "stretch";

/* Mirrors g_aspect_mode for the present()/event-scaling hot path, set by
 * apply_aspect_mode() whenever g_aspect_mode changes. */
static int g_stretch_active = 0;

/* See the file header comment for why "stretch" can't just be a different
 * SDL_RenderSetLogicalSize argument. No-op on Android, which manages its
 * own non-4:3-fitting overlay layout and never reads g_aspect_mode. */
static void apply_aspect_mode(int w, int h)
{
    if (!s_ren) return;
#ifdef __ANDROID__
    (void)w; (void)h;
    return;
#else
    if (g_aspect_mode[0] == '4') {
        g_stretch_active = 0;
        SDL_RenderSetLogicalSize(s_ren, w, h);
    } else {
        g_stretch_active = 1;
        SDL_RenderSetLogicalSize(s_ren, 0, 0); /* 0,0 = disable, per SDL docs */
    }
#endif
}

/* sdl_internal.h: lets platform_sdl.c's mouse/touch handlers know whether
 * logical-size scaling is currently active. SDL only auto-rescales
 * SDL_MOUSEMOTION / SDL_FINGER* coordinates into framebuffer space when
 * logical-size scaling is ON ("4:3" mode here); in "stretch" mode it's
 * disabled (see apply_aspect_mode above), so the event pump has to do that
 * scaling itself using the window size this returns. */
void platform_video_get_present_state(int *stretch_active, int *win_w, int *win_h,
                                      int *fb_w, int *fb_h)
{
    *stretch_active = g_stretch_active;
    *fb_w = s_fb_w;
    *fb_h = s_fb_h;
    if (s_win) {
        SDL_GetWindowSize(s_win, win_w, win_h);
    } else {
        *win_w = s_fb_w;
        *win_h = s_fb_h;
    }
}

unsigned plat_video_sdl_init_flags(void)
{
    return SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO;
}

int plat_video_init(int w, int h, const char *title)
{
    /* Platform display prefs, before any sizing (it sets g_fullscreen /
     * g_scale_factor that the sizing below reads): on the desktop this shows
     * the first-run mode picker (fullscreen / window / scale); PortMaster /
     * Switch force fullscreen for their WM-less panels; Miyoo / PS2 no-op. */
    plat_apply_video_prefs();

    s_fb_w = w;
    s_fb_h = h;

    /* T54 — HiDPI scaling. The framebuffer stays w×h; the SDL window can be
     * enlarged Nx and SDL_RenderSetLogicalSize handles the upscale via
     * SDL_HINT_RENDER_SCALE_QUALITY. */
    int sf    = g_scale_factor > 0 ? g_scale_factor : DEFAULT_SCALE_FACTOR;
    int win_w = w * sf;
    int win_h = h * sf;
    if (g_scale_mode && *g_scale_mode) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, g_scale_mode);
    }

    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (g_fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    s_win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, win_flags);
    if (!s_win) {
        LOG_INFO("log", "SDL_CreateWindow: %s", SDL_GetError());
        return 0;
    }

    s_ren = SDL_CreateRenderer(s_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren) {
        /* Try software fallback before bailing. */
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_ren) {
        LOG_INFO("log", "SDL_CreateRenderer: %s", SDL_GetError());
        return 0;
    }

    /* NOTE: Android skips logical-size on purpose. The touch overlay manages the
     * full window itself (game in a centred rect, controls in the side panels)
     * so the panels are touchable — SDL's letterbox maps emulator touch to the
     * 4:3 canvas and saturates the bars, making them unreachable. */
#ifndef __ANDROID__
    apply_aspect_mode(w, h);
#endif
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!s_tex) {
        LOG_INFO("log", "SDL_CreateTexture: %s", SDL_GetError());
        return 0;
    }

    /* Embedded window icon — the 64×64 BMP baked into the binary. SDL scales
     * it for whatever surface the window manager asks for. Failure is
     * non-fatal; falls back to the SDL2 default cross. */
    extern unsigned char wacki_icon_bmp[];
    extern unsigned int  wacki_icon_bmp_len;
    SDL_RWops *icon_rw = SDL_RWFromConstMem(wacki_icon_bmp,
                                            (int)wacki_icon_bmp_len);
    if (icon_rw) {
        SDL_Surface *icon = SDL_LoadBMP_RW(icon_rw, 1 /* freesrc */);
        if (icon) {
            SDL_SetWindowIcon(s_win, icon);
            SDL_FreeSurface(icon);
        } else {
            LOG_INFO("platform", "SDL_LoadBMP_RW (icon): %s", SDL_GetError());
        }
    }

#ifdef __APPLE__
    /* Polish-localise SDL's stock menu bar AND add a "Gra" menu (defined in
     * platform/macos/macos.m, linked on Darwin desktop builds only). The menu
     * exists by the time SDL_CreateWindow has returned. */
    PlatformSetupMacMenu();
#endif

    /* T31 v2 — hide the OS cursor; PaintCursor draws the engine's own sprite.
     * PlatformPumpEvents re-asserts SDL_DISABLE every poll (defeats Cocoa
     * restoring the arrow on focus changes). */
    SDL_ShowCursor(SDL_DISABLE);

    const char *drv = SDL_GetCurrentVideoDriver();
    LOG_INFO("platform", "SDL ready: %dx%d window (%dx scale, %s filter, fullscreen=%d, aspect=%s), renderer=%s",
             win_w, win_h, sf, g_scale_mode ? g_scale_mode : "nearest",
             g_fullscreen, g_aspect_mode, drv ? drv : "?");

    /* Black initial frame so the window is never garbage. */
    memset(s_pixels32, 0, (size_t)w * h * ARGB_BYTES_PER_PIXEL);
    SDL_UpdateTexture(s_tex, NULL, s_pixels32, w * ARGB_BYTES_PER_PIXEL);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
    SDL_PumpEvents();
    return 1;
}

void plat_video_present(const uint8_t *shadow, const uint8_t *palette_rgb,
                        int w, int h)
{
    if (!s_tex || !shadow || !palette_rgb) return;

    /* SDL_LockTexture maps the streaming texture into our address space so we
     * expand the 8-bpp shadow + palette LUT straight into its backing memory —
     * one fewer 1.2 MB memcpy per frame than SDL_UpdateTexture. Falls back if
     * Lock fails so we still get a frame on screen. */
    void *pixels = NULL;
    int   pitch  = 0;
    if (SDL_LockTexture(s_tex, NULL, &pixels, &pitch) == 0 && pixels) {
        uint32_t *out        = (uint32_t *)pixels;
        int       row_stride = pitch / ARGB_BYTES_PER_PIXEL;
        for (int y = 0; y < h; ++y) {
            uint32_t      *row     = out + (size_t)y * row_stride;
            const uint8_t *src_row = shadow + (size_t)y * w;
            for (int x = 0; x < w; ++x) {
                uint8_t        idx = src_row[x];
                const uint8_t *e   = palette_rgb + idx * PALETTE_BYTES_PER_ENTRY;
                row[x] = ARGB_ALPHA_OPAQUE_SHIFTED
                       | ((uint32_t)e[0] << ARGB_R_SHIFT)
                       | ((uint32_t)e[1] << ARGB_G_SHIFT)
                       |  (uint32_t)e[2];
            }
        }
        SDL_UnlockTexture(s_tex);
    } else {
        int n   = w * h;
        int cap = (int)(sizeof s_pixels32 / sizeof s_pixels32[0]);
        if (n > cap) n = cap;
        for (int i = 0; i < n; ++i) {
            uint8_t        idx = shadow[i];
            const uint8_t *e   = palette_rgb + idx * PALETTE_BYTES_PER_ENTRY;
            s_pixels32[i] = ARGB_ALPHA_OPAQUE_SHIFTED
                          | ((uint32_t)e[0] << ARGB_R_SHIFT)
                          | ((uint32_t)e[1] << ARGB_G_SHIFT)
                          |  (uint32_t)e[2];
        }
        SDL_UpdateTexture(s_tex, NULL, s_pixels32, w * ARGB_BYTES_PER_PIXEL);
    }
    /* Side panels = the clear colour; force black so nothing (e.g. the Android
     * overlay's white fill) leaves a stray colour the next clear would smear. */
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
#ifdef __ANDROID__
    /* Full-window layout: game in a centred 4:3 rect, controls in side panels. */
    wacki_overlay_compute_layout(s_ren);
    SDL_Rect game_rect = wacki_overlay_game_rect();
    if (game_rect.w > 0 && game_rect.h > 0)
        SDL_RenderCopy(s_ren, s_tex, NULL, &game_rect);
    else
        SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    wacki_overlay_draw(s_ren);
#else
    if (g_stretch_active && s_win) {
        /* Logical-size scaling is off (see apply_aspect_mode) — a NULL dest
         * rect would only fill a w×h patch in the corner of the real window,
         * so stretch to the full window explicitly. This is the entire
         * point of "stretch" mode: edge-to-edge fill, disproportionate on a
         * non-4:3 display. */
        int win_w = w, win_h = h;
        SDL_GetWindowSize(s_win, &win_w, &win_h);
        SDL_Rect dest = { 0, 0, win_w, win_h };
        SDL_RenderCopy(s_ren, s_tex, NULL, &dest);
    } else {
        SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    }
#endif
    SDL_RenderPresent(s_ren);
}

void plat_video_shutdown(void)
{
    if (s_tex) { SDL_DestroyTexture (s_tex); s_tex = NULL; }
    if (s_ren) { SDL_DestroyRenderer(s_ren); s_ren = NULL; }
    if (s_win) { SDL_DestroyWindow  (s_win); s_win = NULL; }
}

/* F11 / macOS-menu fullscreen toggle. Reachable only on the desktop in
 * practice (handhelds map no button to SDLK_F11, and have no menu), so it
 * needs no platform guard — on a handheld it's simply never called. */
void plat_video_toggle_fullscreen(void)
{
    if (!s_win) return;
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(s_win,
        g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    LOG_INFO("platform", "fullscreen=%d", g_fullscreen);
    extern void ConfigSave(void);
    ConfigSave();
}

/* sdl_internal.h — flip g_aspect_mode "stretch" ↔ "4:3" at runtime and
 * persist the choice. Wired to the X button (gamepad_sdl.c) and a desktop
 * key (platform_sdl.c). No-op on Android (see apply_aspect_mode). */
void platform_video_toggle_aspect_mode(void)
{
    if (g_aspect_mode[0] == '4') {
        strncpy(g_aspect_mode, "stretch", sizeof g_aspect_mode - 1);
    } else {
        strncpy(g_aspect_mode, "4:3", sizeof g_aspect_mode - 1);
    }
    g_aspect_mode[sizeof g_aspect_mode - 1] = '\0';
    apply_aspect_mode(s_fb_w, s_fb_h);
    LOG_INFO("platform", "aspect_mode=%s", g_aspect_mode);
    extern void ConfigSave(void);
    ConfigSave();
}

void plat_video_message_box(const char *title, const char *body)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, body, s_win);
}
