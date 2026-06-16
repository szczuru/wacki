/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/config.c — wacki.cfg: persisted player display preferences.
 *
 * A tiny key=value file in the working directory (next to Wacki.sav)
 * remembering the display mode the player chose so the engine doesn't
 * re-ask every launch. Deliberately NOT stored in Wacki.sav — that
 * file's WackiSettings struct is byte-compatible with the original
 * 1998 game and we don't want to break that. wacki.cfg is purely a
 * port-side convenience.
 *
 * Keys:
 *   fullscreen=0|1   — start full-screen
 *   scale=N          — window zoom factor when not full-screen (1..8)
 *   aspect_mode=stretch|4:3
 *                    — "stretch" (default) fills the output edge to
 *                      edge; "4:3" forces a true 4:3 logical canvas so
 *                      SDL letterboxes (black bars) instead of
 *                      distorting the image on a non-4:3 panel.
 *                      Matters mainly on fixed-panel handhelds whose
 *                      screen isn't 4:3 — the Nintendo Switch's 16:9
 *                      panel being the motivating case; ignored in
 *                      effect (but harmless) on displays that are
 *                      already ~4:3.
 *
 * Precedence is handled by call order in main.c: ConfigLoad runs
 * FIRST (baseline from saved prefs), then the CLI parser and env
 * overrides layer on top, so an explicit --scale / --fullscreen /
 * WACKI_* always wins over the stored preference.
 *
 * g_config_first_run is set when no wacki.cfg exists — PlatformInit
 * uses it to decide whether to show the one-time display-mode
 * picker. */

#include "wacki.h"
#include "wacki/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WACKI_CFG_PATH      "wacki.cfg"
#define CFG_SCALE_MAX       8
#define CFG_ASPECT_BUF_SZ   16

extern int  g_scale_factor;        /* main.c */
extern int  g_fullscreen;          /* main.c */
extern char g_aspect_mode[CFG_ASPECT_BUF_SZ]; /* platform_sdl.c */

int g_config_first_run = 0;

/* Accept a few spellings so a hand-edited wacki.cfg or an .ini-style
 * "4:3" / "4x3" / "43" all land on the same internal value. Anything
 * unrecognised falls back to "stretch" (the long-standing default
 * behaviour every target had before this option existed). */
static void normalize_aspect_mode(const char *raw)
{
    if (!raw) return;
    if (strncmp(raw, "4:3", 3) == 0 || strncmp(raw, "4x3", 3) == 0 ||
        strncmp(raw, "43",  2) == 0) {
        strncpy(g_aspect_mode, "4:3", CFG_ASPECT_BUF_SZ - 1);
    } else if (strncmp(raw, "stretch", 7) == 0) {
        strncpy(g_aspect_mode, "stretch", CFG_ASPECT_BUF_SZ - 1);
    }
    /* else: leave g_aspect_mode at its current value (the "stretch"
     * compiled-in default) rather than writing garbage. */
    g_aspect_mode[CFG_ASPECT_BUF_SZ - 1] = '\0';
}

void ConfigLoad(void)
{
    FILE *fp = fopen(WACKI_CFG_PATH, "r");
    if (!fp) {
        /* No config yet → first launch. PlatformInit shows the
         * display-mode picker (unless a CLI/env flag pre-empts it). */
        g_config_first_run = 1;
        return;
    }
    char line[128];
    while (fgets(line, sizeof line, fp)) {
        int  v;
        char sv[CFG_ASPECT_BUF_SZ];
        if (sscanf(line, "fullscreen=%d", &v) == 1) {
            g_fullscreen = v ? 1 : 0;
        } else if (sscanf(line, "scale=%d", &v) == 1) {
            if (v >= 1 && v <= CFG_SCALE_MAX) g_scale_factor = v;
        } else if (sscanf(line, "aspect_mode=%15s", sv) == 1) {
            normalize_aspect_mode(sv);
        }
    }
    fclose(fp);
    LOG_INFO("config", "loaded wacki.cfg: fullscreen=%d scale=%d aspect_mode=%s",
             g_fullscreen, g_scale_factor, g_aspect_mode);
}

void ConfigSave(void)
{
    FILE *fp = fopen(WACKI_CFG_PATH, "w");
    if (!fp) {
        LOG_INFO("config", "cannot write %s (display prefs not saved)",
                 WACKI_CFG_PATH);
        return;
    }
    fprintf(fp, "# Wacki display preferences (port-side, not the save file).\n");
    fprintf(fp, "# Skasuj ten plik, by ponownie wybrać tryb przy starcie.\n");
    fprintf(fp, "fullscreen=%d\n", g_fullscreen ? 1 : 0);
    fprintf(fp, "scale=%d\n", g_scale_factor > 0 ? g_scale_factor : 1);
    fprintf(fp, "# aspect_mode: stretch (wypełnia cały ekran) lub 4:3\n");
    fprintf(fp, "# (czarne pasy, oryginalne proporcje — przydatne np. na\n");
    fprintf(fp, "# Nintendo Switch, którego ekran jest 16:9).\n");
    fprintf(fp, "aspect_mode=%s\n", g_aspect_mode[0] ? g_aspect_mode : "stretch");
    fclose(fp);
    LOG_INFO("config", "saved wacki.cfg: fullscreen=%d scale=%d aspect_mode=%s",
             g_fullscreen, g_scale_factor ? g_scale_factor : 1, g_aspect_mode);
}
