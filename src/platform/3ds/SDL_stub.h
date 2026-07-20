/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_stub.h — Minimal SDL compatibility for 3DS (header-only)
 *
 * This provides just enough SDL types/constants to satisfy #include <SDL.h>
 * throughout the engine. Real platform functionality is in 3ds-specific files. */

#ifndef SDL_STUB_3DS_H
#define SDL_STUB_3DS_H

#include <stdint.h>

/* SDL init flags (unused on 3DS) */
#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_TIMER 0x00000001

/* SDL types (opaque) */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Surface SDL_Surface;

/* SDL basic types */
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

/* SDL_Rect */
typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

/* SDL constants */
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define SDL_TEXTUREACCESS_STREAMING 0x00000001
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_WINDOW_FULLSCREEN 0x00000001
#define SDL_RENDERER_ACCELERATED 0x00000002

/* SDL event types */
typedef enum {
    SDL_QUIT = 0x100,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP = 0x301,
} SDL_EventType;

typedef union SDL_Event {
    uint32_t type;
    uint8_t padding[56];
} SDL_Event;

/* SDL functions (stubs - real impl in 3DS files) */
static inline int SDL_Init(uint32_t flags) { (void)flags; return 0; }
static inline void SDL_Quit(void) {}
static inline const char* SDL_GetError(void) { return "3DS"; }
static inline int SDL_PollEvent(SDL_Event *e) { (void)e; return 0; }
static inline void SDL_Delay(uint32_t ms) { (void)ms; }
static inline uint32_t SDL_GetTicks(void) { return 0; }

/* Dummy functions to satisfy linker */
static inline SDL_Window* SDL_CreateWindow(const char *t, int x, int y, int w, int h, uint32_t f) {
    (void)t;(void)x;(void)y;(void)w;(void)h;(void)f; return (SDL_Window*)1;
}
static inline SDL_Renderer* SDL_CreateRenderer(SDL_Window *w, int i, uint32_t f) {
    (void)w;(void)i;(void)f; return (SDL_Renderer*)1;
}
static inline SDL_Texture* SDL_CreateTexture(SDL_Renderer *r, uint32_t fmt, int a, int w, int h) {
    (void)r;(void)fmt;(void)a;(void)w;(void)h; return (SDL_Texture*)1;
}
static inline void SDL_DestroyTexture(SDL_Texture *t) { (void)t; }
static inline void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }
static inline void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
static inline int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch) {
    (void)t;(void)r;(void)p;(void)pitch; return 0;
}
static inline int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d) {
    (void)r;(void)t;(void)s;(void)d; return 0;
}
static inline void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
static inline int SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t g, uint8_t b, uint8_t a) {
    (void)r;(void)red;(void)g;(void)b;(void)a; return 0;
}
static inline int SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }

#endif /* SDL_STUB_3DS_H */
