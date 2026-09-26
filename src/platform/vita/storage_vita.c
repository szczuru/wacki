/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/storage_vita.c — save storage HAL, PS Vita. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/storage.h"
#include <stdio.h>

static int atomic_replace(const char *from, const char *to)
{
    FILE *src = fopen(from, "rb");
    FILE *dst;
    char buf[4096];
    size_t n;
    int ok = 1;
    if (!src) return -1;
    dst = fopen(to, "wb");
    if (!dst) { fclose(src); return -1; }
    while ((n = fread(buf, 1, sizeof buf, src)) > 0)
        if (fwrite(buf, 1, n, dst) != n) { ok = 0; break; }
    fclose(src);
    fflush(dst);
    fclose(dst);
    if (!ok) return -1;
    remove(from);
    return 0;
}

int plat_save_read(void *buf, int size)
{
    FILE *fp = fopen(WACKI_SAVE_FILE, "rb");
    size_t n;
    if (!fp) return 0;
    n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    return (int)n;
}

int plat_save_write(const void *buf, int size)
{
    const char *tmp = WACKI_SAVE_FILE ".tmp";
    FILE *fp = fopen(tmp, "wb");
    size_t written;
    if (!fp) return 0;
    written = fwrite(buf, 1, (size_t)size, fp);
    if (written != (size_t)size) {
        fclose(fp); remove(tmp);
        LOG_INFO("save", "short write (%lu/%d)", (unsigned long)written, size);
        return 0;
    }
    fflush(fp);
    fclose(fp);
    if (atomic_replace(tmp, WACKI_SAVE_FILE) != 0) {
        LOG_INFO("save", "replace failed");
        return 0;
    }
    return 1;
}
