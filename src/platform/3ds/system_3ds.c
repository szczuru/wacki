/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/system_3ds.c — process-lifecycle HAL, Nintendo 3DS.
 *
 * Initializes 3DS services (gfx, hid, romfs) and sets working directory
 * to sdmc:/3ds/wacki/ so save files land in a consistent location. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include <3ds.h>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>

/* Called before any other init - setup environment */
void plat_system_early_init(void)
{
    /* Initialize 3DS services */
    gfxInitDefault();
    gfxSet3D(false); /* Disable 3D for performance */
    
    /* Initialize console for early logging (optional) */
    consoleInit(GFX_BOTTOM, NULL);
    
    /* Create and set working directory */
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/wacki", 0777);
    
    if (chdir("sdmc:/3ds/wacki") == 0) {
        LOG_INFO("platform", "user dir: sdmc:/3ds/wacki");
    } else {
        LOG_INFO("platform", "chdir(sdmc:/3ds/wacki) failed");
    }
    
    /* Initialize RomFS if available (for embedded data) */
    Result rc = romfsInit();
    if (R_SUCCEEDED(rc)) {
        LOG_INFO("platform", "RomFS initialized");
    }
}

/* System initialization (called after video init) */
int plat_system_init(void)
{
    /* Input already initialized via hidScanInput() in gamepad */
    return 1;
}

/* Clean shutdown */
void plat_system_exit(int rc)
{
    (void)rc;
    
    /* Shutdown RomFS */
    romfsExit();
    
    /* Shutdown graphics */
    gfxExit();
}

/* Platform-specific hooks */
void plat_dcache_flush(void *p, unsigned int n)
{
    if (p && n > 0) {
        GSPGPU_FlushDataCache(p, n);
    }
}

void plat_trace_mark(unsigned int code)
{
    (void)code;
}

/* Check if we should quit (HOME button via aptMainLoop) */
int plat_should_quit(void)
{
    return !aptMainLoop();
}
