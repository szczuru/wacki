/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL.h — SDL compatibility wrapper for 3DS.
 *
 * This header is found FIRST by the compiler (via -I src/platform/3ds) and
 * provides SDL compatibility for wacki engine. It includes the compatibility
 * layer instead of real SDL headers. */

#ifndef WACKI_3DS_SDL_H
#define WACKI_3DS_SDL_H

#include <string.h>
#include <stdlib.h>

/* Pull in our compatibility layer */
#include "SDL_compat.h"

/* Additional SDL2 compatibility defines */
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000)
#define SDL_WINDOW_RESIZABLE          0x00000020

/* SDL_GetBasePath - return SD card path */
static inline char* SDL_GetBasePath(void) {
    char *path = malloc(32);
    if (path) strcpy(path, "sdmc:/3ds/wacki/");
    return path;
}

/* Init flags */
#define SDL_INIT_EVENTS 0x00004000

/* Texture access modes */
#define SDL_TEXTUREACCESS_STATIC    0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

/* Renderer flags */
#define SDL_RENDERER_SOFTWARE 0x00000001

/* Pixel formats */
#define SDL_PIXELFORMAT_ARGB8888 0x16362004

/* Audio formats */
#define AUDIO_U8     0x0008
#define AUDIO_S16SYS 0x8010
#define AUDIO_S16LSB 0x8010

/* Audio device flags */
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE    0x00000002
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE  0x00000004
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE   0x00000008

/* SDL Cursor/Display modes */
#define SDL_DISABLE 0
#define SDL_ENABLE  1

/* Additional functions that might be used */
static inline int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
static inline void SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon) { (void)window; (void)icon; }
static inline void SDL_PumpEvents(void) {}
static inline void SDL_GetWindowSize(SDL_Window *window, int *w, int *h) { 
    if (window && w && h) { *w = window->w; *h = window->h; }
}
static inline int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags) { (void)window; (void)flags; return 0; }
static inline int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window) {
    (void)flags; (void)title; (void)message; (void)window; return 0;
}

/* Keyboard state */
static inline const Uint8* SDL_GetKeyboardState(int *numkeys) {
    (void)numkeys;
    static Uint8 dummy_keys[512] = {0};
    return dummy_keys;
}

/* Message box flags */
#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040

/* Window position */
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000
#define SDL_WINDOWPOS_CENTERED  0x2FFF0000

/* RWops stubs */
typedef struct SDL_RWops {
    void *hidden;
} SDL_RWops;

static inline SDL_RWops* SDL_RWFromConstMem(const void *mem, int size) {
    (void)mem; (void)size;
    return NULL;
}

/* Audio device functions (stubs for now) */
typedef uint32_t SDL_AudioDeviceID;
static inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                                     const SDL_AudioSpec *desired,
                                                     SDL_AudioSpec *obtained,
                                                     int allowed_changes) {
    (void)device; (void)iscapture; (void)allowed_changes;
    SDL_OpenAudio((SDL_AudioSpec *)desired, obtained);
    return 1;
}

static inline void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {
    (void)dev;
    SDL_PauseAudio(pause_on);
}

static inline void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    SDL_CloseAudio();
}

static inline void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
}

static inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
}

static inline int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, uint32_t len) {
    (void)dev; (void)data; (void)len;
    return 0;
}

static inline uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
    (void)dev;
    return 0;
}

static inline void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
    (void)dev;
}

/* Audio stream stubs */
typedef struct SDL_AudioStream SDL_AudioStream;

static inline SDL_AudioStream* SDL_NewAudioStream(uint16_t src_format, uint8_t src_channels, int src_rate,
                                                   uint16_t dst_format, uint8_t dst_channels, int dst_rate) {
    (void)src_format; (void)src_channels; (void)src_rate;
    (void)dst_format; (void)dst_channels; (void)dst_rate;
    return NULL;
}

static inline int SDL_AudioStreamPut(SDL_AudioStream *stream, const void *buf, int len) {
    (void)stream; (void)buf; (void)len;
    return 0;
}

static inline int SDL_AudioStreamGet(SDL_AudioStream *stream, void *buf, int len) {
    (void)stream; (void)buf; (void)len;
    return 0;
}

static inline int SDL_AudioStreamAvailable(SDL_AudioStream *stream) {
    (void)stream;
    return 0;
}

static inline void SDL_AudioStreamClear(SDL_AudioStream *stream) {
    (void)stream;
}

static inline void SDL_FreeAudioStream(SDL_AudioStream *stream) {
    (void)stream;
}

/* Lock/Unlock texture stubs */
static inline int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
                                  void **pixels, int *pitch) {
    (void)texture; (void)rect;
    *pixels = NULL;
    *pitch = 0;
    return -1;  /* Always fail - use UpdateTexture instead */
}

static inline void SDL_UnlockTexture(SDL_Texture *texture) {
    (void)texture;
}

/* Additional utility macros */
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_malloc malloc
#define SDL_free free
#define SDL_getenv getenv
#define SDL_setenv(name, value, overwrite) setenv(name, value, overwrite)

/* Audio bitsize helper */
#define SDL_AUDIO_BITSIZE(x) (((x) & 0xFF))

/* SDL_PushEvent stub */
static inline int SDL_PushEvent(SDL_Event *event) {
    (void)event;
    return 0;
}

/* Audio conversion structure */
typedef struct SDL_AudioCVT {
    int needed;
    uint16_t src_format;
    uint16_t dst_format;
    double rate_incr;
    uint8_t *buf;
    int len;
    int len_cvt;
    int len_mult;
    double len_ratio;
    void (*filters[10])(struct SDL_AudioCVT *cvt, uint16_t format);
    int filter_index;
} SDL_AudioCVT;

/* Audio conversion functions */
static inline int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
                                   uint16_t src_format, uint8_t src_channels, int src_rate,
                                   uint16_t dst_format, uint8_t dst_channels, int dst_rate) {
    if (!cvt) return -1;
    SDL_memset(cvt, 0, sizeof(*cvt));
    
    /* Check if conversion is needed */
    if (src_format == dst_format && src_channels == dst_channels && src_rate == dst_rate) {
        cvt->needed = 0;
        return 0;
    }
    
    cvt->needed = 1;
    cvt->src_format = src_format;
    cvt->dst_format = dst_format;
    cvt->rate_incr = (double)dst_rate / (double)src_rate;
    cvt->len_mult = 2;  /* Conservative estimate */
    cvt->len_ratio = cvt->rate_incr * ((double)dst_channels / (double)src_channels);
    
    return 1;
}

static inline int SDL_ConvertAudio(SDL_AudioCVT *cvt) {
    if (!cvt || !cvt->needed) return 0;
    
    /* Simple pass-through for now - real implementation would convert formats */
    cvt->len_cvt = cvt->len;
    return 0;
}

/* SDL_LoadWAV_RW - load WAV file from RWops */
static inline SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops *src, int freesrc,
                                           SDL_AudioSpec *spec, uint8_t **audio_buf, uint32_t *audio_len) {
    (void)src; (void)freesrc; (void)spec; (void)audio_buf; (void)audio_len;
    /* Stub - return NULL to indicate failure */
    return NULL;
}

/* SDL_FreeWAV - free WAV data loaded by SDL_LoadWAV_RW */
static inline void SDL_FreeWAV(uint8_t *audio_buf) {
    if (audio_buf) SDL_free(audio_buf);
}

#endif /* WACKI_3DS_SDL_H */
