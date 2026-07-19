/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.c — SDL compatibility layer for 3DS.
 *
 * Provides minimal SDL API stubs. Real rendering is done by video_3ds_gl.c
 * using NovaGL (OpenGL ES 1.1 → citro3d).
 */

#include "wacki.h"
#include "wacki/log.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

/* SDL types */
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

struct SDL_Surface {
    int w, h;
    void *pixels;
    int pitch;
};

/* Minimal SDL API stubs */

int SDL_Init(uint32_t flags)
{
    (void)flags;
    /* 3DS init done in system_3ds.c */
    return 0;
}

void SDL_Quit(void)
{
    /* Cleanup done in system_3ds.c */
}

int SDL_ShowCursor(int toggle)
{
    (void)toggle;
    return 0;
}

int SDL_PollEvent(void *event)
{
    (void)event;
    /* Input handled in gamepad_3ds.c */
    return 0;
}

void SDL_Delay(uint32_t ms)
{
    svcSleepThread(ms * 1000000ULL);
}

uint32_t SDL_GetTicks(void)
{
    return (uint32_t)(svcGetSystemTick() / 268111);
}

const char* SDL_GetError(void)
{
    return "3DS stub";
}

/* Memory */
void* SDL_malloc(size_t size)
{
    return malloc(size);
}

void SDL_free(void *ptr)
{
    free(ptr);
}

void* SDL_memcpy(void *dst, const void *src, size_t len)
{
    return memcpy(dst, src, len);
}

void* SDL_memset(void *dst, int c, size_t len)
{
    return memset(dst, c, len);
}

/* Environment */
char* SDL_getenv(const char *name)
{
    (void)name;
    return NULL;
}

int SDL_setenv(const char *name, const char *value, int overwrite)
{
    (void)name; (void)value; (void)overwrite;
    return 0;
}

/* Text input stubs */
void SDL_StartTextInput(void)
{
}

void SDL_StopTextInput(void)
{
}

/* Hints */
int SDL_SetHint(const char *name, const char *value)
{
    (void)name; (void)value;
    return 0;
}

/* Paths */
char* SDL_GetBasePath(void)
{
    return NULL;
}

/* Surface stubs */
SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int depth, int pitch, uint32_t format)
{
    (void)pixels; (void)w; (void)h; (void)depth; (void)pitch; (void)format;
    return NULL;
}

int SDL_SetPaletteColors(void *pal, const void *colors, int first, int ncolors)
{
    (void)pal; (void)colors; (void)first; (void)ncolors;
    return 0;
}

int SDL_SaveBMP(SDL_Surface *s, const char *file)
{
    (void)s; (void)file;
    return -1;
}

void SDL_FreeSurface(SDL_Surface *s)
{
    (void)s;
}

/* Message box */
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *msg, void *win)
{
    (void)flags; (void)title; (void)msg; (void)win;
    LOG_INFO("msgbox", "%s: %s", title ? title : "Info", msg ? msg : "");
    return 0;
}

/* Keyboard */
const uint8_t* SDL_GetKeyboardState(int *numkeys)
{
    (void)numkeys;
    static uint8_t state[512] = {0};
    return state;
}

int SDL_PushEvent(void *ev)
{
    (void)ev;
    return 0;
}
