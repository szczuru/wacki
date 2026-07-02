/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.c — SDL compatibility layer implementation for 3DS.
 *
 * Maps minimal SDL API surface to native 3DS APIs (citro3d/citro2d for graphics,
 * ndsp for audio, hidScanInput for events). Allows wacki engine core to compile
 * without modifications while rendering on dual 3DS screens. */

#include "SDL_compat.h"
#include "wacki/log.h"
#include <citro2d.h>
#include <citro3d.h>
#include <tex3ds.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declarations */
static void update_audio_buffers(void);

/* Forward declarations for functions that may not be in old libctru */
#ifndef gspWaitForPPF
#define gspWaitForPPF gspWaitForP3D
#endif

/* Global 3DS graphics state */
static C3D_RenderTarget *s_top_screen = NULL;
static C3D_RenderTarget *s_bottom_screen = NULL;
static int s_screen_width = 400;   /* Top screen logical width */
static int s_screen_height = 240;  /* Top screen logical height */
static int s_initialized = 0;

/* 3DS screen hardware:
 * - Physical screens are rotated 90° (portrait mode in hardware)
 * - citro2d handles this via GX_TRANSFER automatically
 * - Top screen: 400x240 logical (landscape)
 * - Bottom screen: 320x240 logical (landscape)
 * We use logical coordinates throughout */

/* Renderer and Window structures */
struct SDL_Renderer {
    int dummy;
    uint8_t draw_r, draw_g, draw_b, draw_a;
};

/* Window structure defined in SDL_compat.h */

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

/* Audio state */
#define NDSP_CHANNEL 0
#define AUDIO_BUFFER_COUNT 2

static ndspWaveBuf s_wave_bufs[AUDIO_BUFFER_COUNT];
static int s_audio_open = 0;
static SDL_AudioCallback s_audio_callback = NULL;
static void *s_audio_userdata = NULL;
static int16_t *s_audio_buffer[AUDIO_BUFFER_COUNT];
static int s_audio_buffer_size = 0;
static int s_current_buffer = 0;

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
    
    LOG_INFO("3ds-init", "SDL_Init called with flags=0x%08X", flags);
    
    /* Initialize graphics */
    if (flags & SDL_INIT_VIDEO) {
        LOG_INFO("3ds-init", "Initializing video subsystem...");
        gfxInitDefault();
        C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
        C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
        C2D_Prepare();
        
        /* Create screen targets - GFX_LEFT is the 2D monocular view */
        s_top_screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        s_bottom_screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
        
        if (!s_top_screen || !s_bottom_screen) {
            LOG_INFO("3ds-init", "FAILED to create screen targets!");
            set_error("Failed to create screen targets");
            return -1;
        }
        
        LOG_INFO("3ds-init", "Video initialized successfully");
        
        /* Enable gyro for potential motion controls */
        HIDUSER_EnableGyroscope();
    }
    
    /* Initialize audio */
    if (flags & SDL_INIT_AUDIO) {
        LOG_INFO("3ds-init", "Audio init deferred to SDL_OpenAudio");
    }
    
    /* Initialize timer */
    if (flags & SDL_INIT_TIMER) {
        s_start_ticks = osGetTime();
        LOG_INFO("3ds-init", "Timer initialized");
    }
    
    s_initialized = 1;
    LOG_INFO("3ds-init", "SDL_Init completed successfully");
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
    
    /* Start frame ONCE at beginning of render */
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
    
    /* Wait for VBlank to prevent tearing and maintain 60fps timing */
    gspWaitForVBlank();
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, 
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    if (!renderer || !texture) {
        return -1;
    }
    
    /* ===== TOP SCREEN: Main game view ===== 
     * Top screen: 400x240 logical
     * Game: 640x480
     * Scale to fit maintaining aspect ratio */
    
    C2D_SceneBegin(s_top_screen);
    
    /* Scale game (640x480) to fit screen (400x240) */
    /* Use uniform scale of 0.5 to fit 480 height into 240 */
    /* This gives us 320x240, centered horizontally */
    float scale = 0.5f;
    float offset_x = (400.0f - 320.0f) / 2.0f;  /* 40px black bars on sides */
    
    /* Draw main game view */
    C2D_DrawImageAt(texture->c2d_img, offset_x, 0.0f, 0.5f, 
                    NULL, scale, scale);
    
    /* ===== BOTTOM SCREEN: Zoom view ===== 
     * Bottom screen: 320x240 logical */
    
    C2D_SceneBegin(s_bottom_screen);
    
    /* Get zoom level and cursor position */
    extern int platform_3ds_get_zoom_level(void);
    extern int16_t g_mouse_x, g_mouse_y;
    int zoom = platform_3ds_get_zoom_level();
    float zoom_scale = (float)(1 << zoom);
    
    /* Calculate view region in game coordinates (640x480) */
    float view_w = 320.0f / zoom_scale;
    float view_h = 240.0f / zoom_scale;
    
    float view_x = (float)g_mouse_x - view_w / 2.0f;
    float view_y = (float)g_mouse_y - view_h / 2.0f;
    
    /* Clamp to game bounds */
    if (view_x < 0.0f) view_x = 0.0f;
    if (view_y < 0.0f) view_y = 0.0f;
    if (view_x + view_w > 640.0f) view_x = 640.0f - view_w;
    if (view_y + view_h > 480.0f) view_y = 480.0f - view_h;
    
    /* Calculate texture coordinates for zoom region */
    const Tex3DS_SubTexture *subtex = texture->c2d_img.subtex;
    float tex_w = subtex->right - subtex->left;
    float tex_h = subtex->bottom - subtex->top;
    
    float u0 = subtex->left + (view_x / 640.0f) * tex_w;
    float v0 = subtex->top + (view_y / 480.0f) * tex_h;
    float u1 = subtex->left + ((view_x + view_w) / 640.0f) * tex_w;
    float v1 = subtex->top + ((view_y + view_h) / 480.0f) * tex_h;
    
    /* Create temporary subtex for zoomed region */
    Tex3DS_SubTexture zoom_subtex = *subtex;
    *(float*)&zoom_subtex.left = u0;
    *(float*)&zoom_subtex.top = v0;
    *(float*)&zoom_subtex.right = u1;
    *(float*)&zoom_subtex.bottom = v1;
    *(u16*)&zoom_subtex.width = (u16)view_w;
    *(u16*)&zoom_subtex.height = (u16)view_h;
    
    C2D_Image zoom_img = texture->c2d_img;
    zoom_img.subtex = &zoom_subtex;
    
    /* Draw zoomed region filling bottom screen */
    C2D_DrawImageAt(zoom_img, 0.0f, 0.0f, 0.5f, 
                    NULL, zoom_scale, zoom_scale);
    
    /* Draw cursor crosshair in center of zoom view */
    float cursor_screen_x = ((float)g_mouse_x - view_x) * zoom_scale;
    float cursor_screen_y = ((float)g_mouse_y - view_y) * zoom_scale;
    
    u32 cursor_color = C2D_Color32(255, 255, 0, 200);
    float crosshair_size = 6.0f;
    float thickness = 2.0f;
    
    /* Horizontal line */
    C2D_DrawRectSolid(cursor_screen_x - crosshair_size, cursor_screen_y - thickness/2.0f,
                     0.6f, crosshair_size * 2.0f, thickness, cursor_color);
    /* Vertical line */
    C2D_DrawRectSolid(cursor_screen_x - thickness/2.0f, cursor_screen_y - crosshair_size,
                     0.6f, thickness, crosshair_size * 2.0f, cursor_color);
    
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
    
    /* For 3DS performance, use exact size (not power of 2) if possible
     * New 3DS supports non-POT textures up to certain sizes */
    int tex_w = w;
    int tex_h = h;
    
    /* Only use POT if dimensions are large */
    if (w > 512 || h > 512) {
        tex_w = 64;
        while (tex_w < w && tex_w < 1024) tex_w *= 2;
        tex_h = 64;
        while (tex_h < h && tex_h < 1024) tex_h *= 2;
    }
    
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
    
    /* Allocate mutable subtex structure */
    Tex3DS_SubTexture *subtex = (Tex3DS_SubTexture *)malloc(sizeof(Tex3DS_SubTexture));
    if (!subtex) {
        C3D_TexDelete(&tex->c3d_tex);
        free(tex);
        set_error("Failed to allocate subtex");
        return NULL;
    }
    
    /* Initialize subtex fields (cast away const if needed) */
    *(u16*)&subtex->width = w;
    *(u16*)&subtex->height = h;
    *(float*)&subtex->left = 0.0f;
    *(float*)&subtex->top = 0.0f;
    *(float*)&subtex->right = (float)w / (float)tex_w;
    *(float*)&subtex->bottom = (float)h / (float)tex_h;
    
    tex->c2d_img.subtex = subtex;
    
    /* Allocate shadow buffer for UpdateTexture */
    tex->pitch = w * 4;  /* RGBA8 = 4 bytes per pixel */
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
    
    /* The wacki engine sends us ARGB8888 data (0xAARRGGBB).
     * 3DS GPU_RGBA8 format expects bytes in memory as: AABBGGRR (little-endian).
     * Each pixel is stored as 4 bytes: [AA][BB][GG][RR] in memory order. */
    
    int update_w = texture->width;
    int update_h = texture->height;
    
    if (rect) {
        update_w = rect->w;
        update_h = rect->h;
    }
    
    const uint32_t *src = (const uint32_t *)pixels;
    
    if (!texture->pixels_shadow) {
        return -1;
    }
    
    /* Convert ARGB8888 (0xAARRGGBB) to GPU_RGBA8 format (ABGR byte order) */
    uint8_t *dst = (uint8_t *)texture->pixels_shadow;
    int src_pitch_pixels = pitch / 4;
    
    for (int y = 0; y < update_h && y < texture->height; y++) {
        for (int x = 0; x < update_w && x < texture->width; x++) {
            uint32_t argb = src[y * src_pitch_pixels + x];
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            
            /* Write in ABGR byte order for GPU_RGBA8 */
            int idx = (y * texture->width + x) * 4;
            dst[idx + 0] = a;
            dst[idx + 1] = b;
            dst[idx + 2] = g;
            dst[idx + 3] = r;
        }
    }
    
    /* Flush cache and upload to VRAM using GX transfer */
    GSPGPU_FlushDataCache(texture->pixels_shadow, 
                          texture->width * texture->height * 4);
    
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

/* Event queue for 3DS input system */
static SDL_Event s_event_queue[32];
static int s_event_queue_head = 0;
static int s_event_queue_tail = 0;

static void push_event(SDL_Event *event)
{
    int next = (s_event_queue_tail + 1) % 32;
    if (next != s_event_queue_head) {
        s_event_queue[s_event_queue_tail] = *event;
        s_event_queue_tail = next;
    }
}

static int pop_event(SDL_Event *event)
{
    if (s_event_queue_head == s_event_queue_tail) {
        return 0;  /* Queue empty */
    }
    
    *event = s_event_queue[s_event_queue_head];
    s_event_queue_head = (s_event_queue_head + 1) % 32;
    return 1;
}

int SDL_PollEvent(SDL_Event *event)
{
    if (!event) return 0;
    
    /* Update audio buffers first */
    update_audio_buffers();
    
    /* First, try to pop from queue */
    if (pop_event(event)) {
        return 1;
    }
    
    /* Poll 3DS input and generate events */
    hidScanInput();
    
    touchPosition touch;
    u32 kDown = hidKeysDown();
    u32 kUp = hidKeysUp();
    u32 kHeld = hidKeysHeld();
    
    /* Check for touch on bottom screen */
    if (kDown & KEY_TOUCH) {
        hidTouchRead(&touch);
        SDL_Event touch_event;
        touch_event.type = SDL_FINGERDOWN;
        
        /* Map touch to cursor position */
        extern int16_t g_mouse_x, g_mouse_y;
        extern int platform_3ds_get_zoom_level(void);
        
        int zoom = platform_3ds_get_zoom_level();
        float zoom_factor = 1.0f / (1 << zoom);
        
        int zoom_game_w = (int)(640.0f * zoom_factor);
        int zoom_game_h = (int)(480.0f * zoom_factor);
        
        int zoom_src_x = g_mouse_x - zoom_game_w / 2;
        int zoom_src_y = g_mouse_y - zoom_game_h / 2;
        
        if (zoom_src_x < 0) zoom_src_x = 0;
        if (zoom_src_y < 0) zoom_src_y = 0;
        if (zoom_src_x + zoom_game_w > 640) zoom_src_x = 640 - zoom_game_w;
        if (zoom_src_y + zoom_game_h > 480) zoom_src_y = 480 - zoom_game_h;
        
        float rel_x = (float)touch.px / 320.0f;
        float rel_y = (float)touch.py / 240.0f;
        
        g_mouse_x = (int16_t)(zoom_src_x + (int)(rel_x * zoom_game_w));
        g_mouse_y = (int16_t)(zoom_src_y + (int)(rel_y * zoom_game_h));
        
        if (g_mouse_x < 0) g_mouse_x = 0;
        if (g_mouse_x >= 640) g_mouse_x = 639;
        if (g_mouse_y < 0) g_mouse_y = 0;
        if (g_mouse_y >= 480) g_mouse_y = 479;
        
        push_event(&touch_event);
        return pop_event(event);
    }
    
    /* Check for quit request (HOME button) */
    if (!aptMainLoop()) {
        SDL_Event quit_event;
        quit_event.type = SDL_QUIT;
        push_event(&quit_event);
        return pop_event(event);
    }
    
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

/* ---- Audio Implementation ---- */

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (s_audio_open) {
        LOG_INFO("3ds-audio", "Audio already open, reusing");
        if (obtained) {
            memcpy(obtained, desired, sizeof(SDL_AudioSpec));
        }
        return 0;
    }
    
    LOG_INFO("3ds-audio", "Opening audio: %d Hz, %d ch, %d samples",
             desired->freq, desired->channels, desired->samples);
    
    /* Initialize ndsp */
    Result res = ndspInit();
    if (R_FAILED(res)) {
        LOG_INFO("3ds-audio", "Failed to initialize ndsp: 0x%08lX", res);
        set_error("Failed to initialize ndsp");
        return -1;
    }
    
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    
    /* Set up channel 0 for audio playback */
    ndspChnReset(NDSP_CHANNEL);
    ndspChnSetInterp(NDSP_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(NDSP_CHANNEL, (float)desired->freq);
    ndspChnSetFormat(NDSP_CHANNEL, 
                     desired->channels == 1 ? NDSP_FORMAT_MONO_PCM16 
                                            : NDSP_FORMAT_STEREO_PCM16);
    
    /* Allocate audio buffers */
    int samples_per_buf = desired->samples;
    s_audio_buffer_size = samples_per_buf * desired->channels * sizeof(int16_t);
    
    LOG_INFO("3ds-audio", "Allocating %d buffers of %d bytes each",
             AUDIO_BUFFER_COUNT, s_audio_buffer_size);
    
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        s_audio_buffer[i] = (int16_t *)linearAlloc(s_audio_buffer_size);
        if (!s_audio_buffer[i]) {
            LOG_INFO("3ds-audio", "Failed to allocate buffer %d", i);
            /* Clean up on failure */
            for (int j = 0; j < i; j++) {
                linearFree(s_audio_buffer[j]);
                s_audio_buffer[j] = NULL;
            }
            ndspExit();
            set_error("Failed to allocate audio buffers");
            return -1;
        }
        memset(s_audio_buffer[i], 0, s_audio_buffer_size);
        
        /* Set up wave buffer */
        memset(&s_wave_bufs[i], 0, sizeof(ndspWaveBuf));
        s_wave_bufs[i].data_vaddr = s_audio_buffer[i];
        s_wave_bufs[i].nsamples = samples_per_buf;
        s_wave_bufs[i].looping = false;
        s_wave_bufs[i].status = NDSP_WBUF_DONE;
    }
    
    /* Save callback info */
    s_audio_callback = desired->callback;
    s_audio_userdata = desired->userdata;
    s_current_buffer = 0;
    
    /* Fill obtained spec */
    if (obtained) {
        memcpy(obtained, desired, sizeof(SDL_AudioSpec));
    }
    
    s_audio_open = 1;
    
    LOG_INFO("3ds-audio", "Filling initial buffers...");
    
    /* Start initial buffers */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        if (s_audio_callback) {
            s_audio_callback(s_audio_userdata, (uint8_t *)s_audio_buffer[i], 
                           s_audio_buffer_size);
        }
        DSP_FlushDataCache(s_audio_buffer[i], s_audio_buffer_size);
        ndspChnWaveBufAdd(NDSP_CHANNEL, &s_wave_bufs[i]);
    }
    
    LOG_INFO("3ds-audio", "Audio initialized successfully");
    
    return 0;
}

void SDL_CloseAudio(void)
{
    if (!s_audio_open) return;
    
    /* Stop playback */
    ndspChnReset(NDSP_CHANNEL);
    
    /* Free buffers */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        if (s_audio_buffer[i]) {
            linearFree(s_audio_buffer[i]);
            s_audio_buffer[i] = NULL;
        }
    }
    
    ndspExit();
    s_audio_open = 0;
    s_audio_callback = NULL;
    s_audio_userdata = NULL;
}

void SDL_PauseAudio(int pause_on)
{
    if (!s_audio_open) return;
    
    ndspChnSetPaused(NDSP_CHANNEL, pause_on != 0);
}

/* This function should be called regularly to refill audio buffers.
 * We'll call it from SDL_PollEvent to keep audio streaming. */
static void update_audio_buffers(void)
{
    if (!s_audio_open || !s_audio_callback) return;
    
    /* Check if any buffer needs refilling */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        if (s_wave_bufs[i].status == NDSP_WBUF_DONE) {
            /* Refill this buffer */
            s_audio_callback(s_audio_userdata, (uint8_t *)s_audio_buffer[i],
                           s_audio_buffer_size);
            DSP_FlushDataCache(s_audio_buffer[i], s_audio_buffer_size);
            ndspChnWaveBufAdd(NDSP_CHANNEL, &s_wave_bufs[i]);
        }
    }
}
