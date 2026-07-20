/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL_compat.c — implementation of the SDL.h shim.
 *
 * Real bodies for every symbol declared in SDL.h. Kept intentionally
 * small: this is NOT a general SDL2 reimplementation, only the subset
 * the engine core (src/*.c) still calls directly on every platform. */

#include "SDL.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- init / error --------------------------------------------------- */

int SDL_Init(uint32_t flags)
{
    (void)flags;
    return 0;
}

void SDL_Quit(void) {}

const char *SDL_GetError(void)
{
    return "3ds";
}

/* ---- timing ----------------------------------------------------------
 * svcGetSystemTick() runs at CPU_TICKS_PER_SEC (268111856 Hz on 3DS). */

void SDL_Delay(uint32_t ms)
{
    svcSleepThread((s64)ms * 1000000LL);
}

/* 3DS ARM11 tick counter runs at 268,111,856 Hz (3dbrew "Hardware": CPU
 * tick rate in CTR mode) — divide by 268111.856 ~= 268111 to get ms. Using
 * the literal instead of libctru's SYSCLOCK_ARM11 macro keeps this file
 * buildable even if that macro's exact name/header location changes across
 * libctru versions. */
#define WACKI_3DS_TICKS_PER_MS 268111ULL

uint32_t SDL_GetTicks(void)
{
    return (uint32_t)(svcGetSystemTick() / WACKI_3DS_TICKS_PER_MS);
}

/* ---- memory ----------------------------------------------------------- */

void *SDL_malloc(size_t size)  { return malloc(size); }
void  SDL_free(void *ptr)      { free(ptr); }
void *SDL_memcpy(void *dst, const void *src, size_t len) { return memcpy(dst, src, len); }
void *SDL_memset(void *dst, int c, size_t len)            { return memset(dst, c, len); }

/* ---- environment -------------------------------------------------------
 * newlib on 3DS has getenv/setenv; wrap them so main.c's headless-mode
 * "set SDL_VIDEODRIVER=dummy if unset" dance still compiles + behaves
 * (it's a no-op in practice — 3DS has no such drivers to select). */

char *SDL_getenv(const char *name)
{
    return getenv(name);
}

int SDL_setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name)) return 0;
    return setenv(name, value, 1);
}

char *SDL_GetBasePath(void)
{
    /* No concept of "binary directory" via argv[0] on 3DS homebrew launch;
     * data_root.c treats NULL as "skip this candidate" and falls through
     * to plat_data_roots() (data_root_3ds.c's sdmc:/3ds/wacki list). */
    return NULL;
}

/* ---- events ------------------------------------------------------------
 * Only used by main.c's SIGINT handler to request a graceful quit; there is
 * no event queue to push into on 3DS, so just latch app-should-quit via
 * aptSetHomeAllowed-independent state read by plat_should_quit(). We can't
 * reach into platform_3ds.c's static state, so route through a tiny shared
 * flag instead. */

int g_sdl_compat_quit_requested = 0;

int SDL_PushEvent(SDL_Event *event)
{
    if (event && event->type == SDL_QUIT) g_sdl_compat_quit_requested = 1;
    return 1;
}

/* ---- message box -------------------------------------------------------
 * No GUI dialog on 3DS; log it (stderr goes nowhere useful on real hw
 * without a debugger attached, but this keeps 3dslink/citra console output
 * meaningful, and costs nothing). */

int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title,
                             const char *message, SDL_Window *window)
{
    (void)flags; (void)window;
    fprintf(stderr, "[msgbox] %s: %s\n", title ? title : "", message ? message : "");
    return 0;
}

/* ---- minimal RIFF/WAVE loader -------------------------------------------
 * Enough to load the game's own WAV assets (canonical PCM RIFF/WAVE,
 * mono or stereo, 8/16-bit) out of the in-memory buffer SDL_RWFromConstMem
 * wraps. Anything else (unusual chunk layout, compressed WAV) fails, same
 * externally-visible behaviour as upstream SDL2 rejecting a format it
 * doesn't support — callers already treat a load failure as "asset
 * missing/unplayable" and carry on silently. */

struct SDL_RWops {
    const uint8_t *data;
    int            size;
};

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    SDL_RWops *rw = (SDL_RWops *)malloc(sizeof *rw);
    if (!rw) return NULL;
    rw->data = (const uint8_t *)mem;
    rw->size = size;
    return rw;
}

int SDL_RWclose(SDL_RWops *ctx)
{
    free(ctx);
    return 0;
}

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc,
                              SDL_AudioSpec *spec,
                              Uint8 **audio_buf, Uint32 *audio_len)
{
    *audio_buf = NULL;
    *audio_len = 0;
    if (!src || !src->data || src->size < 44) goto fail;

    const uint8_t *p = src->data;
    int            n = src->size;

    if (memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) goto fail;

    int      have_fmt = 0;
    uint16_t afmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    uint32_t data_off = 0, data_sz = 0;

    uint32_t off = 12;
    while (off + 8 <= (uint32_t)n) {
        const uint8_t *chunk = p + off;
        uint32_t csz = rd_u32le(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && csz >= 16 && off + 8 + csz <= (uint32_t)n) {
            afmt     = rd_u16le(chunk + 8);
            channels = rd_u16le(chunk + 10);
            rate     = rd_u32le(chunk + 12);
            bits     = rd_u16le(chunk + 22);
            have_fmt = 1;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_off = off + 8;
            data_sz  = csz;
            if (data_off + data_sz > (uint32_t)n) data_sz = (uint32_t)n - data_off;
            break; /* data chunk found — stop (matches typical WAV layout) */
        }
        off += 8 + csz + (csz & 1);
    }

    if (!have_fmt || data_sz == 0) goto fail;
    if (afmt != 1 /* PCM */ || (bits != 8 && bits != 16) ||
        (channels != 1 && channels != 2)) goto fail;

    Uint8 *buf = (Uint8 *)malloc(data_sz);
    if (!buf) goto fail;
    memcpy(buf, p + data_off, data_sz);

    spec->freq     = (int)rate;
    spec->format   = (uint16_t)(bits == 16 ? AUDIO_S16LSB : 0x0008 /* AUDIO_U8 */);
    spec->channels = (uint8_t)channels;
    spec->silence  = 0;
    spec->samples  = 4096;
    spec->size     = data_sz;
    spec->callback = NULL;
    spec->userdata = NULL;

    *audio_buf = buf;
    *audio_len = data_sz;

    if (freesrc) SDL_RWclose(src);
    return spec;

fail:
    if (freesrc && src) SDL_RWclose(src);
    return NULL;
}

void SDL_FreeWAV(Uint8 *audio_buf)
{
    free(audio_buf);
}

/* ---- audio format conversion --------------------------------------------
 * Only the conversions the game's assets realistically need: mono→stereo
 * duplication and 8-bit→16-bit expansion, both at a fixed rate (no
 * resampling — every shipped WAV is already 22050 Hz). Anything requiring
 * an actual sample-rate change fails, matching SDL_BuildAudioCVT's own
 * "unsupported conversion" failure mode. */

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
                      Uint16 src_format, Uint8 src_channels, int src_rate,
                      Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    if (!cvt) return -1;
    memset(cvt, 0, sizeof *cvt);

    if (src_rate != dst_rate) return -1;   /* no resampler on 3DS build */

    int mult = 1;
    if (src_format == 0x0008 /* AUDIO_U8 */ && dst_format == AUDIO_S16LSB) mult *= 2;
    else if (src_format != dst_format) return -1;

    if (src_channels == 1 && dst_channels == 2) mult *= 2;
    else if (src_channels != dst_channels) return -1;

    cvt->needed      = (mult != 1) ? 1 : 0;
    cvt->src_format  = src_format;
    cvt->dst_format  = dst_format;
    cvt->rate_incr   = 1.0;
    cvt->len_mult    = mult;
    cvt->len_ratio   = (double)mult;
    cvt->filter_index = 0;
    return 0;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    if (!cvt || !cvt->buf) return -1;
    if (!cvt->needed) { cvt->len_cvt = cvt->len; return 0; }

    int src_is_u8      = (cvt->src_format == 0x0008);
    int dst_is_s16      = (cvt->dst_format == AUDIO_S16LSB);
    int upmix_channels  = (cvt->len_mult == 2 && !(src_is_u8 && dst_is_s16));
    int upsample_bits   = (src_is_u8 && dst_is_s16);

    /* Only single-stage conversions are needed by the game's assets; a
     * combined 8-bit-mono → 16-bit-stereo asset doesn't occur in practice
     * (len_mult would be 4, which BuildAudioCVT above never produces). */
    Uint8 *src = cvt->buf;
    int    src_len = cvt->len;

    if (upsample_bits) {
        int16_t *out = (int16_t *)cvt->buf;
        /* Expand in place, back-to-front so we don't overwrite unread
         * source bytes (dst is 2x the size of src, both share cvt->buf). */
        for (int i = src_len - 1; i >= 0; --i) {
            int16_t s16 = (int16_t)(((int)src[i] - 128) << 8);
            out[i] = s16;
        }
        cvt->len_cvt = src_len * 2;
        return 0;
    }

    if (upmix_channels) {
        /* Mono → stereo duplication, S16 samples, back-to-front in place. */
        int      n_samples = src_len / 2;
        int16_t *s16src = (int16_t *)src;
        int16_t *s16dst = (int16_t *)src;
        for (int i = n_samples - 1; i >= 0; --i) {
            int16_t v = s16src[i];
            s16dst[i * 2 + 0] = v;
            s16dst[i * 2 + 1] = v;
        }
        cvt->len_cvt = src_len * 2;
        return 0;
    }

    cvt->len_cvt = src_len;
    return 0;
}

/* ---- surface / palette / BMP --------------------------------------------
 * Only used by the debug screenshot (util/screenshot.c), gated behind the
 * in-game "B" key which doesn't exist on the 3DS control scheme, so this
 * path is effectively dead code on-device — implemented anyway (writing a
 * real 8-bit BMP to sdmc:/3ds/wacki/) so nothing silently misbehaves if a
 * future control-scheme change wires it up. */

SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width,
                                                int height, int depth,
                                                int pitch, uint32_t format)
{
    (void)depth; (void)format;
    SDL_Surface *s = (SDL_Surface *)malloc(sizeof *s);
    if (!s) return NULL;
    s->w      = width;
    s->h      = height;
    s->pitch  = pitch;
    s->pixels = pixels;
    s->format = (SDL_PixelFormat *)malloc(sizeof *s->format);
    if (!s->format) { free(s); return NULL; }
    s->format->palette = (SDL_Palette *)malloc(sizeof *s->format->palette);
    if (!s->format->palette) { free(s->format); free(s); return NULL; }
    s->format->palette->ncolors = 256;
    s->format->palette->colors  = (SDL_Color *)calloc(256, sizeof(SDL_Color));
    return s;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface->format) {
        if (surface->format->palette) {
            free(surface->format->palette->colors);
            free(surface->format->palette);
        }
        free(surface->format);
    }
    free(surface);
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors)
{
    if (!palette || !palette->colors) return -1;
    for (int i = 0; i < ncolors && firstcolor + i < palette->ncolors; ++i)
        palette->colors[firstcolor + i] = colors[i];
    return 0;
}

/* Minimal uncompressed 8-bit indexed BMP writer (BITMAPFILEHEADER +
 * BITMAPINFOHEADER + 256-entry BGRA palette + bottom-up rows, padded to a
 * 4-byte stride) — exactly what an 8bpp SDL_Surface + palette needs. */
int SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
    if (!surface || !surface->pixels || !surface->format ||
        !surface->format->palette) return -1;

    FILE *fp = fopen(file, "wb");
    if (!fp) return -1;

    int w = surface->w, h = surface->h, pitch = surface->pitch;
    int row_bytes = (w + 3) & ~3;
    uint32_t palette_bytes = 256u * 4u;
    uint32_t pixel_bytes   = (uint32_t)row_bytes * (uint32_t)h;
    uint32_t data_off       = 14u + 40u + palette_bytes;
    uint32_t file_size      = data_off + pixel_bytes;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (uint8_t)(file_size);       hdr[3]  = (uint8_t)(file_size >> 8);
    hdr[4]  = (uint8_t)(file_size >> 16); hdr[5]  = (uint8_t)(file_size >> 24);
    hdr[10] = (uint8_t)(data_off);        hdr[11] = (uint8_t)(data_off >> 8);
    hdr[12] = (uint8_t)(data_off >> 16);  hdr[13] = (uint8_t)(data_off >> 24);
    hdr[14] = 40;                                             /* BITMAPINFOHEADER size */
    hdr[18] = (uint8_t)(w); hdr[19] = (uint8_t)(w >> 8);
    hdr[20] = (uint8_t)(w >> 16); hdr[21] = (uint8_t)(w >> 24);
    hdr[22] = (uint8_t)(h); hdr[23] = (uint8_t)(h >> 8);
    hdr[24] = (uint8_t)(h >> 16); hdr[25] = (uint8_t)(h >> 24);
    hdr[26] = 1; hdr[27] = 0;                                  /* planes = 1 */
    hdr[28] = 8; hdr[29] = 0;                                  /* bpp = 8 */
    hdr[46] = 0; hdr[47] = 1; hdr[48] = 0; hdr[49] = 0;         /* clrUsed = 256 */

    fwrite(hdr, 1, sizeof hdr, fp);

    for (int i = 0; i < 256; ++i) {
        SDL_Color c = surface->format->palette->colors[i];
        uint8_t entry[4] = { c.b, c.g, c.r, 0 };
        fwrite(entry, 1, 4, fp);
    }

    uint8_t *pad = (uint8_t *)calloc(1, (size_t)row_bytes);
    const uint8_t *src = (const uint8_t *)surface->pixels;
    for (int y = h - 1; y >= 0; --y) {
        memset(pad, 0, (size_t)row_bytes);
        memcpy(pad, src + (size_t)y * pitch, (size_t)w);
        fwrite(pad, 1, (size_t)row_bytes, fp);
    }
    free(pad);

    fclose(fp);
    return 0;
}
