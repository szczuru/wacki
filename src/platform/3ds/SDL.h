/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL.h — minimal SDL2 compatibility shim for 3DS.
 *
 * The engine core (src/*.c) still calls a handful of SDL2 functions
 * directly (SDL_Delay, SDL_GetTicks, SDL_malloc/free/memcpy/memset,
 * the WAV loader chain, SDL_ShowSimpleMessageBox, SDL_getenv/setenv,
 * SDL_GetBasePath, SDL_PushEvent for the SIGINT handler, screenshot's
 * SDL_Surface/SDL_Color/SDL_SetPaletteColors/SDL_SaveBMP). None of the
 * SDL2 *rendering/window* API is used on 3DS (video_3ds_gl.c talks to
 * picaGL directly) — those symbols are NOT provided here on purpose;
 * if something new starts calling them the missing-symbol error is the
 * signal to either stub it here or route it through the video HAL
 * instead.
 *
 * Every function here is a REAL external symbol implemented in
 * SDL_compat.c (not inline) so this header can be included from many
 * translation units without ODR/multiple-definition issues.
 *
 * -I src/platform/3ds is placed FIRST in mk/3ds.mk's CFLAGS so
 * `#include <SDL.h>` resolves here instead of failing with "no such
 * file" (3DS toolchain ships no SDL2 headers at all). */

#ifndef WACKI_3DS_SDL_H
#define WACKI_3DS_SDL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- basic SDL integer aliases (used directly by audio.c et al) ---- */
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

/* ---- init / error (main.c calls SDL_Init only via platform_sdl.c,   *
 * which we DON'T compile — kept here only in case something generic  *
 * references it; harmless no-op stubs) ------------------------------ */
int         SDL_Init(uint32_t flags);
void        SDL_Quit(void);
const char *SDL_GetError(void);

/* ---- timing --------------------------------------------------------- */
void     SDL_Delay(uint32_t ms);
uint32_t SDL_GetTicks(void);

/* ---- memory (audio.c / music_stream.c use these instead of libc so   *
 * WAV buffers can be freed uniformly with SDL_free / SDL_FreeWAV) ---- */
void *SDL_malloc(size_t size);
void  SDL_free(void *ptr);
void *SDL_memcpy(void *dst, const void *src, size_t len);
void *SDL_memset(void *dst, int c, size_t len);

/* ---- environment (main.c's headless SDL_VIDEODRIVER/AUDIODRIVER) --- */
char *SDL_getenv(const char *name);
int   SDL_setenv(const char *name, const char *value, int overwrite);
char *SDL_GetBasePath(void);   /* data_root.c; returns NULL on 3DS */

/* ---- events (main.c's SIGINT handler pushes a synthetic SDL_QUIT) -- */
typedef enum {
    SDL_QUIT = 0x100
} SDL_EventType_3DS;

typedef union SDL_Event {
    uint32_t type;
    uint8_t  padding[64];
} SDL_Event;

int SDL_PushEvent(SDL_Event *event);

/* ---- message box (main.c: fatal "no data root" dialog) ------------- */
#define SDL_MESSAGEBOX_ERROR 0x00000010
typedef struct SDL_Window SDL_Window;
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title,
                             const char *message, SDL_Window *window);

/* ---- audio spec / WAV loading (audio.c mixer_load_wav chain) ------- *
 * Real decode happens in SDL_compat.c via a tiny built-in RIFF/WAVE
 * parser (no libSDL2 available on 3DS) — enough to load the game's PCM
 * WAV assets. Anything it can't parse simply fails to load (silence),
 * same externally-visible behaviour as a real SDL_LoadWAV_RW miss. */
typedef struct SDL_AudioSpec {
    int      freq;
    uint16_t format;
    uint8_t  channels;
    uint8_t  silence;
    uint16_t samples;
    uint32_t size;
    void   (*callback)(void *userdata, uint8_t *stream, int len);
    void    *userdata;
} SDL_AudioSpec;

#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

typedef struct SDL_RWops SDL_RWops;
SDL_RWops     *SDL_RWFromConstMem(const void *mem, int size);
int            SDL_RWclose(SDL_RWops *ctx);
SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc,
                              SDL_AudioSpec *spec,
                              Uint8 **audio_buf, Uint32 *audio_len);
void           SDL_FreeWAV(Uint8 *audio_buf);

/* SDL_AudioCVT — format conversion (used when a WAV isn't already
 * 22050 Hz / S16 / stereo). The 3DS build's converter (SDL_compat.c)
 * only implements the identity + simple resample/channel cases the
 * game's assets actually need; anything else fails the build_cvt call,
 * same as upstream SDL2 returning -1 for an unsupported conversion. */
typedef struct SDL_AudioCVT {
    int      needed;
    Uint16   src_format;
    Uint16   dst_format;
    double   rate_incr;
    Uint8   *buf;
    int      len;
    int      len_cvt;
    int      len_mult;
    double   len_ratio;
    void    *filters[10];
    int      filter_index;
} SDL_AudioCVT;

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
                      Uint16 src_format, Uint8 src_channels, int src_rate,
                      Uint16 dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);

/* ---- surface / palette / BMP (util/screenshot.c) ------------------- */
typedef struct SDL_Color {
    uint8_t r, g, b, a;
} SDL_Color;

typedef struct SDL_Palette {
    int        ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    SDL_Palette *palette;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    int              w, h, pitch;
    void            *pixels;
    SDL_PixelFormat *format;
} SDL_Surface;

#define SDL_PIXELFORMAT_INDEX8 0x10100801

SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width,
                                                int height, int depth,
                                                int pitch, uint32_t format);
void         SDL_FreeSurface(SDL_Surface *surface);
int          SDL_SetPaletteColors(SDL_Palette *palette,
                                  const SDL_Color *colors,
                                  int firstcolor, int ncolors);
int          SDL_SaveBMP(SDL_Surface *surface, const char *file);

#ifdef __cplusplus
}
#endif

#endif /* WACKI_3DS_SDL_H */
