/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/audio_3ds_ndsp.c — audio-output HAL, 3DS (ndsp).
 *
 * Two ndsp channels:
 *   channel 0 — the engine mixer (src/audio.c). Pull-based: two ~46ms
 *               PCM16-stereo wave buffers ping-pong, refilled from
 *               plat_audio_pull_fn whenever one finishes (polled, not
 *               threaded — see plat_audio_3ds_poll below).
 *   channel 1 — cutscene (AVI) audio. Push-based: plat_avi_audio_push
 *               copies each decoded chunk into a free slot from a small
 *               fixed pool and queues it; format/rate are taken directly
 *               from the source (ndsp resamples in hardware via
 *               ndspChnSetRate), so no manual conversion is needed here
 *               (unlike the SDL backend, which has to build an
 *               SDL_AudioStream when the device doesn't match).
 *
 * Both channels are serviced by plat_audio_3ds_poll(), called once per
 * frame from PlatformPumpEvents (platform_3ds.c) on the main thread —
 * the same thread that calls every plat_audio_ and plat_avi_audio_ entry
 * point, so plat_audio_lock/unlock can be true no-ops (ctrulib's own
 * audio/streaming example uses this exact main-loop-polls-wavebuf-status
 * pattern instead of a dedicated audio thread). */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/audio.h"
#include "audio_3ds.h"
#include <3ds.h>
#include <string.h>
#include <stdlib.h>

int g_ndsp_ready = 0;

static int ndsp_ensure_ready(void)
{
    if (g_ndsp_ready) return 1;
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        LOG_INFO("audio", "ndspInit failed: 0x%08lX (no DSP firmware dumped? "
                          "see 3ds-examples/audio/README — running silent)",
                 (unsigned long)rc);
        return 0;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    g_ndsp_ready = 1;
    return 1;
}

/* ---- mixer channel (ndsp channel 0) -------------------------------- */

#define MIX_NDSP_CHAN     0
/* ~46 ms/buffer at 22050 Hz — comfortably longer than one ~33 ms game
 * frame, so polling once per PlatformPumpEvents call never starves the
 * channel even if a frame runs long. */
#define MIX_BUF_FRAMES    1024
#define MIX_BUF_BYTES     (MIX_BUF_FRAMES * 4)   /* stereo S16 = 4 B/frame */

static int16_t          *s_mix_buf[2]  = { NULL, NULL };
static ndspWaveBuf        s_mix_wave[2];
static plat_audio_pull_fn s_mix_pull   = NULL;
static int                s_mix_open   = 0;

static void mix_fill(int slot)
{
    if (s_mix_pull) s_mix_pull(s_mix_buf[slot], MIX_BUF_BYTES);
    else            memset(s_mix_buf[slot], 0, MIX_BUF_BYTES);
    DSP_FlushDataCache(s_mix_buf[slot], MIX_BUF_BYTES);
}

int plat_audio_open(int freq, int channels, plat_audio_pull_fn pull)
{
    (void)channels;   /* mixer always produces stereo (MIX_OUT_CHANS) */
    if (s_mix_open) return 2;

    if (!ndsp_ensure_ready()) return 0;

    s_mix_pull = pull;

    for (int i = 0; i < 2; ++i) {
        s_mix_buf[i] = (int16_t *)linearAlloc(MIX_BUF_BYTES);
        if (!s_mix_buf[i]) {
            LOG_INFO("audio", "linearAlloc mixer buffer %d failed", i);
            return 0;
        }
    }

    ndspChnReset(MIX_NDSP_CHAN);
    ndspChnInitParams(MIX_NDSP_CHAN);
    ndspChnSetInterp(MIX_NDSP_CHAN, NDSP_INTERP_LINEAR);
    ndspChnSetRate(MIX_NDSP_CHAN, (float)freq);
    ndspChnSetFormat(MIX_NDSP_CHAN, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = 1.0f;  /* front-left */
    mix[1] = 1.0f;  /* front-right */
    ndspChnSetMix(MIX_NDSP_CHAN, mix);

    memset(s_mix_wave, 0, sizeof s_mix_wave);
    for (int i = 0; i < 2; ++i) {
        s_mix_wave[i].data_vaddr = s_mix_buf[i];
        s_mix_wave[i].nsamples   = MIX_BUF_FRAMES;
        s_mix_wave[i].looping    = false;
        mix_fill(i);
        ndspChnWaveBufAdd(MIX_NDSP_CHAN, &s_mix_wave[i]);
    }

    s_mix_open = 1;
    LOG_INFO("audio", "ndsp mixer opened: %d Hz stereo, %d-frame buffers",
             freq, MIX_BUF_FRAMES);
    return 2;
}

void plat_audio_close(void)
{
    if (!s_mix_open) return;
    ndspChnWaveBufClear(MIX_NDSP_CHAN);
    for (int i = 0; i < 2; ++i) {
        if (s_mix_buf[i]) { linearFree(s_mix_buf[i]); s_mix_buf[i] = NULL; }
    }
    s_mix_open = 0;
    s_mix_pull = NULL;
    LOG_INFO("audio", "ndsp mixer closed");
}

int plat_audio_is_open(void) { return s_mix_open; }

/* Single-threaded polling model (see file header) — the mixer channel
 * array is only ever touched from the main thread, so there is nothing
 * to serialise against. */
void plat_audio_lock(void)   {}
void plat_audio_unlock(void) {}

/* ---- cutscene (AVI) audio channel (ndsp channel 1) ------------------ */

#define AVI_NDSP_CHAN    1
#define AVI_CHUNK_BYTES  8192          /* generous vs. one video-frame's
                                         * worth of PCM at any supported
                                         * rate/format */
#define AVI_CHUNK_COUNT  6              /* ~ up to 6 chunks in flight */

typedef struct {
    void       *buf;         /* linearAlloc'd, AVI_CHUNK_BYTES, reused */
    ndspWaveBuf wave;
    int         active;      /* queued/playing, not yet observed DONE */
    uint32_t    frames;      /* frame count committed at push time (our
                              * own copy — used for the cushion estimate,
                              * independent of whatever ndsp does to the
                              * wave buf fields after playback) */
} AviChunk;

static AviChunk s_avi_chunk[AVI_CHUNK_COUNT];
static int      s_avi_open        = 0;
static int      s_avi_bytes_per_frame = 4;   /* recomputed in begin() */
static int      s_avi_rate        = 0;
static uint32_t s_avi_queued_frames = 0;      /* frames not yet finished */

static void avi_pool_free_all(void)
{
    for (int i = 0; i < AVI_CHUNK_COUNT; ++i) {
        if (s_avi_chunk[i].buf) { linearFree(s_avi_chunk[i].buf); s_avi_chunk[i].buf = NULL; }
        s_avi_chunk[i].active = 0;
        s_avi_chunk[i].frames = 0;
    }
    s_avi_queued_frames = 0;
}

void plat_avi_audio_begin(int rate, int channels, int bits)
{
    if (!ndsp_ensure_ready()) return;

    u16 format;
    if (channels <= 1)
        format = (bits == 8) ? NDSP_FORMAT_MONO_PCM8   : NDSP_FORMAT_MONO_PCM16;
    else
        format = (bits == 8) ? NDSP_FORMAT_STEREO_PCM8 : NDSP_FORMAT_STEREO_PCM16;

    if (s_avi_open && s_avi_rate == rate &&
        s_avi_bytes_per_frame == (channels <= 1 ? 1 : 2) * (bits == 8 ? 1 : 2))
        return;   /* same format already set up — keep the pool as-is */

    if (s_avi_open) {
        ndspChnWaveBufClear(AVI_NDSP_CHAN);
        avi_pool_free_all();
    }

    for (int i = 0; i < AVI_CHUNK_COUNT; ++i) {
        s_avi_chunk[i].buf = linearAlloc(AVI_CHUNK_BYTES);
        if (!s_avi_chunk[i].buf) {
            LOG_INFO("audio", "linearAlloc AVI chunk %d failed", i);
            avi_pool_free_all();
            return;
        }
    }

    ndspChnReset(AVI_NDSP_CHAN);
    ndspChnInitParams(AVI_NDSP_CHAN);
    ndspChnSetInterp(AVI_NDSP_CHAN, NDSP_INTERP_LINEAR);
    ndspChnSetRate(AVI_NDSP_CHAN, (float)rate);
    ndspChnSetFormat(AVI_NDSP_CHAN, format);

    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(AVI_NDSP_CHAN, mix);

    s_avi_rate             = rate;
    s_avi_bytes_per_frame  = (channels <= 1 ? 1 : 2) * (bits == 8 ? 1 : 2);
    s_avi_open             = 1;

    LOG_INFO("audio", "ndsp AVI channel opened: %d Hz, %d ch, %d-bit",
             rate, channels, bits);
}

void plat_avi_audio_push(void *pcm, int len)
{
    if (!s_avi_open || len <= 0) return;

    const uint8_t *src = (const uint8_t *)pcm;
    while (len > 0) {
        int take = (len > AVI_CHUNK_BYTES) ? AVI_CHUNK_BYTES : len;

        /* Find a free slot. If the whole pool is backed up (shouldn't
         * happen — the cushion check in flic.c throttles the decoder
         * well before this many chunks could queue), drop the
         * remainder rather than block or overwrite a playing buffer. */
        int slot = -1;
        for (int i = 0; i < AVI_CHUNK_COUNT; ++i) {
            if (!s_avi_chunk[i].active) { slot = i; break; }
        }
        if (slot < 0) return;

        memcpy(s_avi_chunk[slot].buf, src, (size_t)take);
        DSP_FlushDataCache(s_avi_chunk[slot].buf, (u32)take);

        memset(&s_avi_chunk[slot].wave, 0, sizeof s_avi_chunk[slot].wave);
        s_avi_chunk[slot].wave.data_vaddr = s_avi_chunk[slot].buf;
        s_avi_chunk[slot].wave.nsamples   = (u32)(take / s_avi_bytes_per_frame);
        s_avi_chunk[slot].wave.looping    = false;
        s_avi_chunk[slot].frames          = s_avi_chunk[slot].wave.nsamples;
        s_avi_chunk[slot].active          = 1;
        s_avi_queued_frames += s_avi_chunk[slot].frames;

        ndspChnWaveBufAdd(AVI_NDSP_CHAN, &s_avi_chunk[slot].wave);

        src += take;
        len -= take;
    }
}

void plat_avi_audio_end(void)
{
    if (!s_avi_open) return;
    ndspChnWaveBufClear(AVI_NDSP_CHAN);
    avi_pool_free_all();
    s_avi_open = 0;
}

int plat_avi_audio_is_open(void) { return s_avi_open; }

int plat_avi_audio_below_cushion(unsigned ms)
{
    if (!s_avi_open || s_avi_rate <= 0) return 0;
    uint32_t cushion_frames = (uint32_t)s_avi_rate * ms / 1000u;
    return s_avi_queued_frames < cushion_frames;
}

void plat_avi_audio_flush(void)
{
    if (!s_avi_open) return;
    ndspChnWaveBufClear(AVI_NDSP_CHAN);
    for (int i = 0; i < AVI_CHUNK_COUNT; ++i) {
        s_avi_chunk[i].active = 0;
        s_avi_chunk[i].frames = 0;
    }
    s_avi_queued_frames = 0;
}

/* ndsp's own small hardware queue is what provides the cushion here
 * (via the chunk pool above); the decoder does not need to be pumped
 * separately between video frames. */
int plat_avi_audio_needs_pump(void) { return 0; }

/* ---- per-frame service (called from PlatformPumpEvents) ------------ */

void plat_audio_3ds_poll(void)
{
    if (s_mix_open) {
        for (int i = 0; i < 2; ++i) {
            if (s_mix_wave[i].status == NDSP_WBUF_DONE) {
                mix_fill(i);
                ndspChnWaveBufAdd(MIX_NDSP_CHAN, &s_mix_wave[i]);
            }
        }
    }

    if (s_avi_open) {
        for (int i = 0; i < AVI_CHUNK_COUNT; ++i) {
            if (s_avi_chunk[i].active && s_avi_chunk[i].wave.status == NDSP_WBUF_DONE) {
                s_avi_chunk[i].active = 0;
                if (s_avi_queued_frames >= s_avi_chunk[i].frames)
                    s_avi_queued_frames -= s_avi_chunk[i].frames;
                else
                    s_avi_queued_frames = 0;
            }
        }
    }
}
