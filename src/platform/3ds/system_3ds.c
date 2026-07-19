/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/system_3ds.c — 3DS system hooks.
 *
 * Enables New 3DS CPU speedup (804 MHz) for better performance. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"

#include <3ds.h>

void plat_system_init(void)
{
    /* Enable New 3DS CPU speedup */
    bool is_new3ds = false;
    APT_CheckNew3DS(&is_new3ds);
    
    if (is_new3ds) {
        osSetSpeedupEnable(true);
        LOG_INFO("3ds-system", "New 3DS detected - CPU speedup enabled (804 MHz)");
    } else {
        LOG_INFO("3ds-system", "Old 3DS detected - running at 268 MHz");
    }
}

void plat_system_shutdown(void)
{
    /* Nothing to do */
}

uint32_t plat_system_get_ticks_ms(void)
{
    return (uint32_t)(svcGetSystemTick() / CPU_TICKS_PER_MSEC);
}

void plat_system_delay_ms(uint32_t ms)
{
    svcSleepThread((s64)ms * 1000000LL);
}
