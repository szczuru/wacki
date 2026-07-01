/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.c — SDL compatibility layer implementation for 3DS.
 *
 * Maps minimal SDL API surface to native 3DS APIs (citro3d/citro2d for graphics,
 * ndsp for audio, hidScanInput for events). Allows wacki engine core to compile
 * without modifications while rendering on dual 3DS screens. */

#include "SDL_compat.h"
#include <citro2d.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>
#include <malloc.h>

/* Forward declarations for functions that may not be in old libctru */
#ifndef gspWaitForPPF
#define gspWaitForPPF gspWaitForP3D
#endif

/* Global 3DS graphics state */
static C3D_RenderTarget *s_top_screen = NULL;
static C3D_RenderTarget *s_bottom_screen = NULL;
static int s_screen_width = 400;   /* Top screen width */
static int s_screen_height = 240;  /* Screen height */
static int s_initialized = 0;

/* Renderer and Window (dummy structures - we only need one) */
struct SDL_Renderer {
    int dummy;
    uint8_t draw_r, draw_g, draw_b, draw_a;
};

struct SDL_Window {
    int w, h;
};

static SDL_Renderer s_renderer_storage;
static SDL_Window s_window_storage;

/* Texture structure - maps to C3D texture */
struct SDL_Texture {
    C3D_Tex c3d_tex;
    C2D_Image c2d_img;
    int width;
    int height;
    uint32_t format;  /* SDL pixel format */
    int access;
    void *pixels_shadow;  /* For UpdateTexture */
    int pitch;
};

/* Audio state (stub - ndsp not implemented yet) */
static int s_audio_open = 0;

/* Timer state */
static uint64_t s_start_ticks = 0;

/* Error string */
static char s_error_buf[256] = "No error";

static void set_error(const char *msg)
{
    strncpy(s_error_buf, msg, sizeof(s_error_buf) - 1);
    s_error_buf[sizeof(s_error_buf) - 1] = '\0';
}

const char* SDL_GetError(void)
{
    return s_error_buf;
}

/* ---- SDL Init/Quit ---- */

int SDL_Init(uint32_t flags)
{
    if (s_initialized) return 0;
    
    /* Initialize graphics */
    if (flags & SDL_INIT_VIDEO) {
        gfxInitDefault();
        C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
        C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
        C2D_Prepare();
        
        s_top_screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        s_bottom_screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
        
        if (!s_top_screen || !s_bottom_screen) {
            set_error("Failed to create screen targets");
            return -1;
        }
    }
    
    /* Initialize audio (stub) */
    if (flags & SDL_INIT_AUDIO) {
        ndspInit();
    }
    
    /* Initialize timer */
    if (flags & SDL_INIT_TIMER) {
        s_start_ticks = osGetTime();
    }
    
    s_initialized = 1;
    return 0;
}

void SDL_Quit(void)
{
    if (!s_initialized) return;
    
    if (s_audio_open) {
        ndspExit();
        s_audio_open = 0;
    }
    
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    
    s_initialized = 0;
}

/* ---- SDL Hints ---- */

int SDL_SetHint(const char *name, const char *value)
{
    /* Ignore hints - we don't need them on 3DS */
    (void)name;
    (void)value;
    return 1;
}

/* ---- Window Management ---- */

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags)
{
    (void)title; (void)x; (void)y; (void)flags;
    
    s_window_storage.w = (w > 0) ? w : s_screen_width;
    s_window_storage.h = (h > 0) ? h : s_screen_height;
    
    return &s_window_storage;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    (void)window;
    /* Nothing to clean up - static storage */
}

/* ---- Renderer Management ---- */

SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags)
{
    (void)window; (void)index; (void)flags;
    
    memset(&s_renderer_storage, 0, sizeof(s_renderer_storage));
    s_renderer_storage.draw_r = 0;
    s_renderer_storage.draw_g = 0;
    s_renderer_storage.draw_b = 0;
    s_renderer_storage.draw_a = 255;
    
    return &s_renderer_storage;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    (void)renderer;
    /* Nothing to clean up */
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (!renderer) return -1;
    
    renderer->draw_r = r;
    renderer->draw_g = g;
    renderer->draw_b = b;
    renderer->draw_a = a;
    
    return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    if (!renderer) return -1;
    
    /* Clear both screens with the draw color */
    uint32_t clear_color = C2D_Color32(renderer->draw_r, renderer->draw_g, 
                                       renderer->draw_b, renderer->draw_a);
    
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    C2D_TargetClear(s_top_screen, clear_color);
    C2D_TargetClear(s_bottom_screen, clear_color);
    
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    (void)renderer;
    
    /* End the frame - this presents to both screens */
    C3D_FrameEnd(0);
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, 
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    if (!renderer || !texture) return -1;
    
    /* Render to top screen (main game view) */
    C2D_SceneBegin(s_top_screen);
    
    /* For now, just draw the full texture to top screen */
    /* TODO: Handle source/dest rectangles for proper scaling */
    C2D_DrawImageAt(texture->c2d_img, 0.0f, 0.0f, 0.5f);
    
    /* Render zoom view to bottom screen */
    C2D_SceneBegin(s_bottom_screen);
    
    /* Get zoom level from gamepad */
    extern int platform_3ds_get_zoom_level(void);
    int zoom = platform_3ds_get_zoom_level();
    
    /* Zoom levels: 0=100%, 1=50%, 2=25%, 3=12.5% */
    float zoom_factor = 1.0f / (1 << zoom);
    
    /* Bottom screen is 320x240 */
    int bottom_w = 320;
    int bottom_h = 240;
    
    /* Calculate source region for zoom (centered around cursor) */
    extern int16_t g_mouse_x, g_mouse_y;
    
    /* Scale cursor from 640x480 game space to 400x240 screen space */
    int screen_cursor_x = (g_mouse_x * 400) / 640;
    int screen_cursor_y = (g_mouse_y * 240) / 480;
    
    int zoom_src_w = (int)((float)bottom_w * zoom_factor);
    int zoom_src_h = (int)((float)bottom_h * zoom_factor);
    
    int zoom_src_x = screen_cursor_x - zoom_src_w / 2;
    int zoom_src_y = screen_cursor_y - zoom_src_h / 2;
    
    /* Clamp to texture bounds */
    if (zoom_src_x < 0) zoom_src_x = 0;
    if (zoom_src_y < 0) zoom_src_y = 0;
    if (zoom_src_x + zoom_src_w > 400) 
        zoom_src_x = 400 - zoom_src_w;
    if (zoom_src_y + zoom_src_h > 240) 
        zoom_src_y = 240 - zoom_src_h;
    
    /* Draw full texture to bottom with scale */
    float bottom_scale_x = (float)bottom_w / 400.0f;
    float bottom_scale_y = (float)bottom_h / 240.0f;
    
    /* Offset to center zoom area */
    float offset_x = -(float)zoom_src_x * (bottom_w / (float)zoom_src_w);
    float offset_y = -(float)zoom_src_y * (bottom_h / (float)zoom_src_h);
    
    /* Apply zoom scale */
    float zoom_scale = 1.0f / zoom_factor;
    
    C2D_DrawImageAt(texture->c2d_img, offset_x, offset_y, 0.5f, 
                    NULL, zoom_scale * bottom_scale_x, zoom_scale * bottom_scale_y);
    
    return 0;
}

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
    (void)renderer;
    (void)w;
    (void)h;
    /* 3DS handles scaling internally - ignore logical size */
    return 0;
}

/* ---- Texture Management ---- */

SDL_Texture* SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, 
                               int access, int w, int h)
{
    (void)renderer;
    
    SDL_Texture *tex = (SDL_Texture *)malloc(sizeof(SDL_Texture));
    if (!tex) {
        set_error("Out of memory");
        return NULL;
    }
    
    memset(tex, 0, sizeof(SDL_Texture));
    tex->width = w;
    tex->height = h;
    tex->format = format;
    tex->access = access;
    
    /* Determine C3D texture format - wacki uses ARGB8888 */
    GPU_TEXCOLOR c3d_format = GPU_RGBA8;  /* Always use RGBA8 for simplicity */
    int bytes_per_pixel = 4;  /* RGBA8 = 4 bytes per pixel */
    
    /* Calculate proper texture size (must be power of 2) */
    int tex_w = 64;
    while (tex_w < w) tex_w *= 2;
    int tex_h = 64;
    while (tex_h < h) tex_h *= 2;
    
    /* Create C3D texture */
    if (!C3D_TexInit(&tex->c3d_tex, tex_w, tex_h, c3d_format)) {
        free(tex);
        set_error("Failed to create C3D texture");
        return NULL;
    }
    
    C3D_TexSetFilter(&tex->c3d_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&tex->c3d_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    
    /* Create C2D image */
    tex->c2d_img.tex = &tex->c3d_tex;
    tex->c2d_img.subtex = (Tex3DS_SubTexture *)malloc(sizeof(Tex3DS_SubTexture));
    if (tex->c2d_img.subtex) {
        tex->c2d_img.subtex->width = w;
        tex->c2d_img.subtex->height = h;
        tex->c2d_img.subtex->left = 0.0f;
        tex->c2d_img.subtex->right = (float)w / (float)tex_w;
        tex->c2d_img.subtex->bottom = (float)h / (float)tex_h;
        tex->c2d_img.subtex->top = 0.0f;
    }
    
    /* Allocate shadow buffer for UpdateTexture */
    tex->pitch = w * bytes_per_pixel;
    tex->pixels_shadow = linearAlloc(tex->pitch * h);
    if (!tex->pixels_shadow) {
        C3D_TexDelete(&tex->c3d_tex);
        free((void *)tex->c2d_img.subtex);
        free(tex);
        set_error("Failed to allocate shadow buffer");
        return NULL;
    }
    
    return tex;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (!texture) return;
    
    if (texture->pixels_shadow) {
        linearFree(texture->pixels_shadow);
    }
    
    if (texture->c2d_img.subtex) {
        free((void *)texture->c2d_img.subtex);
    }
    
    C3D_TexDelete(&texture->c3d_tex);
    free(texture);
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, 
                      const void *pixels, int pitch)
{
    if (!texture || !pixels) return -1;
    
    /* For 3DS, we'll use a simpler approach: convert pixels to texture format
     * and upload directly. The engine uses ARGB8888 format. */
    
    int update_w = texture->width;
    int update_h = texture->height;
    
    if (rect) {
        /* Partial update not fully supported yet - just do full update */
        update_w = rect->w;
        update_h = rect->h;
    }
    
    /* The wacki engine sends us ARGB8888 data.
     * We need to convert it to a format 3DS understands.
     * For simplicity, we'll convert to RGBA8 which C3D supports. */
    
    const uint32_t *src = (const uint32_t *)pixels;
    
    /* Allocate temp buffer for converted data if needed */
    if (!texture->pixels_shadow) {
        return -1;
    }
    
    /* Convert ARGB8888 to RGBA8 (swap channels) */
    uint32_t *dst = (uint32_t *)texture->pixels_shadow;
    int src_pitch_pixels = pitch / 4;
    
    for (int y = 0; y < update_h && y < texture->height; y++) {
        for (int x = 0; x < update_w && x < texture->width; x++) {
            uint32_t argb = src[y * src_pitch_pixels + x];
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            
            /* Convert to RGBA8 */
            uint32_t rgba = (r << 24) | (g << 16) | (b << 8) | a;
            dst[y * texture->width + x] = rgba;
        }
    }
    
    /* Flush cache and upload to VRAM */
    GSPGPU_FlushDataCache(texture->pixels_shadow, 
                          texture->width * texture->height * 4);
    
    /* Use C3D_SyncDisplayTransfer to upload texture data */
    GX_DisplayTransfer((u32*)texture->pixels_shadow, 
                       GX_BUFFER_DIM(texture->width, texture->height),
                       (u32*)texture->c3d_tex.data, 
                       GX_BUFFER_DIM(texture->c3d_tex.width, texture->c3d_tex.height),
                       GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | 
                       GX_TRANSFER_RAW_COPY(0) |
                       GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | 
                       GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    
    gspWaitForPPF();
    
    return 0;
}

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
    (void)texture;
    (void)blendMode;
    /* 3DS handles blending automatically */
    return 0;
}

/* ---- Surface Management ---- */

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(uint32_t flags, int width, int height, 
                                            int depth, uint32_t format)
{
    (void)flags;
    (void)depth;
    
    SDL_Surface *surface = (SDL_Surface *)malloc(sizeof(SDL_Surface));
    if (!surface) {
        set_error("Out of memory");
        return NULL;
    }
    
    memset(surface, 0, sizeof(SDL_Surface));
    
    surface->format = (SDL_PixelFormat *)malloc(sizeof(SDL_PixelFormat));
    if (!surface->format) {
        free(surface);
        set_error("Out of memory");
        return NULL;
    }
    
    memset(surface->format, 0, sizeof(SDL_PixelFormat));
    surface->format->format = format;
    surface->format->BitsPerPixel = (format == SDL_PIXELFORMAT_INDEX8) ? 8
