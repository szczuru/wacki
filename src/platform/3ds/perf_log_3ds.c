/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/perf_log_3ds.c — performance log on the SD card.
 * See perf_log_3ds.h for the idea; this file has the details.
 *
 * What the numbers on a line mean (avg/max over the 0.5 s window, ms):
 *   fps    — frames actually drawn per second (skipped identical
 *            frames not counted);
 *   ticks  — plat_video_present calls per second (drawn + skipped);
 *   frame  — time from one present to the next (the whole game loop);
 *   engine — frame minus draw, wait and sleep: the engine's own work
 *            (scripts, actors, painting the 8-bit frame, audio pump);
 *   draw   — CPU time inside the present (palette → RGB565, the GPU
 *            command list), without waiting for the GPU;
 *   wait   — time C3D_FrameBegin waited for the GPU's previous frame;
 *   gpu    — the GPU's own time for the last frame (citro3d);
 *   sleep  — time the engine slept on purpose (frame pacing to 30 fps)
 *            per frame;
 *   skip   — frames skipped because nothing changed;
 *   lin/vram — free linear / VRAM memory in KB.
 * Error and warning lines (LOG_WARN / LOG_ERROR, GPU failures) are
 * written as they come, prefixed with "!".
 *
 * Threading: the main thread takes a LightLock only for a few additions
 * per frame. The writer thread runs at a LOWER priority than the main
 * thread (and on core 2 on a New 3DS when the system allows it), so it
 * only gets the CPU while the game sleeps or waits for the GPU — the SD
 * write never steals time from a frame. */

#include "perf_log_3ds.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef WACKI_VERSION
#define WACKI_VERSION "dev"
#endif

#define PERF_LOG_PATH      "sdmc:/3ds/wacki/wacki_perf.log"
#define PERF_WINDOW_NS     500000000LL     /* 0.5 s */
#define PERF_MSG_BUF       8192
#define PERF_STACK         (16 * 1024)
#define TICKS_PER_MS       268111.856f     /* SYSCLOCK_ARM11 / 1000 */

typedef struct {
    uint32_t presents, drawn, skipped;
    uint64_t frame_sum, frame_max;
    uint64_t engine_sum, engine_max;
    uint64_t draw_sum, draw_max;
    uint64_t wait_sum, wait_max;
    uint64_t sleep_sum;
    float    gpu_last;
} PerfWindow;

static LightLock  s_lock;
static LightEvent s_wake;
static Thread     s_thread;
static FILE      *s_file;
static volatile int s_stop;
static int        s_started;

static PerfWindow s_win;               /* guarded by s_lock */
static char       s_msgs[PERF_MSG_BUF];/* guarded by s_lock */
static size_t     s_msgs_len;
static uint32_t   s_msgs_dropped;
static char       s_state[96];         /* guarded by s_lock */

/* main thread only */
static uint64_t   s_prev_present;      /* tick of the previous present's end */
static uint64_t   s_sleep_pending;     /* sleep since the previous present */
static uint64_t   s_start;

uint64_t perf_now(void) { return svcGetSystemTick(); }

float perf_ms(uint64_t ticks) { return (float)ticks / TICKS_PER_MS; }

/* newlib's printf on devkitARM handles %f, but the rest of the 3DS port
 * avoids it (see video_3ds_gl.c) — keep to integers: x.y ms. */
static int fmt_ms(char *out, size_t n, uint64_t ticks)
{
    uint32_t tenths = (uint32_t)((ticks * 10ULL + (uint64_t)(TICKS_PER_MS / 2)) /
                                 (uint64_t)TICKS_PER_MS);
    return snprintf(out, n, "%lu.%lu", (unsigned long)(tenths / 10),
                    (unsigned long)(tenths % 10));
}

static void write_window(const PerfWindow *w, uint64_t now, uint64_t window,
                         const char *state)
{
    if (!s_file) return;
    char a[16], b[16], line[512];
    int  n = 0;
    uint32_t t_tenths = (uint32_t)((now - s_start) / (uint64_t)(TICKS_PER_MS * 100.0f));
    float    secs = perf_ms(window) / 1000.0f;
    if (secs <= 0.0f) secs = 0.5f;
    unsigned fps10   = (unsigned)(w->drawn    / secs * 10.0f + 0.5f);
    unsigned ticks10 = (unsigned)(w->presents / secs * 10.0f + 0.5f);

    n += snprintf(line + n, sizeof line - n, "t=%lu.%lus fps=%u.%u ticks=%u.%u",
                  (unsigned long)(t_tenths / 10), (unsigned long)(t_tenths % 10),
                  fps10 / 10, fps10 % 10, ticks10 / 10, ticks10 % 10);
    if (w->presents) {
        uint32_t p = w->presents;
        fmt_ms(a, sizeof a, w->frame_sum / p);  fmt_ms(b, sizeof b, w->frame_max);
        n += snprintf(line + n, sizeof line - n, " frame=%s/%s", a, b);
        fmt_ms(a, sizeof a, w->engine_sum / p); fmt_ms(b, sizeof b, w->engine_max);
        n += snprintf(line + n, sizeof line - n, " engine=%s/%s", a, b);
        fmt_ms(a, sizeof a, w->sleep_sum / p);
        n += snprintf(line + n, sizeof line - n, " sleep=%s", a);
    }
    if (w->drawn) {
        uint32_t d = w->drawn;
        fmt_ms(a, sizeof a, w->draw_sum / d);   fmt_ms(b, sizeof b, w->draw_max);
        n += snprintf(line + n, sizeof line - n, " draw=%s/%s", a, b);
        fmt_ms(a, sizeof a, w->wait_sum / d);   fmt_ms(b, sizeof b, w->wait_max);
        n += snprintf(line + n, sizeof line - n, " wait=%s/%s", a, b);
        unsigned g10 = (unsigned)(w->gpu_last * 10.0f + 0.5f);
        n += snprintf(line + n, sizeof line - n, " gpu=%u.%u", g10 / 10, g10 % 10);
    }
    n += snprintf(line + n, sizeof line - n, " skip=%lu lin=%luK vram=%luK",
                  (unsigned long)w->skipped,
                  (unsigned long)(linearSpaceFree() / 1024),
                  (unsigned long)(vramSpaceFree() / 1024));
    if (state[0] && n < (int)sizeof line - 4)
        n += snprintf(line + n, sizeof line - n, " [%s]", state);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    fwrite(line, 1, (size_t)n, s_file);
}

static void flush_once(uint64_t *last)
{
    PerfWindow w;
    char       msgs[PERF_MSG_BUF];
    size_t     msgs_len;
    uint32_t   dropped;
    char       state[sizeof s_state];

    LightLock_Lock(&s_lock);
    w = s_win;
    memset(&s_win, 0, sizeof s_win);
    s_win.gpu_last = w.gpu_last;
    msgs_len = s_msgs_len;
    memcpy(msgs, s_msgs, msgs_len);
    s_msgs_len = 0;
    dropped = s_msgs_dropped;
    s_msgs_dropped = 0;
    memcpy(state, s_state, sizeof state);
    LightLock_Unlock(&s_lock);

    uint64_t now = perf_now();
    if (s_file) {
        if (msgs_len) fwrite(msgs, 1, msgs_len, s_file);
        if (dropped) fprintf(s_file, "! (%lu messages dropped, buffer full)\n",
                             (unsigned long)dropped);
        if (w.presents || msgs_len)
            write_window(&w, now, now - *last, state);
        fflush(s_file);
    }
    *last = now;
}

static void writer_thread(void *arg)
{
    (void)arg;
    uint64_t last = perf_now();
    while (!s_stop) {
        LightEvent_WaitTimeout(&s_wake, PERF_WINDOW_NS);
        flush_once(&last);
    }
    flush_once(&last);
}

void perf_log_init(void)
{
    if (s_started) return;
    s_started = 1;
    LightLock_Init(&s_lock);
    LightEvent_Init(&s_wake, RESET_ONESHOT);
    s_start = s_prev_present = perf_now();

    s_file = fopen(PERF_LOG_PATH, "w");
    if (s_file) {
        bool n3ds = false;
        APT_CheckNew3DS(&n3ds);
        fprintf(s_file,
                "# wacki %s perf log (3DS, GPU backend citro3d), %s\n"
                "# per 0.5 s: avg/max ms; engine = frame - draw - wait - sleep\n",
                WACKI_VERSION, n3ds ? "New 3DS" : "Old 3DS");
        fflush(s_file);
    }

    /* Lower priority than the caller (a bigger number is lower). */
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio += 1;
    if (prio > 0x3F) prio = 0x3F;

    s_stop = 0;
    s_thread = NULL;
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (n3ds) s_thread = threadCreate(writer_thread, NULL, PERF_STACK, prio, 2, false);
    if (!s_thread) s_thread = threadCreate(writer_thread, NULL, PERF_STACK, prio, -2, false);
    if (!s_thread && s_file)
        fprintf(s_file, "! writer thread not started: lines are written at shutdown only\n");
}

void perf_log_shutdown(void)
{
    if (!s_started) return;
    if (s_thread) {
        s_stop = 1;
        LightEvent_Signal(&s_wake);
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    } else {
        uint64_t last = s_start;
        flush_once(&last);
    }
    if (s_file) { fclose(s_file); s_file = NULL; }
    s_started = 0;
}

void perf_log_present(int drew, uint64_t draw, uint64_t wait, float gpu_ms)
{
    if (!s_started) return;
    uint64_t now   = perf_now();
    uint64_t frame = now - s_prev_present;
    uint64_t sleep = s_sleep_pending;
    s_prev_present  = now;
    s_sleep_pending = 0;

    uint64_t busy   = draw + wait + sleep;
    uint64_t engine = frame > busy ? frame - busy : 0;

    LightLock_Lock(&s_lock);
    PerfWindow *w = &s_win;
    w->presents++;
    w->frame_sum += frame;   if (frame  > w->frame_max)  w->frame_max  = frame;
    w->engine_sum += engine; if (engine > w->engine_max) w->engine_max = engine;
    w->sleep_sum += sleep;
    if (drew) {
        w->drawn++;
        w->draw_sum += draw; if (draw > w->draw_max) w->draw_max = draw;
        w->wait_sum += wait; if (wait > w->wait_max) w->wait_max = wait;
        w->gpu_last = gpu_ms;
    } else {
        w->skipped++;
    }
    LightLock_Unlock(&s_lock);
}

void perf_log_sleep(uint64_t ticks)
{
    s_sleep_pending += ticks;
}

void perf_log_msg(const char *fmt, ...)
{
    if (!s_started) return;
    char    line[256];
    int     n;
    uint32_t t = (uint32_t)((perf_now() - s_start) / (uint64_t)(TICKS_PER_MS * 100.0f));
    n = snprintf(line, sizeof line, "! t=%lu.%lus ", (unsigned long)(t / 10),
                 (unsigned long)(t % 10));
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';

    LightLock_Lock(&s_lock);
    if (s_msgs_len + (size_t)n <= sizeof s_msgs) {
        memcpy(s_msgs + s_msgs_len, line, (size_t)n);
        s_msgs_len += (size_t)n;
    } else {
        s_msgs_dropped++;
    }
    LightLock_Unlock(&s_lock);
}

void perf_log_set_state(const char *state)
{
    if (!s_started) return;
    LightLock_Lock(&s_lock);
    if (state) {
        strncpy(s_state, state, sizeof s_state - 1);
        s_state[sizeof s_state - 1] = '\0';
    } else {
        s_state[0] = '\0';
    }
    LightLock_Unlock(&s_lock);
}
