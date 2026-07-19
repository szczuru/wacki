/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.c — SDL compatibility layer for 3DS.
 *
 * Provides minimal SDL API stubs. Real rendering is done by video_3ds_gl.c
 * using picaGL (OpenGL ES 1.1 → citro3d).
 */

#include "wacki.h"
#include "wacki/log.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

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
    return 1;
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

int SDL_SetPaletteColors(void *pal, const SDL_Color *colors, int first, int ncolors)
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
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *msg, SDL_Window *win)
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

int SDL_PushEvent(SDL_Event *ev)
{
    (void)ev;
    return 1;
}

/* Audio - minimal stubs (audio won't work but will compile) */
SDL_RWops* SDL_RWFromConstMem(const void *mem, int size)
{
    (void)mem; (void)size;
    return NULL;
}

SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    (void)src; (void)freesrc; (void)spec; (void)audio_buf; (void)audio_len;
    return NULL;
}

void SDL_FreeWAV(Uint8 *audio_buf)
{
    SDL_free(audio_buf);
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels, int src_rate,
                      Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    (void)cvt; (void)src_format; (void)src_channels; (void)src_rate;
    (void)dst_format; (void)dst_channels; (void)dst_rate;
    return -1;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    (void)cvt;
    return -1;
}
