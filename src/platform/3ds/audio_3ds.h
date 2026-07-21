/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/audio_3ds.h — private declarations shared between
 * audio_3ds_ndsp.c (implementation), platform_3ds.c (per-frame poll
 * call site) and system_3ds.c (ndspExit at shutdown). */
#ifndef WACKI_3DS_AUDIO_H
#define WACKI_3DS_AUDIO_H

/* Refills any ndsp wave buffer that finished playing (mixer channel 0 +
 * AVI cutscene channel 1). ctrulib's own audio/streaming example fills
 * buffers this way — polled from the main loop — rather than from a
 * dedicated audio thread; our two ~93 ms mixer buffers comfortably
 * outlast one ~33 ms game frame, so polling once per PlatformPumpEvents
 * call (platform_3ds.c) keeps both channels fed with no extra thread
 * and no locking (everything stays on the main thread, so
 * plat_audio_lock/unlock — called by audio.c around s_mix[] mutation —
 * can be true no-ops). */
void plat_audio_3ds_poll(void);

/* Set once ndspInit() succeeds (lazily, on first plat_audio_open or
 * plat_avi_audio_begin call). system_3ds.c's plat_system_exit() checks
 * this before calling ndspExit() so shutdown is safe even if audio was
 * never touched during the run (e.g. --headless). */
extern int g_ndsp_ready;

#endif /* WACKI_3DS_AUDIO_H */
