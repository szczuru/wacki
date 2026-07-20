/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/audio_3ds_stub.c — audio-output HAL, 3DS (silent stub).
 *
 * Implements every entry point in include/wacki/platform/audio.h so the
 * engine links and runs with no audio hardware backend wired up yet.
 * plat_audio_open always reports failure (0), so the mixer
 * (mixer_ensure_open) never marks itself "open" and every audio.c call
 * gated behind plat_audio_is_open() is skipped — the game runs silent
 * but never touches uninitialized audio state.
 *
 * Swap this file for a real ndsp-backed implementation later without
 * touching any other 3DS platform file or the engine core. */

#include "wacki/platform/audio.h"

int plat_audio_open(int freq, int channels, plat_audio_pull_fn pull)
{
    (void)freq; (void)channels; (void)pull;
    return 0;   /* no channels obtained -> mixer stays "closed" */
}

void plat_audio_close(void) {}
int  plat_audio_is_open(void) { return 0; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

void plat_avi_audio_begin(int rate, int channels, int bits)
{
    (void)rate; (void)channels; (void)bits;
}
void plat_avi_audio_push(void *pcm, int len) { (void)pcm; (void)len; }
void plat_avi_audio_end(void) {}
int  plat_avi_audio_is_open(void) { return 0; }
int  plat_avi_audio_below_cushion(unsigned ms) { (void)ms; return 0; }
void plat_avi_audio_flush(void) {}
int  plat_avi_audio_needs_pump(void) { return 0; }
