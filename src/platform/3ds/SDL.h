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

/* Pull in our compatibility layer */
#include "SDL_compat.h"

/* Additional SDL2 compatibility defines */
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000)
#define SDL_WINDOW_RESIZABLE          0x00000020

/* Texture access modes */
#define SDL_TEXTUREACCESS_STATIC    0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

/* Audio formats */
#define AUDIO_U8     0x0008
#define AUDIO_S16SYS 0x8010
#define AUDIO_S16LSB 0x8010

/* Audio device flags */
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE    0x00000002
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE  0x00000004
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE   0x00000008

/* Additional functions that might be used */
#define SDL_ShowCursor(toggle) (0)
#define SDL_SetWindowIcon(window, icon) ((void)0)
#define SDL_PumpEvents() ((void)0)
#define SDL_GetWindowSize(window, w, h) do { \
    *(w) = (window)->w; \
    *(h) = (window)->h; \
} while(0)
#define SDL_SetWindowFullscreen(window, flags) (0)
#define SDL_ShowSimpleMessageBox(flags, title, message, window) ((void)0)

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

/* Audio bitsize helper */
#define SDL_AUDIO_BITSIZE(x) (((x) & 0xFF))

#endif /* WACKI_3DS_SDL_H */
