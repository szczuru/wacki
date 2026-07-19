/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/storage_3ds.c — Save/load to SD card for 3DS.
 */

#include "wacki.h"
#include "wacki/platform/storage.h"
#include "wacki/log.h"
#include <stdio.h>
#include <string.h>

#define SAVE_PATH "sdmc:/3ds/wacki/wacki.sav"

int plat_save_read(void *buf, int sz)
{
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) {
        LOG_DEBUG("storage", "No save file at %s", SAVE_PATH);
        return -1;
    }
    
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    
    if (rd != sz) {
        LOG_WARN("storage", "Save read %zu bytes, expected %zu", rd, sz);
        return -1;
    }
    
    LOG_INFO("storage", "Loaded save from %s (%zu bytes)", SAVE_PATH, sz);
    return 0;
}

int plat_save_write(const void *buf, int sz)
{
    FILE *f = fopen(SAVE_PATH, "wb");
    if (!f) {
        LOG_ERROR("storage", "Failed to open %s for writing", SAVE_PATH);
        return -1;
    }
    
    size_t wr = fwrite(buf, 1, sz, f);
    fflush(f);
    fclose(f);
    
    if (wr != sz) {
        LOG_ERROR("storage", "Save write failed: %zu/%zu bytes", wr, sz);
        return -1;
    }
    
    LOG_INFO("storage", "Saved to %s (%zu bytes)", SAVE_PATH, sz);
    return 0;
}
