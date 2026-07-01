/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/data_root_3ds.c — data-root discovery for 3DS.
 *
 * On 3DS we look in:
 * 1. sdmc:/3ds/wacki/data/
 * 2. sdmc:/3ds/wacki/
 * 3. Current directory (romfs:/)
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/data_root.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char g_data_root[260];

static int dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

void plat_data_root_discover(void)
{
    /* Try SD card locations first */
    const char *candidates[] = {
        "sdmc:/3ds/wacki/data",
        "sdmc:/3ds/wacki",
        "romfs:/",
        ".",
    };
    
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (dir_exists(candidates[i])) {
            strncpy(g_data_root, candidates[i], sizeof(g_data_root) - 1);
            g_data_root[sizeof(g_data_root) - 1] = '\0';
            LOG_INFO("platform", "Data root: %s", g_data_root);
            return;
        }
    }
    
    /* Fallback to sdmc:/3ds/wacki/data even if it doesn't exist yet */
    strncpy(g_data_root, "sdmc:/3ds/wacki/data", sizeof(g_data_root) - 1);
    g_data_root[sizeof(g_data_root) - 1] = '\0';
    LOG_INFO("platform", "Data root: %s (fallback)", g_data_root);
}
