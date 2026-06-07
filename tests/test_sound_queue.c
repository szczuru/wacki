/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * tests/test_sound_queue.c — positional alpha-tint source queue.
 *
 * Exercises the production SoundQueueReset / SoundQueueEnqueue /
 * AlphaTintForListener. NOTE: VM op 0x41/0x42 are dynamic alpha-plane
 * LIGHTING, not sound — the "sound" names are legacy. A source carries a
 * position,
 * an RGB tint (0x80 per channel = identity) and a falloff radius; the
 * blend at a listener point is a raised-cosine of distance, accumulated
 * across sources and clamped to 0xff per channel. Empty queue → 0x808080.
 * The packed result is 0xBBGGRR (R in the low byte, for SetAlphaTint).
 */

#include "test.h"
#include "wacki.h"

#include <stdint.h>

extern void     SoundQueueReset(void);
extern void     SoundQueueEnqueue(int16_t x, int16_t y, uint32_t rgb, uint16_t radius);
extern uint32_t AlphaTintForListener(int16_t lx, int16_t ly);

#define TINT_IDENTITY  0x00808080u
#define HUGE_RADIUS    50000   /* covers the whole screen (w≈1 everywhere) */

/* ---- empty / reset → identity -------------------------------------- */

TEST(empty_queue_returns_identity)
{
    SoundQueueReset();
    ASSERT_EQ(AlphaTintForListener(0, 0), TINT_IDENTITY);
}

TEST(reset_clears_previous_state)
{
    SoundQueueReset();
    SoundQueueEnqueue(100, 100, 0x808080, 200);
    SoundQueueReset();
    ASSERT_EQ(AlphaTintForListener(0, 0), TINT_IDENTITY);
}

/* ---- global darken source (the shipped "dark room" source) ---------- */

TEST(global_darken_source_uniform)
{
    /* rgb 0x404040 (half), huge radius → w≈1 everywhere → 0x404040 at
     * both a near and a far listener point. */
    SoundQueueReset();
    SoundQueueEnqueue(0, 0, 0x404040, HUGE_RADIUS);
    ASSERT_EQ(AlphaTintForListener(0, 0),     0x00404040u);
    ASSERT_EQ(AlphaTintForListener(300, 200), 0x00404040u);
}

/* ---- local light spot: bright at center, dark far away ------------- */

TEST(light_spot_bright_at_center_dark_far)
{
    SoundQueueReset();
    SoundQueueEnqueue(320, 240, 0x808080, 50);
    /* At the source: dist 0 → w=1 → 0x808080. */
    ASSERT_EQ(AlphaTintForListener(320, 240), TINT_IDENTITY);
    /* Far beyond radius*PI (~157 px): w=0 → black. */
    ASSERT_EQ(AlphaTintForListener(520, 240) & 0xFF, 0u);
}

/* ---- RGB byte order: R is the low byte (SetAlphaTint convention) ---- */

TEST(rgb_low_byte_is_red)
{
    SoundQueueReset();
    SoundQueueEnqueue(0, 0, 0x0000FF /*R=ff,G=0,B=0*/, HUGE_RADIUS);
    uint32_t t = AlphaTintForListener(0, 0);
    ASSERT_EQ(t & 0xFF,        0xFFu);   /* R */
    ASSERT_EQ((t >> 8)  & 0xFF, 0u);     /* G */
    ASSERT_EQ((t >> 16) & 0xFF, 0u);     /* B */
}

/* ---- zero radius contributes nothing ------------------------------- */

TEST(zero_radius_contributes_nothing)
{
    SoundQueueReset();
    SoundQueueEnqueue(0, 0, 0x808080, 0);
    /* Queue non-empty but the only source is skipped → all channels 0. */
    ASSERT_EQ(AlphaTintForListener(0, 0), 0u);
}

/* ---- accumulation + clamp ------------------------------------------ */

TEST(darken_plus_light_accumulates)
{
    /* The shipped pattern: global half-darken + a local identity spot.
     * At the spot they sum: 0x40 + 0x80 = 0xC0 per channel. */
    SoundQueueReset();
    SoundQueueEnqueue(0, 0, 0x404040, HUGE_RADIUS);
    SoundQueueEnqueue(0, 0, 0x808080, 50);
    ASSERT_EQ(AlphaTintForListener(0, 0) & 0xFF, 0xC0u);
}

TEST(channels_clamp_to_255)
{
    SoundQueueReset();
    for (int i = 0; i < 16; ++i) {
        SoundQueueEnqueue(0, 0, 0x808080, HUGE_RADIUS);
    }
    ASSERT_EQ(AlphaTintForListener(0, 0), 0x00FFFFFFu);
}

/* ---- monotonic distance falloff ------------------------------------ */

TEST(falloff_decreases_with_distance)
{
    SoundQueueReset();
    SoundQueueEnqueue(0, 0, 0x808080, 100);
    int near = AlphaTintForListener(0,   0) & 0xFF;
    int mid  = AlphaTintForListener(100, 0) & 0xFF;
    int far  = AlphaTintForListener(200, 0) & 0xFF;
    ASSERT_TRUE(near > mid);
    ASSERT_TRUE(mid  > far);
}

SUITE(sound_queue)
{
    RUN_TEST(empty_queue_returns_identity);
    RUN_TEST(reset_clears_previous_state);
    RUN_TEST(global_darken_source_uniform);
    RUN_TEST(light_spot_bright_at_center_dark_far);
    RUN_TEST(rgb_low_byte_is_red);
    RUN_TEST(zero_radius_contributes_nothing);
    RUN_TEST(darken_plus_light_accumulates);
    RUN_TEST(channels_clamp_to_255);
    RUN_TEST(falloff_decreases_with_distance);
}
