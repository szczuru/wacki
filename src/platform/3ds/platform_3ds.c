/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/platform_3ds.c — 3DS platform init replacement for platform_sdl.c */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"

int plat_should_quit(void)
{
    return !aptMainLoop();
}

int plat_system_init(void)
{
    return 1;
}

void platform_pad_open(void)
{
    LOG_INFO("3ds", "Input initialized");
}

int PlatformShouldQuit(void)
{
    return plat_should_quit();
}

int PlatformInit(int w, int h, const char *title)
{
    plat_system_early_init();
    
    if (!plat_video_init(w, h, title)) {
        LOG_INFO("platform", "video init failed");
        return 0;
    }
    
    if (!plat_system_init()) {
        LOG_INFO("platform", "system init failed");
        return 0;
    }
    
    platform_pad_open();
    
    return 1;
}

void PlatformShutdown(void)
{
    plat_video_shutdown();
    plat_system_exit(0);
}
