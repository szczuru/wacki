/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/data_root_vita.c — Vita data-root probe. */

#include "wacki/platform/storage.h"
#include <stddef.h>

int plat_data_roots(int (*probe)(const char *root))
{
    static const char *const candidates[] = {
        "ux0:/data/wacki/data",
        "ux0:/data/wacki",
        "ux0:/app/WACK10001/data",
        "ux0:/app/WACK10001",
    };
    size_t i;
    for (i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
        int r = probe(candidates[i]);
        if (r) return r;
    }
    return 0;
}

int plat_prompt_data_folder(int (*probe)(const char *root))
{
    (void)probe;
    return 0;
}
