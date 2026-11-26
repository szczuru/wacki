/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/perf_log_3ds.h — performance log on the SD card
 * (sdmc:/3ds/wacki/wacki_perf.log).
 *
 * The main thread only adds numbers to in-memory counters and copies
 * error lines into a small buffer. A separate low-priority thread turns
 * them into text and writes the file every 0.5 s: one write to the SD
 * card can take ~15 ms, which must never land inside a game frame.
 *
 * One line per 0.5 s window:
 *   t=12.5s fps=30.0 ticks=30.0 frame=33.3/41.2 engine=9.1/14.0
 *   draw=2.3/3.1 wait=0.4/1.2 gpu=1.1 sleep=21.0 skip=0 ...
 * (avg/max in ms). See perf_log_3ds.c for what each number covers. */
#ifndef WACKI_PERF_LOG_3DS_H
#define WACKI_PERF_LOG_3DS_H

#include <stdint.h>

/* Open the file and start the writer thread. Safe to call more than once. */
void perf_log_init(void);

/* Stop the thread and write what is left. */
void perf_log_shutdown(void);

/* Current time in CPU ticks (svcGetSystemTick). */
uint64_t perf_now(void);

/* Ticks → milliseconds. */
float perf_ms(uint64_t ticks);

/* One plat_video_present call. drew = 0 when the frame was skipped
 * (identical to the previous one). All times in ticks:
 *   draw  — CPU work in the present (palette → RGB565, GPU commands),
 *           without the wait for the GPU;
 *   wait  — time C3D_FrameBegin waited for the previous GPU frame;
 *   gpu_ms — the GPU's own time for the previous frame (citro3d). */
void perf_log_present(int drew, uint64_t draw, uint64_t wait, float gpu_ms);

/* Time the engine slept on purpose (SDL_Delay / frame pacing), so it can
 * be taken out of the engine's own time. */
void perf_log_sleep(uint64_t ticks);

/* A message for the log (errors, warnings, one-off events). Thread-safe;
 * printf-style. */
void perf_log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* A short state note put on every following line (e.g. "gpu smooth=1
 * aspect=4:3"). Copied; NULL or "" clears it. */
void perf_log_set_state(const char *state);

#endif /* WACKI_PERF_LOG_3DS_H */
