/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/switch/storage_switch.c — save-image storage HAL, Switch.
 *
 * Same tmp+rename-style atomic-write contract as src/platform/sdl/
 * save_host.c, with two Switch-specific differences:
 *
 *   1. fsdevCommitDevice("sdmc") after every write. libnx buffers writes to
 *      the sdmc: device in the kernel FS layer and does NOT flush them to
 *      the physical SD card on fclose() — fsdevCommitDevice is the only
 *      call that does. Without it, a save looks correct within the running
 *      session (a subsequent fread reads it back fine) but is silently
 *      empty/truncated after the next power cycle or game relaunch — this
 *      cost a real player their save during testing before this fix.
 *
 *   2. Manual copy instead of rename() for the tmp → real-name swap.
 *      rename() on FAT/exFAT under libnx has been observed to not reliably
 *      overwrite an existing destination file (sometimes leaving both the
 *      old destination AND the .tmp source intact, depending on firmware
 *      version) — a plain read-then-write copy sidesteps that rename
 *      semantics question entirely. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/storage.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>

static int atomic_replace(const char *from, const char *to)
{
    FILE *src = fopen(from, "rb");
    if (!src) return -1;
    FILE *dst = fopen(to, "wb");
    if (!dst) { fclose(src); return -1; }

    char buf[4096];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = 0; break; }
    }
    fclose(src);
    fflush(dst);
    fclose(dst);
    if (!ok) return -1;
    remove(from); /* clean up the .tmp file */

    Result rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(rc))
        LOG_INFO("save", "fsdevCommitDevice(sdmc) failed (0x%x) — save may not persist after power-off", rc);
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
    const char *tmp_path = WACKI_SAVE_FILE ".tmp";
    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return 0;

    size_t written = fwrite(buf, 1, (size_t)size, fp);
    if (written != (size_t)size) {
        fclose(fp);
        remove(tmp_path);
        LOG_INFO("save", "short write (%lu/%d) — keeping previous save",
                 (unsigned long)written, size);
        return 0;
    }
    fflush(fp);
    fclose(fp);

    if (atomic_replace(tmp_path, WACKI_SAVE_FILE) != 0) {
        LOG_INFO("save", "replace(%s -> %s) failed — keeping tmp", tmp_path, WACKI_SAVE_FILE);
        return 0;
    }
    return 1;
}
