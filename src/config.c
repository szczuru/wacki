/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/config.c — wacki.cfg: persisted player display/input preferences.
 *
 * A tiny key=value file in the working directory (next to Wacki.sav)
 * remembering the choices the player made so the engine doesn't re-ask /
 * reset every launch. Deliberately NOT stored in Wacki.sav — that file's
 * WackiSettings struct is byte-compatible with the original 1998 game and
 * we don't want to break that. wacki.cfg is purely a port-side convenience.
 *
 * Keys:
 *   fullscreen=0|1   — start full-screen
 *   scale=N          — window zoom factor when not full-screen (1..8)
 *   aspect_mode=stretch|4:3
 *                    — "stretch" (default) fills the output edge to edge;
 *                      "4:3" forces a true 4:3 logical canvas so SDL
 *                      letterboxes (black bars) instead of distorting the
 *                      image on a non-4:3 panel. Matters mainly on
 *                      fixed-panel handhelds whose screen isn't 4:3 — the
 *                      Nintendo Switch's 16:9 panel being the motivating
 *                      case; harmless (no visible difference) on a display
 *                      that's already ~4:3. See src/platform/sdl/video_sdl.c.
 *   touch_mode=absolute|relative|off
 *                    — touchscreen → cursor mapping on targets with a touch
 *                      panel. "absolute" (default) taps to point + click
 *                      (SDL's built-in synthesis); "relative" treats the
 *                      whole panel as a laptop-style touchpad (drag to
 *                      move, never clicks); "off" ignores touch entirely.
 *                      No-op in effect on a target with no touch panel. See
 *                      src/platform/sdl/platform_sdl.c.
 *
 * Precedence is handled by call order in main.c: ConfigLoad runs FIRST
 * (baseline from saved prefs), then the CLI parser and env overrides layer
 * on top, so an explicit --scale / --fullscreen / WACKI_* always wins over
 * the stored preference.
 *
 * g_config_first_run is set when no wacki.cfg exists — desktop's
 * PlatformInit uses it to decide whether to show the one-time display-mode
 * picker. Every target also gets wacki.cfg WRITTEN immediately on first run
 * (with whatever compiled-in defaults are live at that point in startup),
 * not just on the desktop's picker path — a handheld target (WACKI_HANDHELD,
 * including the Nintendo Switch) skips that dialog entirely, so without an
 * explicit save here wacki.cfg would never be created until the player
 * happened to press a runtime toggle, meaning the file's documented keys
 * wouldn't exist yet for someone wanting to hand-edit it. */

#include "wacki.h"
#include "wacki/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WACKI_CFG_PATH      "wacki.cfg"
#define CFG_SCALE_MAX       8
#define CFG_STRBUF_SZ       16

extern int  g_scale_factor;                    /* main.c */
extern int  g_fullscreen;                       /* main.c */
extern char g_aspect_mode[CFG_STRBUF_SZ];       /* src/platform/sdl/video_sdl.c */
extern char g_touch_mode[CFG_STRBUF_SZ];        /* src/platform/sdl/platform_sdl.c */

int g_config_first_run = 0;

/* Forward declaration — ConfigSave is defined further down this file but
 * ConfigLoad (just below) needs to call it on first-run to materialize
 * wacki.cfg with the compiled-in defaults. */
void ConfigSave(void);

/* Accept a few spellings so a hand-edited wacki.cfg ("4:3" / "4x3" / "43")
 * all land on the same internal value. Anything unrecognised leaves
 * g_aspect_mode at its current (compiled-in "stretch") value rather than
 * writing garbage. */
static void normalize_aspect_mode(const char *raw)
{
    if (!raw) return;
    if (strncmp(raw, "4:3", 3) == 0 || strncmp(raw, "4x3", 3) == 0 ||
        strncmp(raw, "43",  2) == 0) {
        strncpy(g_aspect_mode, "4:3", CFG_STRBUF_SZ - 1);
    } else if (strncmp(raw, "stretch", 7) == 0) {
        strncpy(g_aspect_mode, "stretch", CFG_STRBUF_SZ - 1);
    }
    g_aspect_mode[CFG_STRBUF_SZ - 1] = '\0';
}

/* Same idea for touch_mode: accept "absolute"/"relative"/"off", anything
 * else leaves the compiled-in default ("absolute") untouched. */
static void normalize_touch_mode(const char *raw)
{
    if (!raw) return;
    if (strncmp(raw, "absolute", 8) == 0) {
        strncpy(g_touch_mode, "absolute", CFG_STRBUF_SZ - 1);
    } else if (strncmp(raw, "relative", 8) == 0) {
        strncpy(g_touch_mode, "relative", CFG_STRBUF_SZ - 1);
    } else if (strncmp(raw, "off", 3) == 0) {
        strncpy(g_touch_mode, "off", CFG_STRBUF_SZ - 1);
    }
    g_touch_mode[CFG_STRBUF_SZ - 1] = '\0';
}

void ConfigLoad(void)
{
    FILE *fp = fopen(WACKI_CFG_PATH, "r");
    if (!fp) {
        /* No config yet → first launch. Materialize wacki.cfg right away
         * with the compiled-in defaults (see file header comment for why
         * this can't wait for the desktop's first-run picker, which
         * handheld targets never show). */
        g_config_first_run = 1;
        ConfigSave();
        return;
    }
    char line[128];
    while (fgets(line, sizeof line, fp)) {
        int  v;
        char sv[CFG_STRBUF_SZ];
        if (sscanf(line, "fullscreen=%d", &v) == 1) {
            g_fullscreen = v ? 1 : 0;
        } else if (sscanf(line, "scale=%d", &v) == 1) {
            if (v >= 1 && v <= CFG_SCALE_MAX) g_scale_factor = v;
        } else if (sscanf(line, "aspect_mode=%15s", sv) == 1) {
            normalize_aspect_mode(sv);
        } else if (sscanf(line, "touch_mode=%15s", sv) == 1) {
            normalize_touch_mode(sv);
        }
    }
    fclose(fp);
    LOG_INFO("config", "loaded wacki.cfg: fullscreen=%d scale=%d aspect_mode=%s touch_mode=%s",
             g_fullscreen, g_scale_factor, g_aspect_mode, g_touch_mode);
}

void ConfigSave(void)
{
    FILE *fp = fopen(WACKI_CFG_PATH, "w");
    if (!fp) {
        LOG_INFO("config", "cannot write %s (display prefs not saved)",
                 WACKI_CFG_PATH);
        return;
    }
    fprintf(fp, "# Wacki display/input preferences (port-side, not the save file).\n");
    fprintf(fp, "# Skasuj ten plik, by ponownie wybrac tryb przy starcie.\n");
    fprintf(fp, "fullscreen=%d\n", g_fullscreen ? 1 : 0);
    fprintf(fp, "scale=%d\n", g_scale_factor > 0 ? g_scale_factor : 1);
    fprintf(fp, "# aspect_mode: stretch (wypelnia caly ekran) lub 4:3\n");
    fprintf(fp, "# (czarne pasy, oryginalne proporcje).\n");
    fprintf(fp, "aspect_mode=%s\n", g_aspect_mode[0] ? g_aspect_mode : "stretch");
    fprintf(fp, "# touch_mode (tylko ekrany dotykowe):\n");
    fprintf(fp, "#   absolute - dotyk ustawia kursor w tym miejscu i klika\n");
    fprintf(fp, "#   relative - caly ekran dziala jak touchpad (bez klikania)\n");
    fprintf(fp, "#   off      - ignoruj dotyk\n");
    fprintf(fp, "touch_mode=%s\n", g_touch_mode[0] ? g_touch_mode : "absolute");
    fclose(fp);
    LOG_INFO("config", "saved wacki.cfg: fullscreen=%d scale=%d aspect_mode=%s touch_mode=%s",
             g_fullscreen, g_scale_factor ? g_scale_factor : 1, g_aspect_mode, g_touch_mode);
}
