/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — 3DS button + touch + circle-pad input.
 *
 * BUTTON MAPPING (Nintendo A/B swapped to match the "A = confirm/left-
 * click" convention the engine expects, same rationale as the Switch
 * port — ctrulib's KEY_A/KEY_B follow the physical button silkscreen,
 * which is mirrored vs. an Xbox-layout pad):
 *   Physical A (east)  → left click   (KEY_B in ctrulib)
 *   Physical B (south) → right click  (KEY_A in ctrulib)
 *   Physical X (north) → cycle bottom-screen zoom level (1x/2x/4x)
 *   Physical Y (west)  → (reserved; no-op — no aspect-mode concept on
 *                         a fixed dual-screen console)
 *   START              → pause menu
 *   SELECT             → toggle shoulder hand-mode (left/right)
 *
 * SHOULDER HAND MODES (SELECT toggles):
 *   left  (default): L/ZL = left/right click,  R/ZR = quicksave/quickload
 *   right           : L/ZL = quicksave/quickload,  R/ZR = left/right click
 *
 * CURSOR: the circle pad + D-pad move the shared virtual cursor
 * (g_mouse_x/g_mouse_y — the SAME globals every other platform's
 * gamepad/mouse code writes; there is no separate 3DS-only cursor
 * variable). Touching the bottom screen jumps the cursor straight to
 * the touched point (mapped from the 320x240 touch panel to the
 * 640x480 game surface) and fires a left click, mirroring "tap where
 * you want to click" on the zoomed view video_3ds_gl.c draws there. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"
#include <3ds.h>

#define ANALOG_DEADZONE   20
#define ANALOG_MAX_PX      9

/* ---- shoulder hand-mode (SELECT toggles) --------------------------- */

typedef enum { HAND_MODE_LEFT = 0, HAND_MODE_RIGHT = 1 } HandMode;
static HandMode s_hand_mode = HAND_MODE_LEFT;

/* ---- touch → cursor mapping ----------------------------------------- */

static int s_touch_was_down = 0;

/* video_3ds_gl.c reads this to center the bottom-screen zoom view. */
extern void platform_video_cycle_zoom(void);

static void touch_to_game_coords(int tx, int ty, int *gx, int *gy)
{
    /* Bottom screen panel is 320x240; the game surface is 640x480 —
     * a flat 2x scale in both axes. */
    *gx = tx * 2;
    *gy = ty * 2;
    if (*gx < 0) *gx = 0;
    if (*gy < 0) *gy = 0;
    if (*gx > WACKI_SCREEN_W - 1) *gx = WACKI_SCREEN_W - 1;
    if (*gy > WACKI_SCREEN_H - 1) *gy = WACKI_SCREEN_H - 1;
}

/* ---- HAL entry points ------------------------------------------------ */

void platform_pad_open(void)
{
    LOG_INFO("3ds", "gamepad ready (hand_mode=left)");
}

/* Called once per frame from PlatformPumpEvents (platform_3ds.c). Scans
 * hardware state exactly once (hidScanInput) and latches every edge-
 * triggered action the engine expects to see as one-shot globals. */
void platform_pad_handle_buttons(void)
{
    hidScanInput();
    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();

    /* Physical A (ctrulib KEY_B) = left click. */
    if (down & KEY_B) g_lmb_clicked = 1;
    /* Physical B (ctrulib KEY_A) = right click. */
    if (down & KEY_A) g_rmb_clicked = 1;
    /* Physical X (ctrulib KEY_Y) = cycle bottom-screen zoom. */
    if (down & KEY_Y) platform_video_cycle_zoom();
    /* Physical Y (ctrulib KEY_X): reserved, intentionally no-op. */

    if (down & KEY_START)  g_pause_menu_request = 1;
    if (down & KEY_SELECT) {
        s_hand_mode = (s_hand_mode == HAND_MODE_LEFT) ? HAND_MODE_RIGHT : HAND_MODE_LEFT;
        LOG_INFO("3ds", "hand_mode=%s", s_hand_mode == HAND_MODE_LEFT ? "left" : "right");
    }

    if (s_hand_mode == HAND_MODE_LEFT) {
        if (down & KEY_L)  g_lmb_clicked       = 1;
        if (down & KEY_ZL) g_rmb_clicked       = 1;
        if (down & KEY_R)  g_quicksave_request = 1;
        if (down & KEY_ZR) g_quickload_request = 1;
    } else {
        if (down & KEY_L)  g_quicksave_request = 1;
        if (down & KEY_ZL) g_quickload_request = 1;
        if (down & KEY_R)  g_lmb_clicked       = 1;
        if (down & KEY_ZR) g_rmb_clicked       = 1;
    }

    /* Touch: jump the cursor to the touched point + fire a left click
     * on touch-down (not every held frame — a drag shouldn't spam
     * clicks). */
    if (held & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int gx, gy;
        touch_to_game_coords(touch.px, touch.py, &gx, &gy);
        g_mouse_x = (int16_t)gx;
        g_mouse_y = (int16_t)gy;
        if (!s_touch_was_down) {
            g_lmb_clicked  = 1;
            s_touch_was_down = 1;
        }
    } else {
        s_touch_was_down = 0;
    }
}

/* Folded into the shared virtual-cursor poll the same way gamepad_sdl.c's
 * counterpart is: discrete D-pad dx/dy shares the caller's accel ramp,
 * the circle pad drives proportional ax/ay in px/tick. */
void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    u32 held = hidKeysHeld();
    if (held & KEY_DRIGHT) ++(*dx);
    if (held & KEY_DLEFT)  --(*dx);
    if (held & KEY_DDOWN)  ++(*dy);
    if (held & KEY_DUP)    --(*dy);

    circlePosition pos;
    hidCircleRead(&pos);
    if (pos.dx > ANALOG_DEADZONE || pos.dx < -ANALOG_DEADZONE)
        *ax = (float)pos.dx / 156.0f * ANALOG_MAX_PX;
    if (pos.dy > ANALOG_DEADZONE || pos.dy < -ANALOG_DEADZONE)
        *ay = -(float)pos.dy / 156.0f * ANALOG_MAX_PX;  /* circle pad Y is inverted vs screen Y */

    plat_pad_read_extra(ax, ay);
}

/* Edge-triggered nav for a pre-game modal — not used on 3DS (no boot-time
 * video-mode picker like the PS2's), but the HAL requires it. Returns 0
 * ("no controller") so any caller falls back to its default instead of
 * soft-locking. */
int plat_pad_menu_nav(int *up, int *down, int *confirm)
{
    *up = *down = *confirm = 0;
    return 0;
}

void plat_input_flush(void)
{
    hidScanInput();
    g_lmb_clicked        = 0;
    g_rmb_clicked        = 0;
    g_quicksave_request  = 0;
    g_quickload_request  = 0;
    g_pause_menu_request = 0;
}

int plat_input_has_keyboard(void)
{
    return 0;   /* no reliable keyboard on 3DS (swkbd is a modal applet) */
}

int plat_handle_platform_key(int sym)
{
    (void)sym;
    return 0;
}

void plat_pad_read_extra(float *ax, float *ay)
{
    (void)ax; (void)ay;
}
