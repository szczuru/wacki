/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/audio_3ds_stub.c — Audio HAL stubs for 3DS.
 *
 * Audio not yet implemented - silent stubs to allow compilation. */

#include "wacki.h"

void plat_audio_init(int freq, int samples) { (void)freq; (void)samples; }
void plat_audio_shutdown(void) {}
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
void plat_audio_pause(int on) { (void)on; }
int  plat_audio_queue_size(void) { return 0; }
void plat_audio_queue(const void *buf, int len) { (void)buf; (void)len; }
void plat_audio_mix(void *dst, const void *src, int len, int vol) {
    (void)dst; (void)src; (void)len; (void)vol;
}
