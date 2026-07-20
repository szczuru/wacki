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
