/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/storage_3ds.c — save-image storage HAL, Nintendo 3DS.
 *
 * 3DS requires explicit sync() to commit writes to SD card.
 * Without it, saves work in-session but vanish after reboot. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/storage.h"

#include <3ds.h>
#include <stdio.h>
#include <unistd.h>

extern char g_data_root[260];

int plat_storage_save(const void *buf, size_t len)
{
    char path[300];
    snprintf(path, sizeof path, "%s/wacki.sav", g_data_root);
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_INFO("save", "Failed to open %s for writing", path);
        return -1;
    }
    
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    
    if (written != len) {
        LOG_INFO("save", "Short write: %zu/%zu bytes", written, len);
        return -1;
    }
    
    /* Commit to SD card - critical for 3DS
     * Modern libctru uses fsync() for this */
    sync();
    return 0;
}

int plat_storage_load(void *buf, size_t len)
{
    char path[300];
    snprintf(path, sizeof path, "%s/wacki.sav", g_data_root);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_INFO("save", "No save file at %s", path);
        return -1;
    }
    
    size_t r = fread(buf, 1, len, f);
    fclose(f);
    
    if (r != len) {
        LOG_INFO("save", "Short read: %zu/%zu bytes", r, len);
        return -1;
    }
    
    return 0;
}
