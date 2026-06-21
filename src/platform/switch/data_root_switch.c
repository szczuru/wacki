/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/switch/data_root_switch.c — Switch SD-card data-root probe.
 *
 * Mirrors src/platform/sdl/data_root_handheld.c's pattern (a fixed list of
 * SD-card candidate paths, each handed to the core-supplied probe callback)
 * but with Switch's own homebrew folder convention (sdmc:/switch/<app>/)
 * instead of Miyoo/PortMaster's ROMs layout — kept as its own small file
 * rather than folding a third, unrelated convention into that shared file.
 *
 * No native folder picker exists on Switch — plat_prompt_data_folder is a
 * no-op returning 0, same as every other handheld target. */

#include "wacki/platform/storage.h"
#include <stddef.h>

int plat_data_roots(int (*probe)(const char *root))
{
    static const char *const candidates[] = {
        "sdmc:/switch/wacki/data",
        "sdmc:/switch/wacki",
        "sdmc:/wacki/data",
        "sdmc:/wacki",
    };
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
        int r = probe(candidates[i]);
        if (r) return r;
    }
    return 0;
}

int plat_prompt_data_folder(int (*probe)(const char *root))
{
    (void)probe;
    return 0; /* no native folder picker on Switch */
}
