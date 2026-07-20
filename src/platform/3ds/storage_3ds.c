/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/storage_3ds.c — save-image storage HAL, 3DS.
 *
 * 3DS requires FSUSER_ControlArchive with ARCHIVE_ACTION_COMMIT_SAVE_DATA
 * to flush buffered writes to physical SD card. Without it, saves work
 * in-session but vanish on reboot. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/storage.h"
#include <3ds.h>
#include <stdio.h>

/* Commit buffered SDMC writes to the physical card. FS_Archive is a
 * plain `typedef u64` handle (see libctru's fs.h) — NOT a struct — so
 * it must be opened via FSUSER_OpenArchive before any control call,
 * and closed afterwards. */
static int commit_sd_card(void)
{
    FS_Archive sdmc = 0;
    FS_Path    root = fsMakePath(PATH_EMPTY, "");

    Result rc = FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, root);
    if (R_FAILED(rc)) {
        LOG_INFO("save", "FSUSER_OpenArchive(SDMC) failed: 0x%08lX", (unsigned long)rc);
        return -1;
    }

    rc = FSUSER_ControlArchive(sdmc, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(sdmc);
    if (R_FAILED(rc)) {
        LOG_INFO("save", "FSUSER_ControlArchive(COMMIT) failed: 0x%08lX", (unsigned long)rc);
        return -1;
    }
    return 0;
}

static int atomic_replace(const char *from, const char *to)
{
    FILE *src = fopen(from, "rb");
    if (!src) return -1;
    
    FILE *dst = fopen(to, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }

    char buf[4096];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            ok = 0;
            break;
        }
    }
    
    fclose(src);
    fflush(dst);
    fclose(dst);
    
    if (!ok) return -1;
    remove(from);

    /* Commit to SD card */
    if (commit_sd_card() != 0) {
        LOG_INFO("save", "SD commit failed - save may not persist");
    }
    
    return 0;
}

int plat_save_read(void *buf, int size)
{
    FILE *fp = fopen(WACKI_SAVE_FILE, "rb");
    if (!fp) return 0;
    
    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    return (int)n;
}

int plat_save_write(const void *buf, int size)
{
    const char *tmp = WACKI_SAVE_FILE ".tmp";
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return 0;

    size_t written = fwrite(buf, 1, (size_t)size, fp);
    if (written != (size_t)size) {
        fclose(fp);
        remove(tmp);
        LOG_INFO("save", "short write (%lu/%d)", (unsigned long)written, size);
        return 0;
    }
    
    fflush(fp);
    fclose(fp);

    if (atomic_replace(tmp, WACKI_SAVE_FILE) != 0) {
        LOG_INFO("save", "replace(%s->%s) failed", tmp, WACKI_SAVE_FILE);
        return 0;
    }
    
    return 1;
}
