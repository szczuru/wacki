/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds_gl.c — 3DS video backend using NovaGL.
 *
 * Uses NovaGL (OpenGL ES 1.1 → citro3d) for hardware-accelerated rendering.
 * Dual-screen layout:
 * - Top screen (400x240): Main game view
 * - Bottom screen (320x240): Zoomed view around cursor */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"

#include <3ds.h>
#include <NovaGL.h>
#include <string.h>

#define TOP_SCREEN_W    400
#define TOP_SCREEN_H    240
#define BOTTOM_SCREEN_W 320
#define BOTTOM_SCREEN_H 240
#define GAME_W          640
#define GAME_H          480

static GLuint s_game_texture = 0;
static uint32_t s_game_pixels[GAME_W * GAME_H];

extern int platform_3ds_get_zoom_level(void);
extern int16_t g_mouse_x, g_mouse_y;

unsigned plat_video_sdl_init_flags(void)
{
    return 0;
}

int plat_video_init(int w, int h, const char *title)
{
    (void)title;
    
    LOG_INFO("3ds-video", "Initializing NovaGL (OpenGL ES 1.1 → citro3d)");
    
    gfxInitDefault();
    gfxSet3D(false);
    nova_init();
    
    LOG_INFO("3ds-video", "NovaGL initialized");
    
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    
    glGenTextures(1, &s_game_texture);
    glBindTexture(GL_TEXTURE_2D, s_game_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 GAME_W, GAME_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    LOG_INFO("3ds-video", "Created %dx%d game texture (GL tex id: %u)",
             GAME_W, GAME_H, s_game_texture);
    
    return 1;
}

static void draw_textured_quad(float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x,     y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x,     y + h);
    glEnd();
}

static void draw_crosshair(float x, float y)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 0.0f, 0.8f);
    
    float size = 6.0f;
    float thickness = 2.0f;
    
    glBegin(GL_QUADS);
    glVertex2f(x - size, y - thickness/2);
    glVertex2f(x + size, y - thickness/2);
    glVertex2f(x + size, y + thickness/2);
    glVertex2f(x - size, y + thickness/2);
    glEnd();
    
    glBegin(GL_QUADS);
    glVertex2f(x - thickness/2, y - size);
    glVertex2f(x + thickness/2, y - size);
    glVertex2f(x + thickness/2, y + size);
    glVertex2f(x - thickness/2, y + size);
    glEnd();
    
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!shadow || !pal) return;
    
    for (int y = 0; y < h; ++y) {
        const uint8_t *src = shadow + y * w;
        uint32_t *dst = s_game_pixels + y * GAME_W;
        
        for (int x = 0; x < w; ++x) {
            const uint8_t *rgb = pal + src[x] * 3;
            dst[x] = 0xFF000000u | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16);
        }
    }
    
    glBindTexture(GL_TEXTURE_2D, s_game_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_W, GAME_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_game_pixels);
    
    nova_set_render_target(0);
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, TOP_SCREEN_W, TOP_SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_game_texture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    
    float scale = 0.5f;
    float dst_w = GAME_W * scale;
    float dst_h = GAME_H * scale;
    float offset_x = (TOP_SCREEN_W - dst_w) / 2.0f;
    
    draw_textured_quad(offset_x, 0, dst_w, dst_h,
                      0.0f, 0.0f, 1.0f, 1.0f);
    
    nova_set_render_target(2);
    
    glClear(GL_COLOR_BUFFER_BIT);
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, BOTTOM_SCREEN_W, BOTTOM_SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    int zoom = platform_3ds_get_zoom_level();
    float zoom_factor = 1.0f / (float)(1 << zoom);
    
    int view_w = (int)(BOTTOM_SCREEN_W * zoom_factor);
    int view_h = (int)(BOTTOM_SCREEN_H * zoom_factor);
    
    int view_x = g_mouse_x - view_w / 2;
    int view_y = g_mouse_y - view_h / 2;
    
    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;
    if (view_x + view_w > GAME_W) view_x = GAME_W - view_w;
    if (view_y + view_h > GAME_H) view_y = GAME_H - view_h;
    
    float u0 = (float)view_x / GAME_W;
    float v0 = (float)view_y / GAME_H;
    float u1 = (float)(view_x + view_w) / GAME_W;
    float v1 = (float)(view_y + view_h) / GAME_H;
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_game_texture);
    
    draw_textured_quad(0, 0, BOTTOM_SCREEN_W, BOTTOM_SCREEN_H,
                      u0, v0, u1, v1);
    
    draw_crosshair(BOTTOM_SCREEN_W / 2.0f, BOTTOM_SCREEN_H / 2.0f);
    
    novaSwapBuffers();
}

void plat_video_shutdown(void)
{
    if (s_game_texture) {
        glDeleteTextures(1, &s_game_texture);
        s_game_texture = 0;
    }
    
    nova_fini();
    gfxExit();
    
    LOG_INFO("3ds-video", "NovaGL shutdown complete");
}

void plat_video_toggle_fullscreen(void) {}

void plat_video_message_box(const char *title, const char *body)
{
    LOG_INFO("msgbox", "%s: %s", title, body);
}

void plat_apply_video_prefs(void) {}

void platform_video_get_present_state(int *stretch, int *win_w, int *win_h,
                                     int *fb_w, int *fb_h)
{
    if (stretch) *stretch = 1;
    if (win_w)   *win_w = TOP_SCREEN_W;
    if (win_h)   *win_h = TOP_SCREEN_H;
    if (fb_w)    *fb_w = GAME_W;
    if (fb_h)    *fb_h = GAME_H;
}
