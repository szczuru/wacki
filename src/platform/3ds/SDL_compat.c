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
    
    /* Wait for VBlank */
    gspWaitForVBlank();
}
 
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, 
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    if (!renderer || !texture) return -1;
    
    /* Render to top screen (main game view) */
    C2D_SceneBegin(s_top_screen);
    
    /* Source rectangle */
    int src_x = 0, src_y = 0;
    int src_w = texture->width;
    int src_h = texture->height;
    
    if (srcrect) {
        src_x = srcrect->x;
        src_y = srcrect->y;
        src_w = srcrect->w;
        src_h = srcrect->h;
    }
    
    /* Destination rectangle - scale to top screen */
    int dst_x = 0, dst_y = 0;
    int dst_w = s_screen_width;
    int dst_h = s_screen_height;
    
    if (dstrect) {
        dst_x = dstrect->x;
        dst_y = dstrect->y;
        dst_w = dstrect->w;
        dst_h = dstrect->h;
    }
    
    /* Calculate scaling factors */
    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    
    /* Draw to top screen */
    C2D_DrawImageAt(texture->c2d_img, (float)dst_x, (float)dst_y, 0.5f,
                    NULL, scale_x, scale_y);
    
    /* Draw zoomed view to bottom screen */
    C2D_SceneBegin(s_bottom_screen);
    
    /* Get zoom level from gamepad (extern function) */
    extern int platform_3ds_get_zoom_level(void);
    int zoom = platform_3ds_get_zoom_level();
    
    /* Zoom levels: 0=100%, 1=50%, 2=25%, 3=12.5% */
    float zoom_factor = 1.0f / (1 << zoom);  /* 1.0, 0.5, 0.25, 0.125 */
    
    /* Bottom screen is 320x240 */
    int bottom_w = 320;
    int bottom_h = 240;
    
    /* Calculate source region for zoom (centered around cursor) */
    extern int16_t g_mouse_x, g_mouse_y;  /* From engine globals */
    
    int zoom_src_w = (int)((float)bottom_w * zoom_factor);
    int zoom_src_h = (int)((float)bottom_h * zoom_factor);
    
    int zoom_src_x = g_mouse_x - zoom_src_w / 2;
    int zoom_src_y = g_mouse_y - zoom_src_h / 2;
    
    /* Clamp to texture bounds */
    if (zoom_src_x < 0) zoom_src_x = 0;
    if (zoom_src_y < 0) zoom_src_y = 0;
    if (zoom_src_x + zoom_src_w > texture->width) 
        zoom_src_x = texture->width - zoom_src_w;
    if (zoom_src_y + zoom_src_h > texture->height) 
        zoom_src_y = texture->height - zoom_src_h;
    
    /* Draw zoomed region to bottom screen */
    Tex3DS_SubTexture sub_tex = {
        (u16)zoom_src_w, (u16)zoom_src_h,
        (float)zoom_src_x / (float)texture->width,
        (float)(zoom_src_x + zoom_src_w) / (float)texture->width,
        (float)(zoom_src_y + zoom_src_h) / (float)texture->height,
        (float)zoom_src_y / (float)texture->height
    };
    
    C2D_Image zoom_img = {&texture->c3d_tex, &sub_tex};
    C2D_DrawImageAt(zoom_img, 0.0f, 0.0f, 0.5f, NULL, 
                    (float)bottom_w / (float)zoom_src_w, 
                    (float)bottom_h / (float)zoom_src_h);
    
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
    
    /* Determine C3D texture format */
    GPU_TEXCOLOR c3d_format;
    int bytes_per_pixel;
    
    if (format == SDL_PIXELFORMAT_RGB888) {
        c3d_format = GPU_RGB8;
        bytes_per_pixel = 3;
    } else if (format == SDL_PIXELFORMAT_INDEX8) {
        c3d_format = GPU_L8;  /* 8-bit luminance for indexed */
        bytes_per_pixel = 1;
    } else {
        c3d_format = GPU_RGBA8;
        bytes_per_pixel = 4;
    }
    
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
    
    /* Copy to shadow buffer */
    int update_x = 0, update_y = 0;
    int update_w = texture->width;
    int update_h = texture->height;
    
    if (rect) {
        update_x = rect->x;
        update_y = rect->y;
        update_w = rect->w;
        update_h = rect->h;
    }
    
    /* Handle ARGB8888 format conversion */
    if (texture->format == SDL_PIXELFORMAT_RGB888 || 
        texture->format == 0x16462004) {  /* SDL_PIXELFORMAT_ARGB8888 */
        
        /* Convert ARGB8888 to RGB565 for 3DS */
        uint16_t *dst = (uint16_t *)texture->pixels_shadow;
        const uint32_t *src = (const uint32_t *)pixels;
        
        for (int y = 0; y < update_h; y++) {
            for (int x = 0; x < update_w; x++) {
                uint32_t argb = src[y * (pitch / 4) + x];
                uint8_t r = (argb >> 16) & 0xFF;
                uint8_t g = (argb >> 8) & 0xFF;
                uint8_t b = argb & 0xFF;
                
                /* Convert to RGB565 */
                uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                dst[(y + update_y) * texture->width + (x + update_x)] = rgb565;
            }
        }
        
        /* Upload to VRAM - use RGBA5551 as closest match */
        GSPGPU_FlushDataCache(texture->pixels_shadow, 
                              texture->width * texture->height * 2);
        C3D_SyncDisplayTransfer((u32 *)texture->pixels_shadow, 
                               GX_BUFFER_DIM(texture->width, texture->height),
                               (u32 *)texture->c3d_tex.data, 
                               GX_BUFFER_DIM(texture->c3d_tex.width, texture->c3d_tex.height),
                               GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | 
                               GX_TRANSFER_RAW_COPY(0) |
                               GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | 
                               GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                               GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
        
    } else if (texture->format == SDL_PIXELFORMAT_INDEX8) {
        /* Direct copy for indexed textures */
        const uint8_t *src = (const uint8_t *)pixels;
        uint8_t *dst = (uint8_t *)texture->pixels_shadow;
        
        for (int y = 0; y < update_h; y++) {
            memcpy(dst + (y + update_y) * texture->width + update_x,
                   src + y * pitch,
                   update_w);
        }
        
        /* Upload to VRAM */
        GSPGPU_FlushDataCache(texture->pixels_shadow, 
                              texture->width * texture->height);
        C3D_SyncDisplayTransfer((u32 *)texture->pixels_shadow, 
                               GX_BUFFER_DIM(texture->width, texture->height),
                               (u32 *)texture->c3d_tex.data, 
                               GX_BUFFER_DIM(texture->c3d_tex.width, texture->c3d_tex.height),
                               GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | 
                               GX_TRANSFER_RAW_COPY(0) |
                               GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_L8) | 
                               GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_L8) |
                               GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    }
    
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
    surface->format->BitsPerPixel = (format == SDL_PIXELFORMAT_INDEX8) ? 8 : 32;
    surface->format->BytesPerPixel = surface->format->BitsPerPixel / 8;
    
    surface->w = width;
    surface->h = height;
    surface->pitch = width * surface->format->BytesPerPixel;
    
    surface->pixels = malloc(surface->pitch * height);
    if (!surface->pixels) {
        free(surface->format);
        free(surface);
        set_error("Out of memory");
        return NULL;
    }
    
    return surface;
}
 
void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    
    if (surface->pixels) free(surface->pixels);
    if (surface->format && surface->format->palette) {
        if (surface->format->palette->colors) {
            free(surface->format->palette->colors);
        }
        free(surface->format->palette);
    }
    if (surface->format) free(surface->format);
    free(surface);
}
 
SDL_Surface* SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    (void)src;
    (void)freesrc;
    set_error("SDL_LoadBMP_RW not implemented");
    return NULL;
}
 
int SDL_SetColorKey(SDL_Surface *surface, int flag, uint32_t key)
{
    (void)surface;
    (void)flag;
    (void)key;
    return 0;
}
 
/* ---- Event Handling ---- */
 
int SDL_PollEvent(SDL_Event *event)
{
    if (!event) return 0;
    
    /* 3DS uses native input polling in gamepad_3ds.c */
    /* Touch screen events are handled here for bottom screen */
    
    hidScanInput();
    
    touchPosition touch;
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    
    /* Check for touch on bottom screen */
    if (kDown & KEY_TOUCH) {
        hidTouchRead(&touch);
        event->type = SDL_FINGERDOWN;
        
        /* Map touch to cursor position (will be used by engine) */
        extern int16_t g_mouse_x, g_mouse_y;
        
        /* Get zoom level to map touch to game coordinates */
        extern int platform_3ds_get_zoom_level(void);
        int zoom = platform_3ds_get_zoom_level();
        float zoom_factor = 1.0f / (1 << zoom);
        
        /* Bottom screen is 320x240, map to zoomed game coordinates */
        int zoom_src_w = (int)(320.0f * zoom_factor);
        int zoom_src_h = (int)(240.0f * zoom_factor);
        
        /* Touch position relative to zoom window */
        float rel_x = (float)touch.px / 320.0f;
        float rel_y = (float)touch.py / 240.0f;
        
        /* Map to game coordinates */
        int zoom_src_x = g_mouse_x - zoom_src_w / 2;
        int zoom_src_y = g_mouse_y - zoom_src_h / 2;
        
        /* Clamp zoom source */
        if (zoom_src_x < 0) zoom_src_x = 0;
        if (zoom_src_y < 0) zoom_src_y = 0;
        
        /* Calculate absolute game coordinates */
        g_mouse_x = (int16_t)(zoom_src_x + (int)(rel_x * zoom_src_w));
        g_mouse_y = (int16_t)(zoom_src_y + (int)(rel_y * zoom_src_h));
        
        return 1;
    }
    
    /* No events */
    return 0;
}
 
/* ---- Timing ---- */
 
uint32_t SDL_GetTicks(void)
{
    uint64_t now = osGetTime();
    return (uint32_t)(now - s_start_ticks);
}
 
void SDL_Delay(uint32_t ms)
{
    svcSleepThread((s64)ms * 1000000LL);
}
 
SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_TimerCallback callback, void *param)
{
    /* Stub - timers not implemented */
    (void)interval;
    (void)callback;
    (void)param;
    return 0;
}
 
int SDL_RemoveTimer(SDL_TimerID id)
{
    (void)id;
    return 0;
}
 
/* ---- Text Input ---- */
 
void SDL_StartTextInput(void)
{
    /* Could use 3DS software keyboard - not implemented yet */
}
 
void SDL_StopTextInput(void)
{
    /* Stub */
}
 
/* ---- Audio Stubs ---- */
 
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (s_audio_open) return 0;
    
    /* Initialize ndsp */
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    
    if (obtained) {
        memcpy(obtained, desired, sizeof(SDL_AudioSpec));
    }
    
    s_audio_open = 1;
    return 0;
}
 
void SDL_CloseAudio(void)
{
    if (!s_audio_open) return;
    
    ndspExit();
    s_audio_open = 0;
}
 
void SDL_PauseAudio(int pause_on)
{
    /* Stub - would control ndsp playback */
    (void)pause_on;
}
