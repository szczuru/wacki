/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/switch/switch.c — Nintendo Switch homebrew platform hooks.
 *
 * Switch reuses the shared SDL family unchanged for file I/O, audio, FLIC
 * streaming, video presentation, and gamepad input (src/platform/sdl/) —
 * including gamepad_sdl.c's Nintendo-button-layout auto-detection, which
 * also benefits any other SDL target a Joy-Con/Pro Controller gets plugged
 * into. The save image goes through this target's own storage_switch.c
 * instead of sdl/save_host.c (needs an fsdevCommitDevice() call save_host.c
 * doesn't make — see that file's header comment). The one shared-file
 * deviation is a single #ifdef __SWITCH__ branch added to
 * src/platform/sdl/system_sdl.c (cwd pin), mirroring its existing
 * __ANDROID__ branch — see that file's comment for why.
 *
 * This file is Switch's one hooks provider (see docs/platform-hal.md):
 *   - plat_apply_video_prefs(): always-fullscreen (no window manager on
 *     Switch homebrew), PLUS the dynamic WACKI.EXE load below. This hook
 *     runs after FindDataRoot() (so g_data_root is set) and before the SDL
 *     window is created — exactly the right window for both jobs.
 *   - everything else is a no-op, matching portmaster.c's pattern (no
 *     firmware-volume API to restore, no extra HID device to poll, no
 *     hardware-button-as-keysym mapping beyond what gamepad_sdl.c already
 *     covers via real SDL_CONTROLLER* events).
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"

#include <SDL.h>
#include <stdio.h>
#include <stddef.h>

extern int  g_fullscreen;
extern char g_data_root[260];

/* ---- dynamic WACKI.EXE loading ------------------------------------ *
 *
 * Tried in the same spirit as data_root_handheld.c's candidate-path list:
 * first beside the located data root (the common case — the player drops
 * WACKI.EXE in the same folder as Dane_*.dta), then the canonical
 * sdmc:/switch/wacki/ locations as a fallback, in case WACKI.EXE lives one
 * level up/down from wherever the data root resolved to. */
static int load_wacki_exe_dynamic(void)
{
    extern int PeLoaderInit(const char *exe_path);
    char path[300];

    if (g_data_root[0]) {
        snprintf(path, sizeof path, "%s/WACKI.EXE", g_data_root);
        if (PeLoaderInit(path)) return 1;
        snprintf(path, sizeof path, "%s/wacki.exe", g_data_root);
        if (PeLoaderInit(path)) return 1;
    }

    static const char *const fallbacks[] = {
        "sdmc:/switch/wacki/WACKI.EXE",
        "sdmc:/switch/wacki/wacki.exe",
        "sdmc:/switch/wacki/data/WACKI.EXE",
        "sdmc:/switch/wacki/data/wacki.exe",
    };
    for (size_t i = 0; i < sizeof fallbacks / sizeof fallbacks[0]; ++i)
        if (PeLoaderInit(fallbacks[i])) return 1;

    return 0;
}

/* video.h: "Apply platform display preferences before the window is
 * created." Called once from plat_video_init(), after FindDataRoot()
 * succeeded — see src/main.c's WackiMain → PlatformInit → plat_video_init
 * call chain. */
void plat_apply_video_prefs(void)
{
    g_fullscreen = 1; /* no window manager — always cover the display */

    if (load_wacki_exe_dynamic()) {
        LOG_INFO("wacki", "WACKI.EXE loaded dynamically (PeLoaderInit)");
        return;
    }

    /* Fatal: without WACKI.EXE's data sections the engine has nothing to
     * resolve script/entity tables against and will crash deep inside the
     * VM later — a confusing failure mode for an end user. This hook has
     * no return value to bail PlatformInit cleanly (unlike main.c's own
     * FindDataRoot failure path, which can just `return 1` from WackiMain),
     * so show the same kind of native message box FindDataRoot's failure
     * path uses, then exit directly. No SDL window exists yet at this
     * point — SDL_ShowSimpleMessageBox accepts a NULL window fine. */
    const char *msg =
        "Nie znalazłem pliku WACKI.EXE.\n\n"
        "Skopiuj WACKI.EXE z oryginalnej płyty do tego samego "
        "folderu co pliki Dane_*.dta (np. sdmc:/switch/wacki/data/) "
        "i uruchom grę ponownie.";
    LOG_INFO("wacki", "%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Wacki — brak WACKI.EXE", msg, NULL);
    exit(1);
}

void plat_restore_system_volume(void)
{
    /* No firmware volume API to restore on Switch (unlike Miyoo's MI_AO) —
     * the console's own volume/mute is entirely out of the app's hands. */
}

int plat_handle_platform_key(int sym)
{
    /* No hardware buttons arrive as keysyms here (unlike Miyoo) — every
     * Switch input is a real SDL_CONTROLLER* event, handled generically by
     * gamepad_sdl.c. */
    (void)sym;
    return 0;
}

void plat_pad_read_extra(float *ax, float *ay)
{
    /* No extra HID device to poll (PS2's USB-mouse equivalent) — the
     * Joy-Con/Pro Controller analog stick is already covered by
     * gamepad_sdl.c's standard SDL_CONTROLLER axis reads. */
    (void)ax;
    (void)ay;
}

int plat_input_has_keyboard(void)
{
    /* No physical or on-screen keyboard concept the engine should assume —
     * pad + touch only. (The save-slot rename field falls back to its
     * pad/touch-driven on-screen-keyboard path, same as PortMaster/Miyoo.) */
    return 0;
}
