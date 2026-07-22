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
    } else {
        /* Resync from g_mouse_x/y every frame BEFORE applying D-pad/
         * circle-pad motion. Needed because platform_pad_handle_buttons()
         * (called earlier this same PlatformPumpEvents tick — see below)
         * writes a touch tap's position straight into g_mouse_x/y, but
         * this function's OWN idea of the cursor position (s_vcur_x/y)
         * previously only synced from g_mouse_x/y once, at startup. Without
         * this resync, a touch tap would move g_mouse_x/y for exactly one
         * frame, and the very next D-pad/circle-pad nudge would silently
         * snap the cursor back to wherever s_vcur_x/y was BEFORE the touch
         * (stale) — the touch tap and analog cursor fought over ownership
         * of the same globals. In frames with no touch this is a no-op:
         * g_mouse_x/y is always exactly what the PREVIOUS call to this
         * function wrote from s_vcur_x/y. */
        s_vcur_x = g_mouse_x;
        s_vcur_y = g_mouse_y;
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

/* ---- Inline text-entry (save-slot rename) -----------------------------
 *
 * src/menu/slot_picker.c's SAVE-menu inline editor is written against
 * an SDL-shaped contract: PlatformSetTextInput(1) opens text entry,
 * then PlatformPollTypedChar() is polled every frame (0x08=Backspace,
 * 0x0D=Enter, else a printable ASCII byte) until it returns 0, and
 * PlatformSetTextInput(0) closes it. On desktop that maps directly to
 * SDL_StartTextInput()/SDL_TEXTINPUT events arriving incrementally
 * across many frames.
 *
 * The 3DS has no incremental per-keystroke text events — its software
 * keyboard (swkbdInputText, 3ds/applets/swkbd.h) is a MODAL applet:
 * calling it blocks this thread, takes over both screens, and only
 * returns once the user has finished typing (confirmed or cancelled).
 * There's no way to "poll" it one keystroke at a time.
 *
 * To satisfy slot_picker.c's polling contract without changing that
 * shared file, PlatformSetTextInput(1) below runs the ENTIRE modal
 * keyboard synchronously right there, then pushes the whole resulting
 * string into the same typed-char ring buffer other platforms fill
 * one keystroke at a time — followed by a synthetic Enter (0x0D) so
 * slot_picker.c's process_typed_chars() commits immediately once it
 * drains the queue, exactly as if the user had typed the name and
 * pressed Enter on a real keyboard. PlatformSetTextInput(0) (called
 * right after commit) is a no-op since the applet already closed.
 *
 * This does mean the game's own frame loop is paused for the duration
 * of typing (matches how every other modal system dialog on this
 * console behaves — there is no non-modal alternative), but the
 * moment the user confirms, the typed name is already fully queued. */
#define TYPED_QUEUE_SZ  40
#define ASCII_BACKSPACE 0x08
#define ASCII_ENTER     0x0D
#define SWKBD_BUF_SZ    32   /* matches EDIT_NAME_MAX_CHARS_3DS + margin */
/* Mirrors src/menu/slot_picker.c's own (private, non-exported)
 * EDIT_NAME_MAX_CHARS — kept as a separate local constant rather than
 * an #include of that file's internals, same "duplicate the constant
 * at the boundary" approach the rest of this port uses for HAL
 * contracts it doesn't own. */
#define EDIT_NAME_MAX_CHARS_3DS 20

static uint8_t s_typed_q[TYPED_QUEUE_SZ];
static int     s_typed_head = 0, s_typed_tail = 0;

static void typed_queue_push(uint8_t c)
{
    int next = (s_typed_head + 1) % TYPED_QUEUE_SZ;
    if (next == s_typed_tail) return; /* full — drop, matches sdl_internal.h's ring */
    s_typed_q[s_typed_head] = c;
    s_typed_head = next;
}

void PlatformPushTypedChar(uint8_t c)
{
    typed_queue_push(c);
}

uint8_t PlatformPollTypedChar(void)
{
    if (s_typed_head == s_typed_tail) return 0;
    uint8_t c = s_typed_q[s_typed_tail];
    s_typed_tail = (s_typed_tail + 1) % TYPED_QUEUE_SZ;
    return c;
}

void PlatformSetTextInput(int on)
{
    if (!on) return; /* closing: nothing to do, applet already returned below */

    s_typed_head = s_typed_tail = 0;

    static SwkbdState swkbd;
    char buf[SWKBD_BUF_SZ];
    buf[0] = '\0';

    /* SWKBD_TYPE_WESTERN: plain Latin keyboard (no Japanese kana/
     * kanji pages) — matches the Latin-only save-name field
     * (EDIT_NAME_MAX_CHARS=20 in slot_picker.c). 2 buttons = Cancel +
     * OK; SWKBD_ANYTHING accepts an empty name (slot_picker.c already
     * falls back to an auto-generated "etap N kM" name when nothing
     * was typed). */
    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, EDIT_NAME_MAX_CHARS_3DS);
    swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
    swkbdSetHintText(&swkbd, "Nazwa zapisu");

    SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof buf);

    if (button != SWKBD_BUTTON_NONE && button != SWKBD_BUTTON_LEFT) {
        /* Right/confirm button: queue the typed string one byte at a
         * time (mirrors handle_textinput() in platform_sdl.c pushing
         * each SDL_TEXTINPUT byte individually), then a synthetic
         * Enter so slot_picker.c's process_typed_chars() commits as
         * soon as it drains these bytes. */
        for (const char *p = buf; *p; ++p) {
            uint8_t c = (uint8_t)*p;
            if (c < 0x80) typed_queue_push(c); /* ASCII only — matches
                                                 * platform_sdl.c's own
                                                 * UTF8_MULTIBYTE_MARK
                                                 * filter */
        }
        typed_queue_push(ASCII_ENTER);
    }
    /* Cancel (SWKBD_BUTTON_LEFT) or any other outcome: leave the queue
     * empty — slot_picker.c's edit buffer is untouched, matching
     * "player backed out without typing anything" on desktop. */
}
