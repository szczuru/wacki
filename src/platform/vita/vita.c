/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/vita.c — PlayStation Vita platform hooks. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"
#include <SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

extern int  g_fullscreen;
extern char g_data_root[260];
extern const int g_wacki_pe_slice_count;

static int load_wacki_exe_dynamic(void)
{
    extern int PeLoaderInit(const char *path);
    char path[300];
    if (g_data_root[0]) {
        snprintf(path, sizeof path, "%s/WACKI.EXE", g_data_root);
        if (PeLoaderInit(path)) return 1;
        snprintf(path, sizeof path, "%s/wacki.exe", g_data_root);
        if (PeLoaderInit(path)) return 1;
    }
    {
        static const char *const fb[] = {
            "ux0:/data/wacki/WACKI.EXE",
            "ux0:/data/wacki/wacki.exe",
            "ux0:/data/wacki/data/WACKI.EXE",
            "ux0:/data/wacki/data/wacki.exe",
            "ux0:/app/WACKI00001/WACKI.EXE",
        };
        size_t i;
        for (i = 0; i < sizeof fb / sizeof fb[0]; ++i)
            if (PeLoaderInit(fb[i])) return 1;
    }
    return 0;
}

void plat_apply_video_prefs(void)
{
    g_fullscreen = 1;

    /* Szybkość: nie czekaj na VSync; wymuś prostszy present */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest, taniej */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    /* reszta jak było: PE load… */
    if (g_wacki_pe_slice_count > 0) {
        LOG_INFO("wacki", "WACKI.EXE embedded (%d slices)", g_wacki_pe_slice_count);
        return;
    }
    if (load_wacki_exe_dynamic()) {
        LOG_INFO("wacki", "WACKI.EXE loaded from memory card");
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wacki",
        "Nie znalazlem WACKI.EXE.\n"
        "Skopiuj go do ux0:/data/wacki/data/", NULL);
    exit(1);
}

void plat_restore_system_volume(void) {}
int  plat_handle_platform_key(int sym) { (void)sym; return 0; }
void plat_pad_read_extra(float *ax, float *ay) { (void)ax; (void)ay; }
int  plat_input_has_keyboard(void) { return 0; }
