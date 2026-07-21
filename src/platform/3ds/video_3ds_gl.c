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

/* Convert 8-bit indexed to RGBA8888 */
static void indexed_to_rgba(const uint8_t *indexed, const uint8_t *pal,
                            uint32_t *rgba, int w, int h)
{
    for (int i = 0; i < w * h; ++i) {
        const uint8_t *e = pal + indexed[i] * 3;
        rgba[i] = 0xFF000000u | (e[0] << 16) | (e[1] << 8) | e[2];
    }
}

/* Extract zoomed region around cursor */
static void extract_zoom_region(const uint32_t *src, int src_w, int src_h,
                               uint32_t *dst, int dst_w, int dst_h,
                               int cx, int cy, int zoom)
{
    /* Source region size */
    int src_region_w = dst_w / zoom;
    int src_region_h = dst_h / zoom;

    /* Center around cursor */
    int src_x = cx - src_region_w / 2;
    int src_y = cy - src_region_h / 2;

    /* Clamp to source bounds */
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x + src_region_w > src_w) src_x = src_w - src_region_w;
    if (src_y + src_region_h > src_h) src_y = src_h - src_region_h;

    /* Nearest-neighbor upscale */
    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = src_y + dy / zoom;
        if (sy >= src_h) sy = src_h - 1;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = src_x + dx / zoom;
            if (sx >= src_w) sx = src_w - 1;
            dst[dy * dst_w + dx] = src[sy * src_w + sx];
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

    /* Convert full frame to RGBA */
    static uint32_t *full_rgba = NULL;
    if (!full_rgba) full_rgba = (uint32_t *)linearAlloc(GAME_WIDTH * GAME_HEIGHT * 4);
    if (!full_rgba) return;

    indexed_to_rgba(shadow, pal, full_rgba, w, h);

    /* --- TOP SCREEN: Downscaled game (640x480 -> 400x240) --- */
    /* Simple nearest-neighbor downscale */
    float x_ratio = (float)w / TOP_WIDTH;
    float y_ratio = (float)h / TOP_HEIGHT;
    for (int y = 0; y < TOP_HEIGHT; ++y) {
        int sy = (int)(y * y_ratio);
        if (sy >= h) sy = h - 1;
        for (int x = 0; x < TOP_WIDTH; ++x) {
            int sx = (int)(x * x_ratio);
            if (sx >= w) sx = w - 1;
            s_top_rgba[y * TOP_WIDTH + x] = full_rgba[sy * w + sx];
        }
    }

    /* --- BOTTOM SCREEN: Zoomed region around cursor --- */
    int zoom = ZOOM_LEVELS[g_zoom_level];
    int cx = g_mouse_x;
    int cy = g_mouse_y;

    /* Clamp cursor to game bounds */
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= w) cx = w - 1;
    if (cy >= h) cy = h - 1;

    extract_zoom_region(full_rgba, w, h, s_bot_rgba, BOT_WIDTH, BOT_HEIGHT,
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
    glClear(GL_COLOR_BUFFER_BIT);
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
    glClear(GL_COLOR_BUFFER_BIT);
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
