/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/switch/switch.c — Nintendo Switch homebrew platform hooks.
 *
 * Switch reuses the shared SDL family unchanged for file I/O, audio, FLIC
 * streaming, video presentation, and gamepad input (src/platform/sdl/).
 * Nintendo button-layout remapping is handled generically by the new
 * plat_pad_is_nintendo_layout() hook — implemented here by delegating to
 * src/platform/nintendo/nintendo_gamepad.c, which is linked for all Nintendo
 * homebrew targets and always returns 1 without a runtime type query.
 *
 * WACKI.EXE — two build paths, both fully supported:
 *
 *   1. Embedded (preferred, identical to PS2/PortMaster): CI restores
 *      WACKI.EXE from a repository secret → tools/embed-pe-data extracts
 *      .rdata/.data → src/embedded_wacki_pe.c is generated and linked.
 *      No runtime file I/O needed; works offline, fastest startup.
 *
 *   2. Dynamic fallback: if the embedded slice table is empty (g_wacki_pe_
 *      slice_count == 0 — happens when no secret is configured, e.g. a fresh
 *      fork or a local build without data/WACKI.EXE), plat_apply_video_prefs
 *      calls PeLoaderInit() to load the player's own WACKI.EXE from the SD
 *      card. PeLoaderRead already checks a dynamically-loaded image BEFORE
 *      the embedded table, so path 1 is unaffected by this code being present.
 *
 * mk/switch.mk decides which path is active:
 *   - data/WACKI.EXE present at build time → normal embed-pe-data flow
 *   - data/WACKI.EXE absent                → EMBEDDED_PE_SRC points at the
 *     empty stub (src/platform/switch/embedded_wacki_pe_stub.c), dynamic
 *     fallback below activates at runtime. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/system.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"

#include <SDL.h>
#include <stddef.h>

extern int  g_fullscreen;
extern char g_data_root[260];

/* Defined in the linked embedded_wacki_pe_stub.c OR the generated
 * src/embedded_wacki_pe.c — used below to decide whether the dynamic
 * fallback is needed. */
extern const int g_wacki_pe_slice_count;

/* ---- dynamic WACKI.EXE loading (fallback when not embedded) ----------- */

static int load_wacki_exe_dynamic(void)
{
    extern int PeLoaderInit(const char *exe_path);
    char path[300];

    if (g_data_root[0]) {
        snprintf(path, sizeof path, "%s/WACKI.EXE", g_data_root);
        if (PeLoaderInit(path)) return 1;
        snprintf(path, sizeof path, "%s/wacki.exe", g_data_root);
        if (PeLoaderInit(path)) return 1;
    }

    static const char *const fallbacks[] = {
        "sdmc:/switch/wacki/WACKI.EXE",
        "sdmc:/switch/wacki/wacki.exe",
        "sdmc:/switch/wacki/data/WACKI.EXE",
        "sdmc:/switch/wacki/data/wacki.exe",
    };
    for (size_t i = 0; i < sizeof fallbacks / sizeof fallbacks[0]; ++i)
        if (PeLoaderInit(fallbacks[i])) return 1;

    return 0;
}

/* ---- hooks ------------------------------------------------------------ */

void plat_apply_video_prefs(void)
{
    g_fullscreen = 1;

    /* Path 1 — embedded PE (CI with secret / local build with data/WACKI.EXE):
     * slice count > 0 means embed-pe-data ran at build time; PeLoaderRead
     * resolves VAs against the embedded table automatically, nothing to do. */
    if (g_wacki_pe_slice_count > 0) {
        LOG_INFO("wacki", "WACKI.EXE data embedded at build time (%d slices)",
                 g_wacki_pe_slice_count);
        return;
    }

    /* Path 2 — dynamic fallback (stub linked, no WACKI.EXE at build time). */
    if (load_wacki_exe_dynamic()) {
        LOG_INFO("wacki", "WACKI.EXE loaded dynamically from SD card");
        return;
    }

    const char *msg =
        "Nie znalazlem pliku WACKI.EXE.\n\n"
        "Skopiuj WACKI.EXE z oryginalnej plyty do tego samego\n"
        "folderu co pliki Dane_*.dta na karcie SD\n"
        "(np. sdmc:/switch/wacki/data/) i uruchom gre ponownie.";
    LOG_INFO("wacki", "%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Wacki \xe2\x80\x94 brak WACKI.EXE", msg, NULL);
    exit(1);
}

void plat_restore_system_volume(void) {}

int plat_handle_platform_key(int sym) { (void)sym; return 0; }

void plat_pad_read_extra(float *ax, float *ay) { (void)ax; (void)ay; }

int plat_input_has_keyboard(void) { return 0; }
