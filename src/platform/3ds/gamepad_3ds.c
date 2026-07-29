/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — 3DS button + touch + circle-pad input.
 *
 * BUTTON MAPPING — NO relabeling needed here, unlike the Switch port:
 *
 * The Switch port (src/platform/switch/gamepad_switch.c) swaps SDL_A/
 * SDL_B because SDL_GameController names buttons by POSITION in the
 * Xbox diamond (SDL_A=south, SDL_B=east), while Nintendo's physical
 * silkscreen labels are the opposite (south=B, east=A) — so on Switch,
 * "physical A" is read via the SDL_B enum value.
 *
 * ctrulib's HID API works completely differently: KEY_A/KEY_B/KEY_X/
 * KEY_Y (3ds/services/hid.h) are defined directly against the
 * PHYSICAL button labels — there is no position-based abstraction
 * layer to compensate for. KEY_A *is* the physical A button. Blindly
 * porting the Switch port's "swap A/B, swap X/Y" compensation here
 * (an earlier version of this file did exactly that) double-flips
 * the mapping: pressing physical A fired a right-click and physical B
 * fired a left-click — the exact "A i B są zamienione" bug reported
 * after testing. Fixed by mapping each ctrulib KEY_* directly to its
 * own physical button, with no swap:
 *
 *   Physical A → left click
 *   Physical B → right click
 *   Physical X → cycle bottom-screen zoom level (1x/2x/4x)
 *   Physical Y → toggle top-screen aspect ratio (stretch <-> 4:3)
 *   START      → pause menu
 *   SELECT     → toggle shoulder hand-mode (left/right) — swaps what
 *                 L/ZL vs. R/ZR do (see SHOULDER HAND MODES below)
 *
 * SHOULDER HAND MODES (SELECT toggles):
 *   left  (default): L/ZL = left/right click,  R/ZR = quicksave/quickload
 *   right           : L/ZL = quicksave/quickload,  R/ZR = left/right click
 *
 * CURSOR: the circle pad + D-pad move the shared virtual cursor
 * (g_mouse_x/g_mouse_y — the SAME globals every other platform's
 * gamepad/mouse code writes; there is no separate 3DS-only cursor
 * variable). Touching the bottom screen jumps the cursor straight to
 * the touched point and fires a left click, mirroring "tap where you
 * want to click" on the zoomed view video_3ds_gl.c draws there.
 *
 * The bottom screen shows a `zoom`-times magnified CROP around the
 * cursor, not a flat-scaled whole image — so the touch point must be
 * mapped through platform_video_touch_to_game(), which inverts the
 * exact region video_3ds_gl.c's blit_bottom_screen_zoom() drew that
 * frame (see its own comment for why a locally-recomputed clamp here
 * would drift out of sync with what's actually on screen).
 *
 * TOUCH-HOLD FREEZE (fixes reported "cursor jumps/drifts fast" bug):
 * the crop's center follows g_mouse_x/y every frame it's redrawn. If
 * the crop were allowed to recenter WHILE a touch is held, each frame
 * would feed back into the next: touching a fixed point tx away from
 * the panel's center shifts the cursor by a constant amount every
 * single frame (k = (tx - panel_center) / zoom), i.e. a runaway
 * constant-velocity drift for as long as the finger stays down off-
 * center — exactly the "przeskakuje strasznie szybko" symptom. Fixed
 * by telling video_3ds_gl.c to freeze the crop (platform_video_
 * set_touch_active) for the whole duration a touch is held, only
 * letting it recenter again after release — see that function's own
 * comment in video_3ds_gl.c for the frozen-crop bookkeeping. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"
#include "gamepad_3ds.h"
#include <3ds.h>

#define ANALOG_DEADZONE   20
#define ANALOG_MAX_PX      9

/* ---- shoulder hand-mode (SELECT toggles) --------------------------- */

typedef enum { HAND_MODE_LEFT = 0, HAND_MODE_RIGHT = 1 } HandMode;
static HandMode s_hand_mode = HAND_MODE_LEFT;

/* ---- touch → cursor mapping ----------------------------------------- */

static int s_touch_was_down = 0;

/* video_3ds_gl.c: cycles the bottom-screen zoom level (X button). */
extern void platform_video_cycle_zoom(void);
/* video_3ds_gl.c: toggles top-screen aspect ratio stretch<->4:3 (Y button). */
extern void platform_video_toggle_aspect_mode(void);
/* video_3ds_gl.c: inverts the exact zoom-crop region last drawn to the
 * bottom screen — see this file's touch-handling comment above and
 * video_3ds_gl.c's own comment on indexed_to_rgba_zoom for why the
 * mapping can't be recomputed independently here. */
extern void platform_video_touch_to_game(int tx, int ty, int *gx, int *gy);
/* video_3ds_gl.c: freezes/unfreezes the zoom-crop recenter — see this
 * file's top comment ("TOUCH-HOLD FREEZE") for why this is needed. */
extern void platform_video_set_touch_active(int active);

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

    /* Physical A = left click. No swap — see this file's top comment
     * for why the Switch port's A/B compensation doesn't apply here. */
    if (down & KEY_A) g_lmb_clicked = 1;
    /* Physical B = right click. */
    if (down & KEY_B) g_rmb_clicked = 1;
    /* Physical X = cycle bottom-screen zoom. */
    if (down & KEY_X) platform_video_cycle_zoom();

    if (down & KEY_START) g_pause_menu_request = 1;
    /* Physical Y = toggle top-screen aspect ratio (stretch <-> 4:3). */
    if (down & KEY_Y) platform_video_toggle_aspect_mode();
    /* SELECT toggles the shoulder hand-mode — swaps which pair
     * (L/ZL vs. R/ZR) does mouse clicks vs. quicksave/load.
     * Y previously also triggered this; freed up for aspect toggle. */
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
        /* Freeze the zoom crop's recenter BEFORE reading/mapping this
         * touch — platform_video_touch_to_game (called right below)
         * must invert through the region video_3ds_gl.c is about to
         * treat as frozen for this frame, not one it might still
         * recenter afterward. */
        platform_video_set_touch_active(1);

        touchPosition touch;
        hidTouchRead(&touch);
        int gx, gy;
        platform_video_touch_to_game(touch.px, touch.py, &gx, &gy);
        g_mouse_x = (int16_t)gx;
        g_mouse_y = (int16_t)gy;
        if (!s_touch_was_down) {
            g_lmb_clicked  = 1;
            s_touch_was_down = 1;
        }
    } else {
        if (s_touch_was_down) {
            /* Just released: unfreeze so the crop resumes following
             * the cursor (now driven by D-pad/circle-pad again) from
             * next frame on. */
            platform_video_set_touch_active(0);
        }
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
