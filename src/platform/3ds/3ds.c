/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/3ds.c — Main platform initialization for Nintendo 3DS.
 */

#include "wacki.h"
#include "wacki/platform.h"
#include <3ds.h>

/* Platform init - called before engine starts */
void PlatformInit(void)
{
    /* 3DS initialization done in system_3ds.c */
}

/* Platform shutdown - called on exit */
void PlatformShutdown(void)
{
    /* Cleanup done in system_3ds.c */
}
