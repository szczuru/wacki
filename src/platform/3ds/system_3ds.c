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
#include "audio_3ds.h"
#include <3ds.h>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>

/* Called before any other init - setup environment */
void plat_system_early_init(void)
{
    /* Initialize 3DS services. gfxInitDefault() is called here (not by
     * picaGL) because we need hid/fs/romfs up before video_3ds_gl.c's
     * pglInit() runs — picaGL itself only owns the GPU/citro3d side.
     * NOTE: no consoleInit() — the bottom screen is picaGL's zoom
     * viewport (video_3ds_gl.c), a text console there would fight it
     * for the framebuffer. */
    gfxInitDefault();
    gfxSet3D(false); /* Disable stereoscopic 3D — irrelevant for a 2D game,
                       * and halves the GPU work pglSwapBuffers does. */

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

/* Clean shutdown */
void plat_system_exit(int rc)
{
    (void)rc;

    /* Shutdown ndsp only if it was ever brought up (audio_3ds_ndsp.c
     * sets g_ndsp_ready lazily on first plat_audio_open / _begin call —
     * a --headless run, or one that crashes before touching audio,
     * never calls ndspInit and must not call ndspExit either). */
    if (g_ndsp_ready) ndspExit();

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

void plat_restore_system_volume(void) {}
