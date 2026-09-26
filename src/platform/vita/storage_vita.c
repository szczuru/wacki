/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/storage_vita.c — trwały zapis na kartę (ux0). */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/storage.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Trwała lokalizacja na karcie — NIE katalog aplikacji (ux0:/app/...). */
#define VITA_SAVE_DIR  "ux0:/data/wacki"
#define VITA_SAVE_PATH VITA_SAVE_DIR "/Wacki.sav"
#define VITA_SAVE_TMP  VITA_SAVE_DIR "/Wacki.sav.tmp"

static void ensure_save_dir(void)
{
    struct stat st;
    if (stat(VITA_SAVE_DIR, &st) == 0 && S_ISDIR(st.st_mode))
        return;
    if (mkdir("ux0:/data", 0777) != 0 && errno != EEXIST)
        LOG_INFO("save", "mkdir ux0:/data: %s", strerror(errno));
    if (mkdir(VITA_SAVE_DIR, 0777) != 0 && errno != EEXIST)
        LOG_INFO("save", "mkdir %s: %s", VITA_SAVE_DIR, strerror(errno));
}

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
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = 0; break; }
    }
    fclose(src);
    fflush(dst);
    fclose(dst);
    if (!ok) {
        remove(to);
        return -1;
    }
    remove(from);
    return 0;
}

int plat_save_read(void *buf, int size)
{
    FILE *fp;
    size_t n;
    ensure_save_dir();
    fp = fopen(VITA_SAVE_PATH, "rb");
    if (!fp) {
        /* fallback: stary plik obok binarki / CWD */
        fp = fopen(WACKI_SAVE_FILE, "rb");
        if (!fp) return 0;
    }
    n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    return (int)n;
}

int plat_save_write(const void *buf, int size)
{
    FILE *fp;
    size_t written;
    ensure_save_dir();
    fp = fopen(VITA_SAVE_TMP, "wb");
    if (!fp) {
        LOG_INFO("save", "fopen(%s) failed: %s", VITA_SAVE_TMP, strerror(errno));
        return 0;
    }
    written = fwrite(buf, 1, (size_t)size, fp);
    if (written != (size_t)size) {
        fclose(fp);
        remove(VITA_SAVE_TMP);
        LOG_INFO("save", "short write (%lu/%d)", (unsigned long)written, size);
        return 0;
    }
    fflush(fp);
    fclose(fp);
    if (atomic_replace(VITA_SAVE_TMP, VITA_SAVE_PATH) != 0) {
        LOG_INFO("save", "replace failed");
        return 0;
    }
    LOG_INFO("save", "wrote %s (%d bytes)", VITA_SAVE_PATH, size);
    return 1;
}
