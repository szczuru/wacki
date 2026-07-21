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

#define GAME_WIDTH  640
#define GAME_HEIGHT 480

/* Zoom levels for bottom screen */
static int g_zoom_level = 0; /* 0=1x, 1=2x, 2=4x */
static const int ZOOM_LEVELS[] = {1, 2, 4};
#define NUM_ZOOM_LEVELS 3

/* g_mouse_x / g_mouse_y — the shared cursor globals (wacki/globals.h,
 * pulled in via wacki.h above). gamepad_3ds.c's touch/circle-pad code
 * writes them; using a private g_cursor_x/y here would desync the
 * bottom-screen zoom center from where the engine actually thinks the
 * cursor is. */

/* Game framebuffer (8-bit indexed) */
static uint8_t *s_shadow = NULL;
static uint8_t *s_palette = NULL;
static int s_fb_w = 0, s_fb_h = 0;

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
 * resize-pass rationale as indexed_to_rgba_scaled above. */
static void indexed_to_rgba_zoom(const uint8_t *indexed, const uint8_t *pal,
                                 uint32_t *dst, int dst_w, int dst_h,
                                 int src_w, int src_h,
                                 int cx, int cy, int zoom)
{
    int src_region_w = dst_w / zoom;
    int src_region_h = dst_h / zoom;

    int src_x = cx - src_region_w / 2;
    int src_y = cy - src_region_h / 2;

    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x + src_region_w > src_w) src_x = src_w - src_region_w;
    if (src_y + src_region_h > src_h) src_y = src_h - src_region_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = src_y + dy / zoom;
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = src_x + dx / zoom;
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
    s_top_rgba = (uint32_t *)linearAlloc(TOP_WIDTH * TOP_HEIGHT * 4);
    s_bot_rgba = (uint32_t *)linearAlloc(BOT_WIDTH * BOT_HEIGHT * 4);

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TOP_WIDTH, TOP_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_top_rgba);

    /* Create bottom screen texture */
    pglSelectScreen(GFX_BOTTOM, GFX_LEFT);
    glGenTextures(1, &s_bot_tex);
    glBindTexture(GL_TEXTURE_2D, s_bot_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BOT_WIDTH, BOT_HEIGHT, 0,
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

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!shadow || !pal || !s_top_rgba || !s_bot_rgba) return;

    /* Copy shadow buffer and palette */
    memcpy(s_shadow, shadow, w * h);
    memcpy(s_palette, pal, 256 * 3);

    /* Build each screen's RGBA buffer directly from the indexed source
     * in one pass each (no intermediate 640x480 RGBA buffer, no
     * separate resize pass — see indexed_to_rgba_scaled/_zoom above).
     * This alone removes an entire extra 640x480 palette-lookup pass
     * every frame, which on the 3DS's single ARM11 core at 268MHz was
     * a meaningful fraction of the ~5fps seen. */
    indexed_to_rgba_scaled(shadow, pal, s_top_rgba, TOP_WIDTH, TOP_HEIGHT, w, h);

    int zoom = ZOOM_LEVELS[g_zoom_level];
    int cx = g_mouse_x;
    int cy = g_mouse_y;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= w) cx = w - 1;
    if (cy >= h) cy = h - 1;
    indexed_to_rgba_zoom(shadow, pal, s_bot_rgba, BOT_WIDTH, BOT_HEIGHT, w, h,
                        cx, cy, zoom);

    /* --- Render + present TOP screen ---
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
    pglSelectScreen(GFX_TOP, GFX_LEFT);
    /* picaGL's default viewport (set once, at pglInit time, in
     * _stateDefault) is hardcoded to 400x240 for whichever screen was
     * selected THEN. It is NOT re-derived per pglSelectScreen call, so
     * the bottom screen (320px wide) must explicitly reassert its own
     * viewport/scissor every frame or picaGL renders it using the top
     * screen's 400-wide viewport, producing exactly the kind of
     * horizontal squeeze/garbage distortion seen on real hardware. */
    glViewport(0, 0, TOP_WIDTH, TOP_HEIGHT);
    glScissor(0, 0, TOP_WIDTH, TOP_HEIGHT);
    /* No glClear() here: the quad below covers the entire viewport
     * with opaque texels every frame, so clearing first is pure wasted
     * GPU work — picaGL's glClear (source/misc.c) is a full draw call
     * with a shader swap (clearShader in, basicShader back out), not
     * a cheap register write, and doing that twice per frame (once per
     * screen) for no visible effect was a second real contributor to
     * the ~5fps seen on top of the extra RGBA conversion pass above. */
    glBindTexture(GL_TEXTURE_2D, s_top_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TOP_WIDTH, TOP_HEIGHT,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_top_rgba);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(1, 1); glVertex2f(1, 0);
    glTexCoord2f(0, 1); glVertex2f(0, 0);
    glEnd();

    pglSwapBuffers();

    /* --- Render + present BOTTOM screen --- */
    pglSelectScreen(GFX_BOTTOM, GFX_LEFT);
    glViewport(0, 0, BOT_WIDTH, BOT_HEIGHT);
    glScissor(0, 0, BOT_WIDTH, BOT_HEIGHT);
    glBindTexture(GL_TEXTURE_2D, s_bot_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BOT_WIDTH, BOT_HEIGHT,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_bot_rgba);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(1, 1); glVertex2f(1, 0);
    glTexCoord2f(0, 1); glVertex2f(0, 0);
    glEnd();

    pglSwapBuffers();
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
