/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/system_3ds.c — Nintendo 3DS system lifecycle hooks. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include <3ds.h>

void plat_system_early_init(void)
{
    /* Initialize 3DS services */
    romfsInit();
    hidInit();
    aptInit();
    
    LOG_INFO("platform", "3DS system services initialized");
}

void plat_system_exit(int rc)
{
    /* Cleanup 3DS services */
    aptExit();
    hidExit();
    romfsExit();
    
    LOG_INFO("platform", "3DS system exit (code=%d)", rc);
    /* On 3DS we can return normally */
}

void plat_dcache_flush(void *p, unsigned int n)
{
    /* 3DS doesn't need explicit cache flushing for normal operations */
    (void)p;
    (void)n;
}

void plat_trace_mark(unsigned int code)
{
    /* No trace facility on 3DS */
    (void)code;
}
