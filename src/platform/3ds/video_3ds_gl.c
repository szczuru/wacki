/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds_gl.c — dual-screen video HAL using picaGL.
 *
 * TOP SCREEN (400×240):    Main game rendering
 * BOTTOM SCREEN (320×240): Zoomed view around cursor + touch input
 *
 * picaGL provides OpenGL ES 1.1 API mapped to citro3d. We use:
 *   - pglInit() / pglExit() for initialization
 *   - pglSelectScreen() to switch between GFX_TOP and GFX_BOTTOM
 *   - glTexImage2D() for uploading 8-bit indexed textures
 *   - glDrawArrays() for rendering quads
 *   - pglSwapBuffers() to present both screens
 *
 * Touch input is handled in gamepad_3ds.c and translated to mouse coordinates.
 * X button cycles zoom level (1x, 2x, 4x). */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include <3ds.h>
#include <malloc.h>
#include <string.h>

/* picaGL: pglInit/pglExit/pglSwapBuffers/pglSelectScreen + the GL 1.1
 * entry points used below all come from this single header (it itself
 * pulls in <GL/gl.h>). Installed to /opt/devkitpro/picaGL/include by
 * tools/build-3ds.sh; see mk/3ds.mk's -I. */
#include <GL/picaGL.h>

#define TOP_WIDTH  400
#define TOP_HEIGHT 240
#define BOT_WIDTH  320
#define BOT_HEIGHT 240

/* Internal render-buffer / texture resolution for the top screen.
 * Kept at FULL TOP_WIDTH/TOP_HEIGHT (1:1, no GPU upscale) — an earlier
 * pass tried halving this like BOT_TEX_WIDTH/HEIGHT below, but the
 * softer/blurrier main game view was a net loss for how the game
 * actually looks on real hardware. The FPS win that reduction bought
 * is recovered instead via the dirty-frame skip below, which avoids
 * ~all of the same per-pixel conversion + Morton-tiling cost on any
 * frame where the top screen's content didn't actually change (very
 * common in a point-and-click adventure: dialog waits, static rooms
 * with the cursor idle, menus) WITHOUT touching image quality at all
 * on the frames that do need a redraw. */
#define TOP_TEX_WIDTH  TOP_WIDTH
#define TOP_TEX_HEIGHT TOP_HEIGHT

/* Internal render-buffer / texture resolution for the bottom screen.
 * Deliberately HALF of the on-screen BOT_WIDTH/BOT_HEIGHT — the GPU's
 * texture sampler stretches whatever we upload to fill the full quad
 * (glTexCoord spans 0..1 regardless of the texture's actual pixel
 * dimensions), so this is a free way to cut the bottom screen's
 * per-frame cost.
 *
 * WHY THIS MATTERS (confirmed against picaGL's real source,
 * source/texture.c's _textureTile): glTexSubImage2D does NOT do a
 * flat memcpy. It re-tiles the WHOLE uploaded region into Morton
 * (swizzled) order, one pixel at a time, via a readPixel()/writePixel()
 * INDIRECT function-pointer call per pixel — expensive on the 3DS's
 * ARM11 (weak branch prediction for indirect calls). This runs every
 * frame for both screens; halving the bottom screen's texture
 * dimensions in each axis cuts its share of that cost to 1/4 (160x120
 * = 19200 px vs. 320x240 = 76800 px), which is the dominant per-frame
 * CPU cost on this platform — bigger than the New3DS clock bump alone
 * (osSetSpeedupEnable only affects CPU clock, not the GPU/GSP transfer
 * side of glTexSubImage2D, which is why that fix alone only took FPS
 * from ~9 to ~11-12 rather than tripling it).
 *
 * The top screen (the main game view) is intentionally NOT reduced
 * this way — it's the primary, full-detail view where blurring text/
 * hotspots would visibly hurt a point-and-click adventure. The bottom
 * screen is a magnifier/touch-target preview, so a softer upscale
 * (GL_LINEAR, set below) is an acceptable trade for the 4x CPU saving. */
#define BOT_TEX_WIDTH  160
#define BOT_TEX_HEIGHT 120

#define GAME_WIDTH  640
#define GAME_HEIGHT 480

/* Zoom levels for bottom screen */
static int g_zoom_level = 0; /* 0=1x, 1=2x, 2=4x */
static const int ZOOM_LEVELS[] = {1, 2, 4};
#define NUM_ZOOM_LEVELS 3

/* The exact source-image region the LAST bottom-screen (zoomed) frame
 * was extracted from — the top-left corner (s_zoom_src_x/y, in game-
 * surface pixels) and the zoom multiplier. Cached here (written once
 * per frame by plat_video_present, right before indexed_to_rgba_zoom
 * runs) so platform_video_touch_to_game() can map a touch-panel tap
 * back through the SAME region instead of re-deriving its own
 * (potentially stale, by one frame, or simply differently-clamped)
 * copy of the center/clamp math. See indexed_to_rgba_zoom's comment
 * for why this one-source-of-truth requirement matters. */
static int s_zoom_src_x = 0, s_zoom_src_y = 0, s_zoom_mult = 1;

/* g_mouse_x / g_mouse_y — the shared cursor globals (wacki/globals.h,
 * pulled in via wacki.h above). gamepad_3ds.c's touch/circle-pad code
 * writes them; using a private g_cursor_x/y here would desync the
 * bottom-screen zoom center from where the engine actually thinks the
 * cursor is. */

/* Game framebuffer (8-bit indexed). s_shadow/s_palette double as the
 * PREVIOUS frame's content — see the dirty-frame check in
 * plat_video_present below. */
static uint8_t *s_shadow = NULL;
static uint8_t *s_palette = NULL;
static int s_fb_w = 0, s_fb_h = 0;
static int s_have_prev_frame = 0; /* 0 until the first plat_video_present call */
static int s_prev_mouse_x = -1, s_prev_mouse_y = -1;
static int s_prev_zoom_level = -1;

/* RGBA textures for OpenGL */
static uint32_t *s_top_rgba = NULL;
static uint32_t *s_bot_rgba = NULL;
static GLuint s_top_tex = 0;
static GLuint s_bot_tex = 0;

/* Pack one indexed pixel to the byte order picaGL's glTexImage2D(GL_RGBA,
 * GL_UNSIGNED_BYTE) actually expects.
 *
 * Confirmed against picaGL's real source (source/texture_conv.inc):
 * _readRGBA8() does a bare `*(uint32_t*)data` (no byte reordering at
 * all), and _writeRGBA4() then reads that same value back through
 * `uint8_t *clr = (uint8_t*)&color` and treats clr[0] as R, clr[1] as
 * G, clr[2] as B, clr[3] as A. On little-endian ARM, clr[0] is the
 * LEAST-significant byte of the uint32_t — so the value must be built
 * as R | (G<<8) | (B<<16) | (A<<24), i.e. memory byte order R,G,B,A.
 * The previous code built A<<24 | R<<16 | G<<8 | B (memory order
 * B,G,R,A) — R and B ended up swapped in every pixel, which is exactly
 * the "colors look like a negative" symptom reported. */
static inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | (0xFFu << 24);
}

/* Convert + downscale the full indexed game frame directly into a
 * dst_w x dst_h RGBA buffer (nearest-neighbor). Folding the palette
 * lookup and the resize into one pass — instead of building a full
 * 640x480 RGBA intermediate and resizing THAT — cuts out a whole
 * 640x480 pass of per-pixel work every frame. */
static void indexed_to_rgba_scaled(const uint8_t *indexed, const uint8_t *pal,
                                   uint32_t *dst, int dst_w, int dst_h,
                                   int src_w, int src_h)
{
    float x_ratio = (float)src_w / dst_w;
    float y_ratio = (float)src_h / dst_h;
    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = (int)(dx * x_ratio);
            if (sx >= src_w) sx = src_w - 1;
            const uint8_t *e = pal + row[sx] * 3;
            dst[dy * dst_w + dx] = pack_rgba(e[0], e[1], e[2]);
        }
    }
}

/* Convert + extract the zoomed region around the cursor directly into
 * a dst_w x dst_h RGBA buffer — same fold-palette-lookup-into-the-
 * resize-pass rationale as indexed_to_rgba_scaled above.
 *
 * Takes the already-clamped top-left corner of the source region
 * (src_x, src_y) rather than re-deriving it from (cx, cy) itself: the
 * caller (plat_video_present) computes that clamp ONCE and caches it
 * (see s_zoom_src_x/y/mult below) so platform_video_touch_to_game can
 * invert the EXACT SAME region a touch lands on — duplicating the
 * clamp math in two places let them drift out of sync, which was the
 * root cause of touches on the bottom (zoomed) screen resolving to
 * coordinates that matched the top (full) screen instead.
 *
 * Takes the source region's SIZE (src_region_w/h — the number of
 * game-surface pixels the crop spans, i.e. what defines the zoom
 * level the player perceives on screen) as an explicit ratio input,
 * separate from dst_w/dst_h (how many texels we render it at). This
 * is what lets BOT_TEX_WIDTH/HEIGHT be smaller than the crop's actual
 * pixel span (see BOT_TEX_WIDTH's comment above) without silently
 * shrinking the crop — and therefore changing the on-screen zoom
 * level — as a side effect. Ratio math mirrors
 * indexed_to_rgba_scaled's x_ratio/y_ratio exactly. */
static void indexed_to_rgba_zoom(const uint8_t *indexed, const uint8_t *pal,
                                 uint32_t *dst, int dst_w, int dst_h,
                                 int src_w, int src_h,
                                 int src_x, int src_y,
                                 int src_region_w, int src_region_h)
{
    float x_ratio = (float)src_region_w / dst_w;
    float y_ratio = (float)src_region_h / dst_h;
    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = src_y + (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = src_x + (int)(dx * x_ratio);
            if (sx >= src_w) sx = src_w - 1;
            const uint8_t *e = pal + row[sx] * 3;
            dst[dy * dst_w + dx] = pack_rgba(e[0], e[1], e[2]);
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
 * region indexed_to_rgba_zoom drew into the bottom screen on the most
 * recent plat_video_present call (cached in s_zoom_src_x/y/mult).
 *
 * This is the fix for: "touching the bottom (zoomed) screen picks up
 * a position as if it came from the top (full-image) screen" — the
 * previous gamepad_3ds.c touch handler assumed the bottom screen was
 * a flat 2x scale of the WHOLE game surface (matching the old, wrong,
 * "bottom = shrunk full image" design), but the bottom screen is
 * actually a `zoom`-times magnified crop of a small region around the
 * cursor. A tap at panel pixel (tx,ty) must map to
 * (src_x + tx/zoom, src_y + ty/zoom) — the same forward transform
 * indexed_to_rgba_zoom used, run backwards. */
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
    s_top_rgba = (uint32_t *)linearAlloc(TOP_TEX_WIDTH * TOP_TEX_HEIGHT * 4);
    s_bot_rgba = (uint32_t *)linearAlloc(BOT_TEX_WIDTH * BOT_TEX_HEIGHT * 4);

    if (!s_shadow || !s_palette || !s_top_rgba || !s_bot_rgba) {
        LOG_INFO("3ds", "Failed to allocate video buffers");
        return 0;
    }

    memset(s_shadow, 0, w * h);
    memset(s_palette, 0, 256 * 3);

    /* Initialize picaGL */
    pglInit();

    /* Create top screen texture */
    pglSelectScreen(GFX_TOP, GFX_LEFT);
    glGenTextures(1, &s_top_tex);
    glBindTexture(GL_TEXTURE_2D, s_top_tex);
    /* NEAREST: TOP_TEX_WIDTH/HEIGHT == TOP_WIDTH/HEIGHT (1:1, no GPU
     * upscale needed), so there's no upscale seam for LINEAR to
     * soften — NEAREST keeps the game's original pixel-art crispness
     * intact. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TOP_TEX_WIDTH, TOP_TEX_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_top_rgba);

    /* Create bottom screen texture */
    pglSelectScreen(GFX_BOTTOM, GFX_LEFT);
    glGenTextures(1, &s_bot_tex);
    glBindTexture(GL_TEXTURE_2D, s_bot_tex);
    /* LINEAR (not NEAREST): the GPU upscales BOT_TEX_WIDTH/HEIGHT ->
     * BOT_WIDTH/HEIGHT (2x in each axis) to fill the physical bottom
     * screen; LINEAR softens that upscale instead of showing blocky
     * nearest-neighbor doubling on top of the zoom crop's own
     * nearest-neighbor magnification in indexed_to_rgba_zoom. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BOT_TEX_WIDTH, BOT_TEX_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_bot_rgba);

    /* Setup OpenGL state. picaGL only ships the double-precision
     * glOrtho (no GLES-style glOrthof) — see include/GL/gl.h. */
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    LOG_INFO("3ds", "picaGL initialized: %dx%d game -> top=%dx%d bot=%dx%d",
             w, h, TOP_WIDTH, TOP_HEIGHT, BOT_WIDTH, BOT_HEIGHT);

    return 1;
}

/* Draw the standard full-viewport textured quad + present the
 * currently-selected screen. Shared by both the top and bottom
 * screen's "content changed, do the real work" path below. */
static void draw_fullscreen_quad_and_swap(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(1, 1); glVertex2f(1, 0);
    glTexCoord2f(0, 1); glVertex2f(0, 0);
    glEnd();

    pglSwapBuffers();
}

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!shadow || !pal || !s_top_rgba || !s_bot_rgba) return;

    /* ---- Dirty-frame check --------------------------------------- *
     *
     * The engine (src/scene/frame_tick.c/graphics.c) calls this every
     * single game tick unconditionally -- there's no "did anything
     * actually change" flag upstream (graphics.c's own comment: "we
     * redraw the whole shadow each frame"). But in a point-and-click
     * adventure most ticks genuinely produce IDENTICAL pixels: waiting
     * on a dialog line, an idle cursor in a static room, sitting in a
     * menu. picaGL's glTexSubImage2D is expensive (see BOT_TEX_WIDTH's
     * comment: per-pixel Morton tiling via indirect function-pointer
     * calls, not a memcpy) -- redoing that plus the palette-lookup
     * resize pass for a frame that's pixel-for-pixel identical to the
     * one already on screen is pure waste.
     *
     * s_shadow/s_palette already hold a copy of the PREVIOUS frame
     * (kept for exactly this reason now -- previously copied but never
     * compared). A plain memcmp against the incoming shadow/pal is
     * cheap relative to the conversion+tiling work it lets us skip,
     * and correctly catches cursor movement too: PaintCursor() (called
     * from frame_tick.c's paint_frame()) draws the cursor sprite
     * directly INTO the shadow buffer before FlushFrameToPrimary, so a
     * moved cursor shows up as changed shadow bytes like any other
     * sprite would.
     *
     * If NEITHER screen's content changed, skip everything (no GL
     * calls at all -- both screens simply keep showing their last
     * presented frame, which is still correct since we never cleared
     * either color buffer). If only one screen needs work (e.g. the
     * bottom screen's zoom crop moved because the cursor moved, but
     * that same cursor movement means the top screen's shadow ALSO
     * changed -- see above, so in practice top and bottom are dirty
     * together whenever the cursor moves) each screen's own dirty flag
     * still gates its own conversion+upload+draw+swap independently. */
    int top_dirty = !s_have_prev_frame ||
                    memcmp(s_shadow, shadow, (size_t)w * h) != 0 ||
                    memcmp(s_palette, pal, 256 * 3) != 0;

    int zoom = ZOOM_LEVELS[g_zoom_level];
    int bot_dirty = top_dirty ||
                    g_mouse_x != s_prev_mouse_x ||
                    g_mouse_y != s_prev_mouse_y ||
                    g_zoom_level != s_prev_zoom_level;

    if (!top_dirty && !bot_dirty) return;

    /* Copy shadow buffer and palette — becomes "previous frame" for
     * next call's comparison above. */
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

    /* --- Render + present TOP screen (only if dirty) ---
     * CRITICAL: pglSwapBuffers() only flushes/transfers the buffer for
     * whichever screen was most recently selected via pglSelectScreen()
     * (see picaGL source/misc.c pglSwapBuffers: it branches on
     * pglState->display and does a SINGLE GX_DisplayTransfer for that
     * one screen only — it is NOT a "present both screens" call).
     * Calling it once at the very end (after selecting BOTTOM last)
     * meant TOP never got transferred (blank top screen) and BOTTOM
     * read back a stale/offset region of the shared color buffer
     * (the "zoom" garbage). Each screen's render + swap must be fully
     * completed before switching to the other screen. */
    if (top_dirty) {
        /* Build the RGBA buffer directly from the indexed source in one
         * pass (no intermediate 640x480 RGBA buffer, no separate resize
         * pass — see indexed_to_rgba_scaled above). This alone removes
         * an entire extra 640x480 palette-lookup pass every frame, which
         * on the 3DS's single ARM11 core at 268MHz was a meaningful
         * fraction of the ~5fps originally seen. */
        indexed_to_rgba_scaled(shadow, pal, s_top_rgba, TOP_TEX_WIDTH, TOP_TEX_HEIGHT, w, h);

        pglSelectScreen(GFX_TOP, GFX_LEFT);
        /* picaGL's default viewport (set once, at pglInit time, in
         * _stateDefault) is hardcoded to 400x240 for whichever screen
         * was selected THEN. It is NOT re-derived per pglSelectScreen
         * call, so the bottom screen (320px wide) must explicitly
         * reassert its own viewport/scissor every frame or picaGL
         * renders it using the top screen's 400-wide viewport, producing
         * exactly the kind of horizontal squeeze/garbage distortion seen
         * on real hardware. */
        glViewport(0, 0, TOP_WIDTH, TOP_HEIGHT);
        glScissor(0, 0, TOP_WIDTH, TOP_HEIGHT);
        /* No glClear() here: the quad below covers the entire viewport
         * with opaque texels every frame, so clearing first is pure
         * wasted GPU work — picaGL's glClear (source/misc.c) is a full
         * draw call with a shader swap (clearShader in, basicShader
         * back out), not a cheap register write, and doing that twice
         * per frame (once per screen) for no visible effect was a
         * second real contributor to the ~5fps originally seen. */
        glBindTexture(GL_TEXTURE_2D, s_top_tex);
        /* Must match s_top_rgba's actual allocated size (TOP_TEX_WIDTH x
         * TOP_TEX_HEIGHT), not the physical TOP_WIDTH/HEIGHT — same
         * overflow hazard as the bottom screen's glTexSubImage2D below. */
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TOP_TEX_WIDTH, TOP_TEX_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, s_top_rgba);

        draw_fullscreen_quad_and_swap();
    }

    /* --- Render + present BOTTOM screen (only if dirty) --- */
    if (bot_dirty) {
        /* Build at BOT_TEX_WIDTH/HEIGHT (the reduced internal
         * resolution), not the physical BOT_WIDTH/HEIGHT — the GPU
         * upscales the uploaded texture to fill the full-size quad.
         * Passing src_region_w/h (sized against BOT_WIDTH/BOT_HEIGHT/
         * zoom, i.e. the on-screen zoom level) separately from dst_w/
         * dst_h keeps the perceived zoom identical to what it'd be at
         * full BOT_WIDTH/HEIGHT texture resolution — only the texel
         * density (and thus sharpness + CPU cost) drops, not the crop
         * size. */
        indexed_to_rgba_zoom(shadow, pal, s_bot_rgba, BOT_TEX_WIDTH, BOT_TEX_HEIGHT, w, h,
                            src_x, src_y, src_region_w, src_region_h);

        pglSelectScreen(GFX_BOTTOM, GFX_LEFT);
        /* Viewport/scissor stay at the PHYSICAL screen size (BOT_WIDTH x
         * BOT_HEIGHT) — that's the actual display area the quad below
         * covers, unrelated to the reduced BOT_TEX_WIDTH/HEIGHT texture
         * resolution being uploaded into it (the GPU's texture sampler
         * upscales to fill whatever viewport is active, per glTexCoord's
         * 0..1 span). */
        glViewport(0, 0, BOT_WIDTH, BOT_HEIGHT);
        glScissor(0, 0, BOT_WIDTH, BOT_HEIGHT);
        glBindTexture(GL_TEXTURE_2D, s_bot_tex);
        /* Must match s_bot_rgba's actual allocated size (BOT_TEX_WIDTH x
         * BOT_TEX_HEIGHT) — passing BOT_WIDTH/BOT_HEIGHT here would have
         * picaGL's _textureTile read past the end of a buffer that's
         * only 160x120 texels, corrupting heap memory every frame. */
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BOT_TEX_WIDTH, BOT_TEX_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, s_bot_rgba);

        draw_fullscreen_quad_and_swap();
    }
}

void plat_video_shutdown(void)
{
    if (s_top_tex) glDeleteTextures(1, &s_top_tex);
    if (s_bot_tex) glDeleteTextures(1, &s_bot_tex);
    
    pglExit();

    if (s_shadow) linearFree(s_shadow);
    if (s_palette) linearFree(s_palette);
    if (s_top_rgba) linearFree(s_top_rgba);
    if (s_bot_rgba) linearFree(s_bot_rgba);

    s_shadow = NULL;
    s_palette = NULL;
    s_top_rgba = NULL;
    s_bot_rgba = NULL;
}
