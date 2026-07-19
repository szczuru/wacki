/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/system_3ds.c — 3DS system lifecycle hooks.
 *
 * Handles 3DS-specific initialization and shutdown:
 * - Enable New 3DS clock speed boost (804MHz instead of 268MHz)
 * - No platform-specific volume control (hardware buttons)
 * - No platform-specific key handling beyond gamepad
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"

#include <3ds.h>

void plat_system_early_init(void)
{
    /* Enable New 3DS/2DS XL CPU speed boost
     * This enables the extra L2 cache and higher clock speeds:
     * - Old 3DS: 268MHz (unchanged)
     * - New 3DS: 804MHz (3x faster!)
     * 
     * This is safe to call on Old 3DS - it's a no-op there.
     * Wacki should run on both, but New 3DS will be much smoother. */
    osSetSpeedupEnable(true);
    
    LOG_INFO("3ds-system", "Early init complete (speedup enabled for New 3DS)");
}

void plat_system_late_init(void)
{
    /* Nothing needed - SDL2 initialization handles everything else */
}

void plat_system_shutdown_hook(void)
{
    /* Nothing needed - SDL2 and gfxExit handle cleanup */
}

void plat_restore_system_volume(void)
{
    /* 3DS volume is controlled by hardware buttons (VOL +/-)
     * We don't need to set it programmatically */
}

void plat_handle_platform_key(int sym)
{
    /* No platform-specific keys beyond what gamepad_3ds.c handles
     * All 3DS buttons are processed in platform_pad_read_motion */
    (void)sym;
}
