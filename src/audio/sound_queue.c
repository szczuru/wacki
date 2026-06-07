/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/audio/sound_queue.c — positional ALPHA-TINT source queue.
 *
 * NOTE: despite the "sound" names (kept for now to bound the rename),
 * VM op 0x41 / 0x42 are NOT sound — they drive the original's dynamic
 * colored-lighting on alpha-plane sprites:
 *
 *   op 0x41: push a tint source (x, y, RGB packed in a u32, radius) onto
 *            a 16-entry queue.
 *   op 0x42: reset the queue (does NOT touch audio).
 *   The blend at a point sums every source with a raised-cosine distance
 *   falloff → packed 0xBBGGRR; empty queue → 0x808080 (identity). The
 *   entity render feeds the result to SetAlphaTint.
 *
 * Real SFX are unrelated — they fire frame-driven from the [sampl] tags
 * in Wacky.scr (TriggerFrameSfx). The shipped game uses op 0x41 in just
 * two scenes ("dark room + light spot"): a global ~50% darken source
 * plus one small identity-bright spot.
 *
 * AlphaTintForListener reproduces the original's blend exactly, with the
 * float pipeline replaced by an integer raised-cosine LUT + integer sqrt
 * so we pull in no libm (handheld targets link without it). Decompiler
 * addresses for this system live in ANALYSIS.md.
 */

#include "wacki.h"
#include "wacki/log.h"

#include <stdint.h>
#include <stdio.h>

/* ---- queue storage ------------------------------------------------- */

#define TINT_SOURCE_MAX  16

typedef struct TintSource {
    int16_t  x, y;       /* source position (screen px) */
    uint16_t radius;     /* falloff radius (original "volume" arg)   */
    uint8_t  rgb[3];     /* R, G, B (0x80 = identity per channel)    */
    uint8_t  pad;
} TintSource;

static TintSource s_tint_src[TINT_SOURCE_MAX];
static uint16_t   s_tint_count = 0;

/* op 0x42 — clear all tint sources. Audio is untouched. Also called on
 * every komnata load (scene/komnata.c) so a scene's tint never leaks
 * into the next room. */
void SoundQueueReset(void)
{
    s_tint_count = 0;
}

/* op 0x41 — push one tint source. `rgb` packs R/G/B in the low three
 * bytes; `radius` is the falloff distance. */
void SoundQueueEnqueue(int16_t x, int16_t y, uint32_t rgb, uint16_t radius)
{
    if (s_tint_count >= TINT_SOURCE_MAX) return;

    TintSource *e = &s_tint_src[s_tint_count++];
    e->x      = x;
    e->y      = y;
    e->radius = radius;
    e->rgb[0] = (uint8_t)(rgb      );
    e->rgb[1] = (uint8_t)(rgb >>  8);
    e->rgb[2] = (uint8_t)(rgb >> 16);
}

/* ---- positional tint blend ----------------------------------------- */

/* Identity tint: every channel at 0x80 (Q1.7 1.0 in the alpha blit). */
#define TINT_IDENTITY_PACKED   0x00808080u

/* Raised-cosine falloff w(t) = (cos(pi*t) + 1) / 2 in Q0.8 (0..256),
 * t = i / TINT_LUT_N over [0,1]. This folds the original's
 * (cos(min(dist/radius, PI)) - (-1.0)) * 0.5 — i.e. C1=-1.0, C2=0.5,
 * clamp=PI — into a table indexed by
 * t = min(dist/radius, PI) / PI. Computed offline. */
#define TINT_LUT_N   32
static const uint16_t s_falloff_lut[TINT_LUT_N + 1] = {
    256, 255, 254, 250, 246, 241, 234, 227, 219,
    209, 199, 188, 177, 165, 153, 141, 128, 115,
    103,  91,  79,  68,  57,  47,  37,  29,  22,
     15,  10,   6,   2,   1,   0
};

/* PI in Q14 fixed point (3.14159 * 16384). Used to scale `radius` to the
 * distance at which the falloff reaches zero (dist = radius * PI). */
#define TINT_PI_Q14  51472u

static uint32_t isqrt32(uint32_t v)
{
    uint32_t res = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
        else                {                 res >>= 1; }
        bit >>= 2;
    }
    return res;
}

/* Blend all active tint sources at (lx, ly) and return the packed
 * 0xBBGGRR tint (R in the low byte, matching SetAlphaTint). Empty queue
 * → identity. */
uint32_t AlphaTintForListener(int16_t lx, int16_t ly)
{
    if (s_tint_count == 0) return TINT_IDENTITY_PACKED;

    int acc[3] = { 0, 0, 0 };
    for (uint16_t i = 0; i < s_tint_count; ++i) {
        const TintSource *s = &s_tint_src[i];
        if (s->radius == 0) continue;

        int dx = (int)lx - (int)s->x;
        int dy = (int)ly - (int)s->y;
        /* Original weights the vertical delta 2× (iso foreshortening). */
        uint32_t dist = isqrt32((uint32_t)(dx * dx + 2 * dy * dy));

        /* idx (Q8) = (dist / (radius*PI)) * TINT_LUT_N, clamped to N. */
        uint32_t radius_pi = (uint32_t)(((uint64_t)s->radius * TINT_PI_Q14) >> 14);
        if (radius_pi == 0) radius_pi = 1;
        uint64_t idx_q8 = ((uint64_t)dist * TINT_LUT_N * 256u) / radius_pi;

        uint32_t w;  /* Q0.8 raised-cosine weight, 0..256 */
        uint32_t idx = (uint32_t)(idx_q8 >> 8);
        if (idx >= TINT_LUT_N) {
            w = s_falloff_lut[TINT_LUT_N];           /* = 0 */
        } else {
            uint32_t frac = (uint32_t)(idx_q8 & 0xff);
            int a = s_falloff_lut[idx];
            int b = s_falloff_lut[idx + 1];
            w = (uint32_t)(a + ((b - a) * (int)frac) / 256);
        }

        acc[0] += (int)((w * s->rgb[0]) >> 8);
        acc[1] += (int)((w * s->rgb[1]) >> 8);
        acc[2] += (int)((w * s->rgb[2]) >> 8);
    }
    for (int c = 0; c < 3; ++c) {
        if (acc[c] > 255) acc[c] = 255;
        if (acc[c] < 0)   acc[c] = 0;
    }
    return (uint32_t)((acc[2] << 16) | (acc[1] << 8) | acc[0]);
}

/* ---- script bridges ------------------------------------------------- */

/* op 0x41. Maps to: x=reg_id, y=a1, rgb=u32, radius=a2. */
void ScriptCallSoundPlay(uint16_t id, uint16_t a, uint32_t b, uint16_t c)
{
    SoundQueueEnqueue((int16_t)id, (int16_t)a, b, c);
    LOG_TRACE("tint", "source #%u at (%d,%d) rgb=0x%06x radius=%u",
              s_tint_count, (int)(int16_t)id, (int)(int16_t)a,
              (unsigned)(b & 0xffffff), c);
}

/* op 0x42. Only zeroes the queue — it does NOT stop audio (an earlier
 * port mistakenly called StopMenuMusic here, which cut the track whenever
 * a scene reset its tint sources). */
void ScriptCallSoundStop(void)
{
    SoundQueueReset();
}
