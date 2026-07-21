/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/platform_3ds.c — top-level Platform* entry points
 * (the 3DS replacement for src/platform/sdl/platform_sdl.c).
 *
 * Wires together the HAL pieces implemented elsewhere in this
 * directory (system_3ds.c, video_3ds_gl.c, gamepad_3ds.c) behind the
 * portable Platform* API every src/*.c call site uses (wacki/api.h). */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"
#include "gamepad_3ds.h"
#include "audio_3ds.h"
#include <3ds.h>

/* Set by SDL_compat.c's SDL_PushEvent(&quit_event) — main.c's SIGINT
 * handler is the only caller; there is no real signal delivery on 3DS
 * homebrew, but keeping the same code path means main.c needs no
 * #ifdef for this platform. */
extern int g_sdl_compat_quit_requested;

static int plat_should_quit(void)
{
    return g_sdl_compat_quit_requested || !aptMainLoop();
}

/* ---- virtual cursor (D-pad + circle pad) ------------------------------
 *
 * BUG THIS FIXES: gamepad_3ds.c's platform_pad_read_motion() was fully
 * implemented (D-pad dx/dy + circle-pad ax/ay) from day one, but nothing
 * on this platform ever CALLED it — platform_sdl.c's per-frame virtual-
 * cursor poll (poll_virtual_cursor(), which is the only caller of
 * platform_pad_read_motion on every other platform) was never ported
 * over when platform_sdl.c was replaced wholesale by this file. Buttons
 * worked (platform_pad_handle_buttons() is called directly from
 * PlatformPumpEvents below) but the cursor never moved via D-pad/stick —
 * exactly the reported symptom. Ported the same accel-ramp + analog
 * sub-pixel-remainder logic platform_sdl.c uses, driving the shared
 * g_mouse_x/g_mouse_y globals video_3ds_gl.c and the engine's own click
 * hit-testing both read. */
#define VCUR_BASE_PIXELS_PER_TICK 1
#define VCUR_MAX_PIXELS_PER_TICK  8
#define VCUR_ACCEL_TICKS          10

static int   s_vcur_x = WACKI_SCREEN_W / 2, s_vcur_y = WACKI_SCREEN_H / 2;
static int   s_vcur_initialized = 0;
static int   s_vcur_hold_ticks  = 0;
static float s_vcur_rem_x = 0.0f, s_vcur_rem_y = 0.0f;

static void poll_virtual_cursor(void)
{
    if (!s_vcur_initialized) {
        s_vcur_x = g_mouse_x ? g_mouse_x : WACKI_SCREEN_W / 2;
        s_vcur_y = g_mouse_y ? g_mouse_y : WACKI_SCREEN_H / 2;
        s_vcur_initialized = 1;
        g_mouse_x = (int16_t)s_vcur_x;
        g_mouse_y = (int16_t)s_vcur_y;
    }

    int dx = 0, dy = 0;
    float ax = 0.0f, ay = 0.0f;
    platform_pad_read_motion(&dx, &dy, &ax, &ay);

    if (dx == 0 && dy == 0 && ax == 0.0f && ay == 0.0f) {
        s_vcur_hold_ticks = 0;
        s_vcur_rem_x = s_vcur_rem_y = 0.0f;
        return;
    }

    if (dx != 0 || dy != 0) {
        if (dx >  1) dx =  1; if (dx < -1) dx = -1;
        if (dy >  1) dy =  1; if (dy < -1) dy = -1;
        int spd = VCUR_BASE_PIXELS_PER_TICK +
            (s_vcur_hold_ticks * (VCUR_MAX_PIXELS_PER_TICK - VCUR_BASE_PIXELS_PER_TICK))
            / VCUR_ACCEL_TICKS;
        if (spd > VCUR_MAX_PIXELS_PER_TICK) spd = VCUR_MAX_PIXELS_PER_TICK;
        s_vcur_x += dx * spd;
        s_vcur_y += dy * spd;
        ++s_vcur_hold_ticks;
    } else {
        s_vcur_hold_ticks = 0;
    }

    /* Analog circle pad: proportional, carrying the sub-pixel remainder
     * so a gentle push still creeps the cursor for fine aiming. */
    s_vcur_rem_x += ax;
    s_vcur_rem_y += ay;
    int mvx = (int)s_vcur_rem_x; s_vcur_rem_x -= (float)mvx; s_vcur_x += mvx;
    int mvy = (int)s_vcur_rem_y; s_vcur_rem_y -= (float)mvy; s_vcur_y += mvy;

    if (s_vcur_x < 0) s_vcur_x = 0;
    if (s_vcur_y < 0) s_vcur_y = 0;
    if (s_vcur_x >= WACKI_SCREEN_W) s_vcur_x = WACKI_SCREEN_W - 1;
    if (s_vcur_y >= WACKI_SCREEN_H) s_vcur_y = WACKI_SCREEN_H - 1;

    g_mouse_x = (int16_t)s_vcur_x;
    g_mouse_y = (int16_t)s_vcur_y;
}

int PlatformInit(int w, int h, const char *title)
{
    /* plat_system_early_init() already ran from WackiMain (see main.c)
     * before FindDataRoot — gfx/hid/romfs services are up by the time
     * we get here. */
    platform_pad_open();

    if (!plat_video_init(w, h, title)) {
        LOG_INFO("platform", "video init failed");
        return 0;
    }
    return 1;
}

void PlatformShutdown(void)
{
    plat_video_shutdown();
    /* plat_system_exit(rc) is called separately by main()'s final
     * teardown (matches every other platform's main.c contract). */
}

void PlatformPresent(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    plat_video_present(shadow, pal, w, h);
}

void PlatformPumpEvents(void)
{
    platform_pad_handle_buttons();
    /* Runs AFTER platform_pad_handle_buttons(): when the touch screen
     * just set g_mouse_x/y directly, poll_virtual_cursor()'s "not moved
     * this tick" early-out (dx==dy==0 && ax==ay==0, the common case when
     * only touch was used) returns before touching g_mouse_x/y again,
     * so a touch tap is never immediately overwritten by the D-pad/
     * circle-pad cursor's own (unrelated) last known position. */
    poll_virtual_cursor();
    /* Refills any ndsp wave buffer that finished since last frame — see
     * audio_3ds.h / audio_3ds_ndsp.c for why this is polled here rather
     * than serviced from a dedicated audio thread. */
    plat_audio_3ds_poll();
}

int PlatformShouldQuit(void)
{
    return plat_should_quit();
}

void PlatformShowMessageBox(const char *title, const char *body)
{
    plat_video_message_box(title, body);
}

/* Inline text-entry (save-slot rename). 3DS ships a software keyboard
 * applet (swkbd) but wiring the full applet flow is out of scope for
 * this port pass; the typed-char queue simply stays empty, so the
 * rename UI is present but produces no characters yet — no worse than
 * not having the feature, and the queue plumbing is already correct
 * for a future swkbdInputText() hookup. */
void PlatformSetTextInput(int on)
{
    (void)on;
}

uint8_t PlatformPollTypedChar(void)
{
    return 0;
}

void PlatformPushTypedChar(uint8_t c)
{
    (void)c;
}
