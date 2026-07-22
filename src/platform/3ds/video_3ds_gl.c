/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds_gl.c — dual-screen video HAL, direct
 * framebuffer writes (NO OpenGL / picaGL / citro3d).
 *
 * TOP SCREEN (400×240):    Main game rendering, full native resolution.
 * BOTTOM SCREEN (320×240): Zoomed view around cursor + touch input,
 *                          full native resolution.
 *
 * HISTORY / WHY NOT picaGL: an earlier version of this file used
 * picaGL (OpenGL ES 1.1 -> citro3d) — glTexImage2D/glTexSubImage2D to
 * upload each frame as a texture, then a textured quad + pglSwapBuffers
 * to present. That worked, but was slow (~9-12fps even with the
 * New3DS speedup enabled): picaGL's glTexSubImage2D does NOT do a
 * flat memcpy, it re-tiles the ENTIRE uploaded region into Morton
 * (swizzled) order, one pixel at a time, via an INDIRECT function-
 * pointer call per pixel (source/texture.c's _textureTile in picaGL,
 * confirmed against picaGL's real source) — expensive on the 3DS's
 * ARM11 (weak branch prediction for indirect calls), and that cost
 * scales with resolution. Reducing internal texture resolution helped
 * FPS but visibly hurt image quality; a "skip identical frames"
 * optimization on top of that only bought ~12fps back because this
 * game's frames are almost NEVER pixel-identical (ambient animation,
 * idle-cursor blink, etc.) — so the dirty-check rarely fires and the
 * expensive Morton-tiling path still runs on nearly every frame.
 *
 * This version sidesteps the whole GPU texture pipeline: the game's
 * source data is already a flat 8-bit indexed 2D image, so there is
 * no benefit to routing it through a 3D texture-mapping GPU pipeline
 * at all. Instead we do the same thing the simplest devkitPro
 * examples do (see 3ds-examples/graphics/bitmap/24bit-color) — write
 * pixels straight into the LCD's own framebuffer via
 * gfxGetFramebuffer(), converting each indexed pixel to RGB565 with a
 * single palette-lookup-and-pack, no GPU round-trip, no Morton tiling,
 * no intermediate RGBA buffer. This is dramatically cheaper per pixel
 * than the picaGL path, which is what makes full native resolution on
 * BOTH screens affordable again.
 *
 * SCREEN ROTATION: the 3DS's screens are physically portrait panels;
 * their framebuffers are stored ROTATED 90° from how you'd normally
 * think of them (libctru's own gfx.h: "the top screen is 240 pixels
 * wide and 400 pixels tall" — i.e. GSP_SCREEN_WIDTH=240 is the
 * FRAMEBUFFER's row length for BOTH screens, and visual X maps to
 * framebuffer COLUMNS, not rows). The standard, widely-documented
 * mapping from a normal-orientation visual pixel (x, y) — x in
 * [0, visual_width), y in [0, visual_height) — to a framebuffer pixel
 * INDEX (not yet a byte offset) is:
 *
 *     index = x * PHYS_ROW_LEN + (PHYS_ROW_LEN - 1 - y)
 *
 * where PHYS_ROW_LEN is always 240 (GSP_SCREEN_WIDTH) for both
 * screens — only visual_width/visual_height differ (400x240 top vs.
 * 320x240 bottom). See fb_pixel_index() below. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include <3ds.h>
#include <malloc.h>
#include <string.h>

#define TOP_WIDTH  400
#define TOP_HEIGHT 240
#define BOT_WIDTH  320
#define BOT_HEIGHT 240

/* GSP_SCREEN_WIDTH (3ds/services/gspgpu.h) — the framebuffer's row
 * length in memory for BOTH screens (240), independent of which
 * screen's VISUAL width (400 top / 320 bottom) we're addressing. See
 * this file's top comment for why this differs from the visual
 * width/height. */
#define PHYS_ROW_LEN 240

#define GAME_WIDTH  640
#define GAME_HEIGHT 480

/* Zoom levels for bottom screen */
static int g_zoom_level = 0; /* 0=1x, 1=2x, 2=4x */
static const int ZOOM_LEVELS[] = {1, 2, 4};
#define NUM_ZOOM_LEVELS 3

/* The exact source-image region the LAST bottom-screen (zoomed) frame
 * was extracted from — the top-left corner (s_zoom_src_x/y, in game-
 * surface pixels) and the zoom multiplier. Cached here (written once
 * per frame by plat_video_present, right before the bottom-screen
 * pixel loop runs) so platform_video_touch_to_game() can map a touch-
 * panel tap back through the SAME region instead of re-deriving its
 * own (potentially stale, or differently-clamped) copy of the center/
 * clamp math. */
static int s_zoom_src_x = 0, s_zoom_src_y = 0, s_zoom_mult = 1;

/* g_mouse_x / g_mouse_y — the shared cursor globals (wacki/globals.h,
 * pulled in via wacki.h above). gamepad_3ds.c's touch/circle-pad code
 * writes them; using a private g_cursor_x/y here would desync the
 * bottom-screen zoom center from where the engine actually thinks the
 * cursor is. */

/* Game framebuffer (8-bit indexed). s_shadow/s_palette double as the
 * PREVIOUS frame's content, for the dirty-frame skip in
 * plat_video_present below — still worth keeping even though the
 * per-pixel cost dropped a lot: any tick that's genuinely a no-op
 * (dialog wait, idle menu) still skips real work entirely. */
static uint8_t *s_shadow = NULL;
static uint8_t *s_palette = NULL;
static int s_fb_w = 0, s_fb_h = 0;
static int s_have_prev_frame = 0; /* 0 until the first plat_video_present call */
static int s_prev_mouse_x = -1, s_prev_mouse_y = -1;
static int s_prev_zoom_level = -1;

/* Pack one 8-bit-per-channel RGB triple into RGB565 (5 bits R, 6 bits
 * G, 5 bits B — matches gfxSetScreenFormat(..., GSP_RGB565_OES) set in
 * plat_video_init below). */
static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Visual-orientation pixel (x, y) -> framebuffer u16 index. See this
 * file's top comment for the rotation rationale; PHYS_ROW_LEN (240)
 * is the same constant for both screens, only the caller's x range
 * (visual width) differs. */
static inline uint32_t fb_pixel_index(int x, int y)
{
    return (uint32_t)x * PHYS_ROW_LEN + (uint32_t)(PHYS_ROW_LEN - 1 - y);
}

/* Convert + downscale the full indexed game frame directly into the
 * top screen's live framebuffer (nearest-neighbor), one pixel at a
 * time: palette lookup -> RGB565 pack -> rotated-offset write. No
 * intermediate buffer, no GPU texture upload — see this file's top
 * comment for why this is so much cheaper than the picaGL path it
 * replaces. */
static void blit_top_screen(const uint8_t *indexed, const uint8_t *pal,
                            uint16_t *fb, int src_w, int src_h)
{
    float x_ratio = (float)src_w / TOP_WIDTH;
    float y_ratio = (float)src_h / TOP_HEIGHT;
    for (int dy = 0; dy < TOP_HEIGHT; ++dy) {
        int sy = (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        uint32_t base = fb_pixel_index(0, dy);
        for (int dx = 0; dx < TOP_WIDTH; ++dx) {
            int sx = (int)(dx * x_ratio);
            if (sx >= src_w) sx = src_w - 1;
            const uint8_t *e = pal + row[sx] * 3;
            /* fb_pixel_index(dx, dy) - fb_pixel_index(0, dy) == dx * PHYS_ROW_LEN
             * exactly (the y-dependent term is constant across dx), so
             * this is the same index fb_pixel_index(dx, dy) would give,
             * computed incrementally instead of re-deriving it every
             * pixel. */
            fb[base + (uint32_t)dx * PHYS_ROW_LEN] = pack_rgb565(e[0], e[1], e[2]);
        }
    }
}

/* Same idea as blit_top_screen, but samples a `zoom`-times magnified
 * crop of the source (src_x/src_y top-left corner, src_region_w/h
 * span) instead of the whole image — the bottom screen's "magnifier"
 * view around the cursor. */
static void blit_bottom_screen_zoom(const uint8_t *indexed, const uint8_t *pal,
                                    uint16_t *fb, int src_w, int src_h,
                                    int src_x, int src_y,
                                    int src_region_w, int src_region_h)
{
    float x_ratio = (float)src_region_w / BOT_WIDTH;
    float y_ratio = (float)src_region_h / BOT_HEIGHT;
    for (int dy = 0; dy < BOT_HEIGHT; ++dy) {
        int sy = src_y + (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        uint32_t base = fb_pixel_index(0, dy);
        for (int dx = 0; dx < BOT_WIDTH; ++dx) {
            int sx = src_x + (int)(dx * x_ratio);
            if (sx >= src_w) sx = src_w - 1;
            const uint8_t *e = pal + row[sx] * 3;
            fb[base + (uint32_t)dx * PHYS_ROW_LEN] = pack_rgb565(e[0], e[1], e[2]);
        }
    }
}

void platform_video_cycle_zoom(void)
{
    g_zoom_level = (g_zoom_level + 1) % NUM_ZOOM_LEVELS;
    LOG_INFO("3ds", "zoom level: %dx", ZOOM_LEVELS[g_zoom_level]);
}

/* Map a touch-panel tap (tx,ty in the 320x240 BOT_WIDTH/BOT_HEIGHT
 * panel space) to game-surface coordinates, by inverting the EXACT
 * region blit_bottom_screen_zoom drew into the bottom screen on the
 * most recent plat_video_present call (cached in s_zoom_src_x/y/mult).
 *
 * A tap at panel pixel (tx,ty) maps to
 * (src_x + tx/zoom, src_y + ty/zoom) — the same forward transform
 * blit_bottom_screen_zoom used, run backwards. */
void platform_video_touch_to_game(int tx, int ty, int *gx, int *gy)
{
    int zoom = s_zoom_mult > 0 ? s_zoom_mult : 1;
    int rx = tx / zoom;
    int ry = ty / zoom;
    int x = s_zoom_src_x + rx;
    int y = s_zoom_src_y + ry;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > s_fb_w - 1) x = s_fb_w - 1;
    if (y > s_fb_h - 1) y = s_fb_h - 1;

    *gx = x;
    *gy = y;
}

void platform_video_get_present_state(int *stretch_active,
                                      int *win_w, int *win_h,
                                      int *fb_w,  int *fb_h)
{
    *stretch_active = 0;
    *win_w = TOP_WIDTH;
    *win_h = TOP_HEIGHT;
    *fb_w = s_fb_w;
    *fb_h = s_fb_h;
}

void platform_video_toggle_aspect_mode(void)
{
    /* Not applicable on 3DS - fixed screens */
}

/* wacki/platform/video.h HAL entry point — PlatformShowMessageBox routes
 * here. No native dialog on 3DS; log it (visible over 3dslink's console
 * relay / Citra's stdout) so a fatal-path message isn't silently lost. */
void plat_video_message_box(const char *title, const char *body)
{
    LOG_INFO("msgbox", "%s: %s", title ? title : "", body ? body : "");
}

int plat_video_init(int w, int h, const char *title)
{
    (void)title;
    plat_apply_video_prefs();

    s_fb_w = w;
    s_fb_h = h;

    /* Allocate buffers */
    s_shadow = (uint8_t *)linearAlloc(w * h);
    s_palette = (uint8_t *)linearAlloc(256 * 3);

    if (!s_shadow || !s_palette) {
        LOG_INFO("3ds", "Failed to allocate video buffers");
        return 0;
    }

    memset(s_shadow, 0, w * h);
    memset(s_palette, 0, 256 * 3);

    /* RGB565: 2 bytes/pixel, plenty of headroom for an 8-bit indexed
     * source palette, and half the memory traffic of RGBA8 for every
     * write in blit_top_screen/blit_bottom_screen_zoom above. No
     * picaGL/citro3d init needed at all — we write straight into the
     * LCD's own framebuffer memory (gfxGetFramebuffer), never touch
     * the GPU's 3D pipeline. */
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    /* Double buffering stays enabled (gfxInitDefault's default) so we
     * always write into the buffer NOT currently being scanned out to
     * the LCD — gfxGetFramebuffer returns that back buffer's address,
     * and gfxSwapBuffers() (called at the end of plat_video_present)
     * flips it in at the next VBlank. */

    LOG_INFO("3ds", "direct-framebuffer video initialized: %dx%d game -> top=%dx%d bot=%dx%d",
             w, h, TOP_WIDTH, TOP_HEIGHT, BOT_WIDTH, BOT_HEIGHT);

    return 1;
}

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!shadow || !pal) return;

    /* ---- Dirty-frame check --------------------------------------- *
     *
     * Cheap insurance: skip the pixel loops entirely on any tick whose
     * shadow+palette are byte-for-byte identical to the previous one
     * (dialog waits, idle menus). Most ticks in this game DO change
     * something (ambient animation, cursor blink), so this won't fire
     * every frame, but it's still a free win on the ticks where it
     * does — and unlike the previous picaGL-based version, the "real
     * work" this lets us skip is now cheap enough per-pixel that this
     * check is a minor bonus rather than the main optimization. */
    int top_dirty = !s_have_prev_frame ||
                    memcmp(s_shadow, shadow, (size_t)w * h) != 0 ||
                    memcmp(s_palette, pal, 256 * 3) != 0;

    int zoom = ZOOM_LEVELS[g_zoom_level];
    int bot_dirty = top_dirty ||
                    g_mouse_x != s_prev_mouse_x ||
                    g_mouse_y != s_prev_mouse_y ||
                    g_zoom_level != s_prev_zoom_level;

    if (!top_dirty && !bot_dirty) return;

    memcpy(s_shadow, shadow, (size_t)w * h);
    memcpy(s_palette, pal, 256 * 3);
    s_have_prev_frame = 1;
    s_prev_mouse_x = g_mouse_x;
    s_prev_mouse_y = g_mouse_y;
    s_prev_zoom_level = g_zoom_level;

    /* Bottom screen's zoom-crop source region is computed unconditionally
     * (cheap integer math) even when only the top screen is dirty, so
     * platform_video_touch_to_game always has an up-to-date region to
     * invert against — touch input must keep working even on frames
     * where the bottom screen's PIXELS didn't need re-drawing. */
    int cx = g_mouse_x;
    int cy = g_mouse_y;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= w) cx = w - 1;
    if (cy >= h) cy = h - 1;

    int src_region_w = BOT_WIDTH / zoom;
    int src_region_h = BOT_HEIGHT / zoom;
    int src_x = cx - src_region_w / 2;
    int src_y = cy - src_region_h / 2;
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x + src_region_w > w) src_x = w - src_region_w;
    if (src_y + src_region_h > h) src_y = h - src_region_h;
    if (src_x < 0) src_x = 0; /* region wider than source (zoom<1 edge case) */
    if (src_y < 0) src_y = 0;

    s_zoom_src_x = src_x;
    s_zoom_src_y = src_y;
    s_zoom_mult  = zoom;

    if (top_dirty) {
        uint16_t *topfb = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        blit_top_screen(shadow, pal, topfb, w, h);
    }

    if (bot_dirty) {
        uint16_t *botfb = (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        blit_bottom_screen_zoom(shadow, pal, botfb, w, h,
                                src_x, src_y, src_region_w, src_region_h);
    }

    /* gfxFlushBuffers(): flush the data cache for whichever
     * framebuffer(s) we just wrote via CPU stores above — required
     * before the GSP (a separate processor) reads them for display;
     * see libctru's own docs: "preferred to call this only once per
     * frame, after all software rendering is completed." */
    gfxFlushBuffers();
    /* gfxSwapBuffers(): commit + flip both screens' framebuffers at
     * the next VBlank (equivalent to gfxScreenSwapBuffers for both
     * GFX_TOP and GFX_BOTTOM). Calling it even when only one screen
     * was dirty is harmless — the untouched screen's "swap" just
     * re-presents the same buffer it was already showing. */
    gfxSwapBuffers();
}

void plat_video_shutdown(void)
{
    if (s_shadow) linearFree(s_shadow);
    if (s_palette) linearFree(s_palette);

    s_shadow = NULL;
    s_palette = NULL;
}
