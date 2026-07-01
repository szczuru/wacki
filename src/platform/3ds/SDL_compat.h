/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.h — Minimal SDL compatibility shim for 3DS.
 *
 * This header provides minimal SDL types/functions needed by wacki engine core.
 * It does NOT implement full SDL - only what wacki actually uses. */

#ifndef WACKI_3DS_SDL_COMPAT_H
#define WACKI_3DS_SDL_COMPAT_H

#include <stdint.h>
#include <3ds.h>

/* SDL Init flags */
#define SDL_INIT_VIDEO       0x00000020
#define SDL_INIT_AUDIO       0x00000010
#define SDL_INIT_TIMER       0x00000001
#define SDL_INIT_GAMECONTROLLER 0x00002000

/* SDL Video flags */
#define SDL_WINDOW_SHOWN     0x00000004
#define SDL_WINDOW_FULLSCREEN 0x00000001

/* SDL Renderer flags */
#define SDL_RENDERER_ACCELERATED 0x00000002
#define SDL_RENDERER_PRESENTVSYNC 0x00000004

/* SDL Pixel formats */
#define SDL_PIXELFORMAT_RGB888   0x16462004
#define SDL_PIXELFORMAT_INDEX8   0x13000801

/* SDL Blend modes */
typedef enum {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001,
} SDL_BlendMode;

/* SDL Keycodes */
typedef enum {
    SDLK_UNKNOWN = 0,
    SDLK_RETURN = 13,
    SDLK_ESCAPE = 27,
    SDLK_BACKSPACE = 8,
    SDLK_SPACE = 32,
    SDLK_UP = 1073741906,
    SDLK_DOWN = 1073741905,
    SDLK_RIGHT = 1073741903,
    SDLK_LEFT = 1073741904,
} SDL_Keycode;

/* SDL Scancodes */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
} SDL_Scancode;

/* SDL Key state */
typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    uint16_t mod;
    uint32_t unused;
} SDL_Keysym;

/* SDL Event types */
typedef enum {
    SDL_FIRSTEVENT     = 0,
    SDL_QUIT           = 0x100,
    SDL_KEYDOWN        = 0x300,
    SDL_KEYUP          = 0x301,
    SDL_TEXTINPUT      = 0x303,
    SDL_MOUSEMOTION    = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP  = 0x402,
    SDL_FINGERDOWN     = 0x700,
    SDL_FINGERUP       = 0x701,
    SDL_FINGERMOTION   = 0x702,
} SDL_EventType;

/* SDL Event structure */
typedef struct SDL_Event {
    uint32_t type;
    uint32_t timestamp;
    union {
        struct {
            uint32_t windowID;
            uint8_t state;
            uint8_t repeat;
            uint8_t padding2;
            uint8_t padding3;
            SDL_Keysym keysym;
        } key;
        struct {
            char text[32];
        } text;
        struct {
            uint32_t windowID;
            int32_t x;
            int32_t y;
        } motion;
        struct {
            uint32_t windowID;
            uint8_t button;
            uint8_t state;
        } button;
    };
} SDL_Event;

/* SDL Rect */
typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

/* SDL Color */
typedef struct SDL_Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} SDL_Color;

/* SDL Palette */
typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
    uint32_t version;
    int refcount;
} SDL_Palette;

/* SDL PixelFormat */
typedef struct SDL_PixelFormat {
    uint32_t format;
    SDL_Palette *palette;
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint8_t padding[2];
    uint32_t Rmask;
    uint32_t Gmask;
    uint32_t Bmask;
    uint32_t Amask;
} SDL_PixelFormat;

/* SDL Surface */
typedef struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    void *map;
    int refcount;
} SDL_Surface;

/* SDL Texture (opaque pointer) */
typedef struct SDL_Texture SDL_Texture;

/* SDL Renderer (opaque pointer) */
typedef struct SDL_Renderer SDL_Renderer;

/* SDL Window (opaque pointer) */
typedef struct SDL_Window SDL_Window;

/* SDL RWops (opaque pointer) */
typedef struct SDL_RWops SDL_RWops;

/* SDL Audio spec */
typedef struct SDL_AudioSpec
