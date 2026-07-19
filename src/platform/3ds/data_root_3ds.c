/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/data_root_3ds.c — Data folder discovery for 3DS.
 */

#include "wacki.h"
#include "wacki/platform/storage.h"
#include "wacki/log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Try SD card paths for WACKI.EXE and game data */
static const char *s_search_paths[] = {
    "sdmc:/3ds/wacki/data",
    "sdmc:/3ds/wacki",
    "sdmc:/wacki/data",
    "sdmc:/wacki",
    "romfs:/data",
    "romfs:/",
    NULL
};

const char *plat_find_data_root(void)
{
    struct stat st;
    
    for (int i = 0; s_search_paths[i]; i++) {
        char test_path[256];
        snprintf(test_path, sizeof(test_path), "%s/WACKI.EXE", s_search_paths[i]);
        
        if (stat(test_path, &st) == 0) {
            LOG_INFO("data", "Found data root: %s", s_search_paths[i]);
            return s_search_paths[i];
        }
    }
    
    LOG_WARN("data", "No data root found, using default: sdmc:/3ds/wacki/data");
    return "sdmc:/3ds/wacki/data";
}
