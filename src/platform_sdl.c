/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform_sdl.c — portable platform layer (SDL2).
 *
 * Replaces the original DirectDraw / WndProc / WaitMessage stack. The
 * engine renders into a flat 8-bpp uint8_t shadow buffer; this layer
 * uploads that buffer to a streaming SDL_Texture via a palette LUT
 * and pumps SDL events into the engine's input + key-latch globals.
 *
 * Headless mode (--headless) keeps SDL_Init alive (dummy video +
 * audio drivers) so the event queue + mixer callback still run, but
 * skips window/renderer/texture creation so CI smoke tests work
 * without a display.
 *
 * Public API (declared in wacki.h):
 *   PlatformInit / PlatformShutdown
 *   PlatformPresent  — upload + present one frame
 *   PlatformPumpEvents — drain SDL events, update input globals
 *   PlatformShouldQuit — set by SDL_QUIT / WINDOWCLOSE / ESC
 *   PlatformSetTextInput / PlatformPollTypedChar / PushTypedChar
 *   PlatformShowMessageBox
 *
 * WACKI_SWITCH note: the Nintendo Switch port (src/platform_switch.c)
 * exposes the exact same platform_pad_open / platform_pad_handle_event
 * / platform_pad_read_motion API as the PortMaster (Anbernic) glue, so
 * every call site that previously gated on WACKI_PORTMASTER now also
 * checks WACKI_SWITCH. Also adds g_aspect_mode (4:3 letterbox vs
 * full-stretch) and g_touch_mode (absolute / relative / off touchscreen
 * → cursor mapping) for the Switch's panel — see PlatformInit /
 * handle_touch below. */

#include "wacki.h"
#include "wacki/log.h"

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- constants ---------------------------------------------------- */

/* Typed-char ring buffer for inline-edit (save-slot rename). Populated
 * by SDL_TEXTINPUT (printable chars) + SDL_KEYDOWN (Backspace / Enter).
 * Drained by PlatformPollTypedChar — returns 0 when empty. */
#define TYPED_QUEUE_SZ              32

/* ASCII control codes the inline-edit handler expects. */
#define ASCII_BACKSPACE             0x08
#define ASCII_ENTER                 0x0D

/* ARGB8888 pixel layout (matches SDL_PIXELFORMAT_ARGB8888): alpha is
 * the top byte, then R / G / B descending. */
#define ARGB_BYTES_PER_PIXEL        4
#define ARGB_ALPHA_OPAQUE_SHIFTED   (0xFFu << 24)
#define ARGB_R_SHIFT                16
#define ARGB_G_SHIFT                8

/* UTF-8 lead/continuation bytes start at 0x80 — used to drop multi-
 * byte sequences from SDL_TEXTINPUT (the save-slot name field is
 * single-byte latin-1, accepts only space + '0'..'Z'). */
#define UTF8_MULTIBYTE_MARK         0x80

/* PALETTE_BYTES_PER_ENTRY mirrors the 3-byte RGB entries in the .PAL
 * file — kept local so this module doesn't depend on the alpha-blit
 * module's copy. */
#define PALETTE_BYTES_PER_ENTRY     3

/* Default scale factor when --scale wasn't given (1× = native 640×480
 * window). */
#define DEFAULT_SCALE_FACTOR        1

/* Virtual cursor — d-pad / arrow-keys drive a fake mouse so the game
 * is playable on handhelds (Miyoo Mini Plus and friends, where the
 * d-pad arrives as SDLK_UP/DOWN/LEFT/RIGHT and the A/B face buttons
 * arrive as SDLK_SPACE/SDLK_LCTRL). Always-on additive: on a desktop
 * with a real mouse the keyboard path simply doesn't fire; on a
 * handheld it's the only input source. The acceleration curve
 * mirrors what OnionOS apps do — slow at first so single-pixel hit
 * targets stay reachable, then ramps up. */
#define VCUR_BASE_PIXELS_PER_TICK   1
#define VCUR_MAX_PIXELS_PER_TICK    8
#define VCUR_ACCEL_TICKS            10    /* ticks to reach max speed */

/* ---- module state ------------------------------------------------- */

static SDL_Window   *s_win  = NULL;
static SDL_Renderer *s_ren  = NULL;
static SDL_Texture  *s_tex  = NULL;
static int           s_w = 0, s_h = 0;
static int           s_quit = 0;
static uint32_t      s_pixels32[640 * 480];

static uint8_t       s_typed_q[TYPED_QUEUE_SZ];
static int           s_typed_head = 0, s_typed_tail = 0;

/* Virtual cursor state — see VCUR_* constants above. */
static int           s_vcur_x = 320, s_vcur_y = 240;
static int           s_vcur_initialized = 0;
static int           s_vcur_hold_ticks = 0;     /* d-pad-held duration */
static float         s_vcur_rem_x = 0, s_vcur_rem_y = 0;  /* analog sub-px */

extern int         g_headless;
extern int         g_scale_factor;
extern const char *g_scale_mode;
extern int         g_fullscreen;

/* T-switch-aspect — "stretch" (default, fills a 16:9 panel edge to
 * edge, mild horizontal distortion) or "4:3" (letterbox/pillarbox,
 * preserves the original aspect, black bars left+right on a 16:9
 * screen). Persisted via wacki.cfg (config.c) like every other
 * display knob; see ConfigLoad/ConfigSave. Honoured by every target
 * that goes through SDL_RenderSetLogicalSize below — desktop builds
 * already get correct 4:3 proportions from window resizing, so this
 * mainly matters for fixed-panel handhelds with a non-4:3 screen
 * (Switch's 16:9 being the motivating case; Miyoo/Anbernic panels are
 * close enough to 4:3 already that "stretch" and "4:3" look the same
 * there). Writable buffer (not a string literal) so ConfigLoad can
 * overwrite it via sscanf without touching read-only memory. */
char g_aspect_mode[16] = "stretch";

/* T-switch-touch — touchscreen → cursor mode for targets with a touch
 * panel (Nintendo Switch is the motivating case; harmless no-op on
 * desktop/Miyoo/PortMaster, which never get SDL_FINGER* events).
 *   "absolute" — touching the screen jumps the cursor straight to
 *                that point (scaled into 640×480 space). No click is
 *                fired by the touch itself; A/B on the pad (or LMB/
 *                RMB on desktop) remain the only way to click.
 *   "relative" — touchpad-style: the DELTA between consecutive
 *                FINGERMOTION samples moves the cursor by the same
 *                amount, regardless of where on the panel the finger
 *                is. Good for precision aiming without "warping".
 *   "off"      — ignore touch events entirely (pad/keyboard only).
 * Persisted via wacki.cfg as touch_mode=absolute|relative|off. */
char g_touch_mode[16] = "absolute";

/* ---- typed-char ring buffer -------------------------------------- */

void PlatformPushTypedChar(uint8_t c)
{
    int next = (s_typed_head + 1) % TYPED_QUEUE_SZ;
    if (next == s_typed_tail) return;     /* queue full → drop */
    s_typed_q[s_typed_head] = c;
    s_typed_head = next;
}

uint8_t PlatformPollTypedChar(void)
{
    if (s_typed_head == s_typed_tail) return 0;
    uint8_t c = s_typed_q[s_typed_tail];
    s_typed_tail = (s_typed_tail + 1) % TYPED_QUEUE_SZ;
    return c;
}

void PlatformSetTextInput(int on)
{
    if (g_headless) return;
    if (on) SDL_StartTextInput();
    else    SDL_StopTextInput();
    /* Drop any stale chars queued before/after the toggle. */
    s_typed_head = s_typed_tail = 0;
}

/* ---- aspect-ratio helper ------------------------------------------ *
 *
 * CRITICAL CONSTRAINT: SDL_RenderSetLogicalSize doesn't just affect
 * drawing — it also rescales every mouse/touch event's coordinates
 * into that logical space before the engine ever sees them (SDL_
 * MouseMotionEvent.x/y arrive pre-scaled; this is documented SDL
 * behaviour, distinct from SDL_GetMouseState which does NOT rescale).
 * The engine's hit-tests (scene click targets, HUD buttons, the save/
 * load slot picker) all assume mouse coordinates live in 0..640 ×
 * 0..480 — the original framebuffer space. So the logical size must
 * ALWAYS stay at the framebuffer's own w×h regardless of aspect mode,
 * or every click handler silently breaks (this is exactly what went
 * wrong in an earlier draft of this function, which set the logical
 * size to the window's size for "stretch" — that made click
 * coordinates arrive scaled to e.g. 0..1280, which the engine's hit-
 * tests don't expect, and inputs would land on the wrong thing or
 * nothing at all).
 *
 * Given that constraint, "stretch" can't be implemented via
 * SDL_RenderSetLogicalSize at all (SDL's docs confirm it always
 * preserves AR — "if the actual output resolution doesn't have the
 * same aspect ratio the output rendering will be centered", i.e.
 * letterboxed, never disproportionately stretched). So:
 *
 * "4:3"     : SDL_RenderSetLogicalSize(s_ren, w, h) — logical size ==
 *             framebuffer size. SDL's automatic AR-preserving centre/
 *             letterbox kicks in for any output whose AR differs
 *             (e.g. the Switch's 1280×720 panel), giving black bars.
 *             Mouse/touch coordinates stay correctly mapped to 0..640
 *             × 0..480 by SDL's own filtering — this is the side of
 *             the trade-off that "just works".
 * "stretch" : SDL_RenderSetLogicalSize is explicitly RESET to off
 *             (pass 0,0 — SDL's documented way to disable logical-
 *             size scaling) so SDL stops touching mouse/touch
 *             coordinates entirely; PlatformPresent then manually
 *             stretches the 640×480 texture to fill the FULL window
 *             via an explicit destination rect, achieving the
 *             distorted edge-to-edge fill without SDL's coordinate
 *             rescaling ever entering the picture. Mouse/touch
 *             coordinates need a manual window→framebuffer scale in
 *             that case — see scale_event_coords_for_stretch() below,
 *             called from handle_mouse_motion / handle_touch instead
 *             of relying on SDL's (now-disabled) auto-scaling. */
static int g_stretch_active = 0; /* mirrors g_aspect_mode for the hot path */

static void apply_aspect_mode(int w, int h)
{
    if (!s_ren) return;
    if (g_aspect_mode && g_aspect_mode[0] == '4') {
        g_stretch_active = 0;
        SDL_RenderSetLogicalSize(s_ren, w, h);
    } else {
        g_stretch_active = 1;
        SDL_RenderSetLogicalSize(s_ren, 0, 0); /* disable — SDL docs: 0,0 = off */
    }
}

/* Manual window→framebuffer coordinate scale, used ONLY while
 * g_stretch_active (logical-size scaling is off, so SDL no longer
 * rescales event coordinates for us). No-op (returns the input
 * unchanged) whenever logical-size scaling is active, since SDL
 * already did the rescale in that case. */
static void scale_event_coords_for_stretch(float *x, float *y)
{
    if (!g_stretch_active || !s_win || s_w <= 0 || s_h <= 0) return;
    int win_w = s_w, win_h = s_h;
    SDL_GetWindowSize(s_win, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;
    *x = *x * (float)s_w / (float)win_w;
    *y = *y * (float)s_h / (float)win_h;
}

/* ---- init / shutdown --------------------------------------------- */

int PlatformInit(int w, int h, const char *title)
{
    /* App identity for SDL subsystems that surface a name — audio
     * device label, screensaver-inhibit reason, and (when running as a
     * bare binary outside the .app) the macOS menu-bar title. Inside
     * the bundle the menu name comes from CFBundleName; this is the
     * fallback so a terminal-launched build still says "Wacki" rather
     * than the lowercase process name. Must precede SDL_Init. */
#ifdef SDL_HINT_APP_NAME   /* added in SDL 2.0.18; handheld bases ship older */
    SDL_SetHint(SDL_HINT_APP_NAME, "Wacki");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        LOG_INFO("log", "SDL_Init: %s", SDL_GetError());
        return 0;
    }
    s_w = w;
    s_h = h;

#ifdef WACKI_MIYOO
    /* Implemented in platform_miyoo.c — re-apply OnionOS-saved volume
     * via MI_AO_SetVolume. Linked only when TARGET=miyoo. */
    extern void platform_restore_system_volume(void);
    platform_restore_system_volume();
#endif

#if defined(WACKI_PORTMASTER) || defined(WACKI_SWITCH)
    /* Open the handheld's game controller — see
     * src/platform_portmaster.c (Anbernic & friends) or
     * src/platform_switch.c (Nintendo Switch homebrew). Both expose
     * the identical platform_pad_* API consumed below + in
     * PlatformPumpEvents, so a single guard covers both targets. */
    if (!g_headless) {
        extern void platform_pad_open(void);
        platform_pad_open();
    }
#endif

    if (g_headless) {
        /* T45: no window/renderer/texture. SDL stays initialised (dummy
         * video + audio drivers) so PumpEvents still drives the event
         * queue and the mixer callback still fires — CI smoke tests
         * exercise PlayDialogLine + TickMenuMusic etc. */
        const char *drv = SDL_GetCurrentVideoDriver();
        LOG_INFO("platform", "SDL ready (headless): %dx%d, video=%s", w, h, drv ? drv : "?");
        return 1;
    }

#ifndef WACKI_HANDHELD
    /* First launch (no wacki.cfg yet) and the player didn't force a
     * display mode on the command line / env — ask once which mode
     * they want, then persist the choice so we never ask again.
     * Needs SDL video, which SDL_Init above provided; a standalone
     * message box (NULL parent) is fine before any window exists.
     * Handheld skips this — Miyoo is always full-screen and has no
     * pointer to click dialog buttons. */
    extern int  g_config_first_run;
    extern void ConfigSave(void);
    if (g_config_first_run && g_fullscreen == 0 && g_scale_factor == 0) {
        /* Raw UTF-8 literals — the source file is UTF-8, SDL message
         * boxes take UTF-8, and every toolchain we build with (clang,
         * gcc, mingw, arm-gcc) uses a UTF-8 execution charset. Avoids
         * the \x-escape greedy-extend trap (\xBCesz parses "BCe" as
         * one out-of-range escape). */
        const SDL_MessageBoxButtonData btns[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Pełny ekran" },
            { 0,                                       1, "Okno 2×" },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Okno 1×" },
        };
        const SDL_MessageBoxData mbd = {
            SDL_MESSAGEBOX_INFORMATION, NULL,
            "Wacki — tryb wyświetlania",
            "Jak chcesz grać?\n\n"
            "Możesz to później zmienić:\n"
            "  • F11 — przełącz pełny ekran\n"
            "  • rozciągnij okno za róg, aby zmienić zoom",
            (int)SDL_arraysize(btns), btns, NULL
        };
        int choice = -1;
        if (SDL_ShowMessageBox(&mbd, &choice) == 0) {
            switch (choice) {
            case 0: g_fullscreen = 1;                    break;
            case 1: g_fullscreen = 0; g_scale_factor = 2; break;
            case 2: g_fullscreen = 0; g_scale_factor = 1; break;
            default: break;   /* closed without picking → defaults */
            }
        }
        ConfigSave();
    }
#endif

    /* T54 — HiDPI scaling. The framebuffer stays w×h; the SDL window
     * can be enlarged Nx and SDL_RenderSetLogicalSize handles the
     * upscale via SDL_HINT_RENDER_SCALE_QUALITY. */
    int sf    = g_scale_factor > 0 ? g_scale_factor : DEFAULT_SCALE_FACTOR;
    int win_w = w * sf;
    int win_h = h * sf;
    if (g_scale_mode && *g_scale_mode) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, g_scale_mode);
    }

    /* SDL_WINDOW_FULLSCREEN_DESKTOP (not _FULLSCREEN) keeps the desktop
     * resolution and just makes the window cover the active display —
     * cheap to toggle, no jarring mode-switch, plays nicely with macOS
     * Spaces and multi-monitor setups. The framebuffer remains 640×480
     * and SDL_RenderSetLogicalSize letterboxes it inside the screen.
     *
     * SDL_WINDOW_RESIZABLE lets the player drag the window edge to
     * rescale — the most discoverable zoom control (everyone knows
     * how to resize a window). RenderSetLogicalSize keeps the 640×480
     * canvas centred + letterboxed at any window size. */
#ifdef WACKI_PORTMASTER
    /* Anbernic / PortMaster: standard SDL2 drives a KMSDRM panel with no
     * window manager, so always cover the whole display via desktop-
     * fullscreen and let SDL_RenderSetLogicalSize letterbox the 640×480
     * canvas. (Miyoo's mmiyoo backend is inherently fullscreen and is
     * happier left as a "window", so it keeps g_fullscreen as-is.) */
    g_fullscreen = 1;
#endif
#ifdef WACKI_SWITCH
    /* Nintendo Switch: same reasoning as PortMaster above — no window
     * manager, the .nro owns the whole 1280×720 (docked) or 1280×720
     * (handheld, scaled) panel, so always request fullscreen-desktop.
     * Aspect handling (stretch vs 4:3 letterbox) is g_aspect_mode's
     * job via apply_aspect_mode(), not this flag. */
    g_fullscreen = 1;
#endif

    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (g_fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    s_win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, win_flags);
    if (!s_win) {
        LOG_INFO("log", "SDL_CreateWindow: %s", SDL_GetError());
        return 0;
    }

    s_ren = SDL_CreateRenderer(s_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren) {
        /* Try software fallback before bailing. */
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_ren) {
        LOG_INFO("log", "SDL_CreateRenderer: %s", SDL_GetError());
        return 0;
    }

    apply_aspect_mode(w, h);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!s_tex) {
        LOG_INFO("log", "SDL_CreateTexture: %s", SDL_GetError());
        return 0;
    }

    /* Embedded window icon — the 64×64 BMP baked into the binary by
     * the embedded_icon.c slot. SDL scales it for whatever target
     * surface the window manager asks for (taskbar 16×16, dock 128×128,
     * Alt-Tab thumbnail …). Failure to load is non-fatal; falls back
     * to the SDL2 default cross icon. */
    extern unsigned char wacki_icon_bmp[];
    extern unsigned int  wacki_icon_bmp_len;
    SDL_RWops *icon_rw = SDL_RWFromConstMem(wacki_icon_bmp,
                                            (int)wacki_icon_bmp_len);
    if (icon_rw) {
        SDL_Surface *icon = SDL_LoadBMP_RW(icon_rw, 1 /* freesrc */);
        if (icon) {
            SDL_SetWindowIcon(s_win, icon);
            SDL_FreeSurface(icon);
        } else {
            LOG_INFO("platform", "SDL_LoadBMP_RW (icon): %s", SDL_GetError());
        }
    }

#ifdef __APPLE__
    /* Polish-localise SDL's stock menu bar AND add a "Gra" menu with
     * Szybki zapis / odczyt, Zrzut ekranu, Pełny ekran, Pauza — wired
     * to the PlatformMenu* bridges above. Defined in
     * src/platform_macos.m, linked on Darwin desktop builds only; the
     * menu exists by the time SDL_CreateWindow has returned. */
    extern void PlatformSetupMacMenu(void);
    PlatformSetupMacMenu();
#endif

    /* T31 v2 — hide the OS cursor; PaintCursor blits the olowek /
     * kaseta / magnes / drzwi sprite at mouse pos every frame
     * (matches the original DirectDraw build where the GDI cursor was
     * hidden and the engine drew its own sprite via the scene-blit
     * path).
     *
     * The initial call here covers the common case (cursor over the
     * window at launch). PlatformPumpEvents re-asserts SDL_DISABLE on
     * every poll to defeat macOS Cocoa restoring the arrow on focus-
     * loss / mouse-leave / re-enter. The repeated call is cheap (SDL
     * early-outs when the state already matches). */
    SDL_ShowCursor(SDL_DISABLE);

    const char *drv = SDL_GetCurrentVideoDriver();
    LOG_INFO("platform", "SDL ready: %dx%d window (%dx scale, %s filter, fullscreen=%d, aspect=%s), renderer=%s", win_w, win_h, sf, g_scale_mode ? g_scale_mode : "nearest", g_fullscreen, g_aspect_mode ? g_aspect_mode : "stretch", drv ? drv : "?");

    /* Black initial frame so the window is never garbage. */
    memset(s_pixels32, 0, (size_t)w * h * ARGB_BYTES_PER_PIXEL);
    SDL_UpdateTexture(s_tex, NULL, s_pixels32, w * ARGB_BYTES_PER_PIXEL);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
    SDL_PumpEvents();
    return 1;
}

void PlatformShutdown(void)
{
    if (s_tex) { SDL_DestroyTexture (s_tex); s_tex = NULL; }
    if (s_ren) { SDL_DestroyRenderer(s_ren); s_ren = NULL; }
    if (s_win) { SDL_DestroyWindow  (s_win); s_win = NULL; }
    SDL_Quit();
}

/* ---- presentation ------------------------------------------------ */

/* Convert one (palette index, palette[256×3]) frame into the streaming
 * ARGB8888 texture and present it. Caller passes the shadow buffer +
 * the live palette explicitly so this module doesn't reach into
 * graphics.c. */
void PlatformPresent(const uint8_t *shadow,
                     const uint8_t *palette_rgb, int w, int h)
{
    if (g_headless) return;
    if (!s_tex || !shadow || !palette_rgb) return;

    /* SDL_LockTexture maps the GPU/streaming texture into our address
     * space so we expand the 8-bpp shadow + palette LUT straight into
     * its backing memory — one fewer 1.2 MB memcpy per frame than the
     * SDL_UpdateTexture path. Falls back if Lock fails for any reason
     * so we still get a frame on screen. */
    void *pixels = NULL;
    int   pitch  = 0;
    if (SDL_LockTexture(s_tex, NULL, &pixels, &pitch) == 0 && pixels) {
        uint32_t *out         = (uint32_t *)pixels;
        int       row_stride  = pitch / ARGB_BYTES_PER_PIXEL;
        for (int y = 0; y < h; ++y) {
            uint32_t      *row     = out + (size_t)y * row_stride;
            const uint8_t *src_row = shadow + (size_t)y * w;
            for (int x = 0; x < w; ++x) {
                uint8_t        idx = src_row[x];
                const uint8_t *e   = palette_rgb + idx * PALETTE_BYTES_PER_ENTRY;
                row[x] = ARGB_ALPHA_OPAQUE_SHIFTED
                       | ((uint32_t)e[0] << ARGB_R_SHIFT)
                       | ((uint32_t)e[1] << ARGB_G_SHIFT)
                       |  (uint32_t)e[2];
            }
        }
        SDL_UnlockTexture(s_tex);
    } else {
        int n   = w * h;
        int cap = (int)(sizeof s_pixels32 / sizeof s_pixels32[0]);
        if (n > cap) n = cap;
        for (int i = 0; i < n; ++i) {
            uint8_t        idx = shadow[i];
            const uint8_t *e   = palette_rgb + idx * PALETTE_BYTES_PER_ENTRY;
            s_pixels32[i] = ARGB_ALPHA_OPAQUE_SHIFTED
                          | ((uint32_t)e[0] << ARGB_R_SHIFT)
                          | ((uint32_t)e[1] << ARGB_G_SHIFT)
                          |  (uint32_t)e[2];
        }
        SDL_UpdateTexture(s_tex, NULL, s_pixels32, w * ARGB_BYTES_PER_PIXEL);
    }
    SDL_RenderClear(s_ren);
    if (g_stretch_active && s_win) {
        /* Logical-size scaling is off (see apply_aspect_mode), so a
         * NULL dest rect would only fill a 640×480 patch in the
         * corner of the real window — explicitly stretch to the full
         * window instead, which is the entire point of "stretch"
         * mode. */
        int win_w = w, win_h = h;
        SDL_GetWindowSize(s_win, &win_w, &win_h);
        SDL_Rect dest = { 0, 0, win_w, win_h };
        SDL_RenderCopy(s_ren, s_tex, NULL, &dest);
    } else {
        SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    }
    SDL_RenderPresent(s_ren);
}

/* ---- event pump -------------------------------------------------- */

/* Per-event-type handlers — each takes the SDL_Event and returns
 * nothing. PlatformPumpEvents dispatches on ev.type. */

static int input_debug_enabled(void);

#ifndef WACKI_HANDHELD
/* Flip between windowed and desktop-fullscreen at runtime and persist
 * the choice. Shared by the F11 key handler and (on macOS) the "Gra"
 * menu's "Pełny ekran" item. SDL_WINDOW_FULLSCREEN_DESKTOP keeps the
 * desktop resolution and just covers the active display. */
static void toggle_fullscreen_runtime(void)
{
    if (!s_win) return;
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(s_win,
        g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    LOG_INFO("platform", "fullscreen=%d", g_fullscreen);
    extern void ConfigSave(void);
    ConfigSave();
}
#endif

/* Flip g_aspect_mode between "stretch" and "4:3" at runtime and
 * persist the choice — same idea as toggle_fullscreen_runtime above,
 * but for the letterbox setting. Available on every target (not just
 * desktop): wired to the F10 key here, and to the Y button in
 * src/platform_switch.c (Nintendo Switch has no keyboard). Calling
 * apply_aspect_mode() immediately re-applies the logical size so the
 * change is visible the next frame without restarting. */
static void toggle_aspect_mode_runtime(void)
{
    if (g_aspect_mode[0] == '4') {
        strncpy(g_aspect_mode, "stretch", sizeof g_aspect_mode - 1);
    } else {
        strncpy(g_aspect_mode, "4:3", sizeof g_aspect_mode - 1);
    }
    g_aspect_mode[sizeof g_aspect_mode - 1] = '\0';
    apply_aspect_mode(s_w, s_h);
    LOG_INFO("platform", "aspect_mode=%s", g_aspect_mode);
    extern void ConfigSave(void);
    ConfigSave();
}

/* Cycle g_touch_mode absolute → relative → off → absolute … and
 * persist the choice. Wired to the MINUS button in
 * src/platform_switch.c (no keyboard on Switch); harmless to expose
 * on every target since the rest of the touch pipeline simply does
 * nothing on a panel that never emits SDL_FINGER* events. */
static void cycle_touch_mode_runtime(void)
{
    if (strncmp(g_touch_mode, "absolute", 8) == 0) {
        strncpy(g_touch_mode, "relative", sizeof g_touch_mode - 1);
    } else if (strncmp(g_touch_mode, "relative", 8) == 0) {
        strncpy(g_touch_mode, "off", sizeof g_touch_mode - 1);
    } else {
        strncpy(g_touch_mode, "absolute", sizeof g_touch_mode - 1);
    }
    g_touch_mode[sizeof g_touch_mode - 1] = '\0';
    LOG_INFO("platform", "touch_mode=%s", g_touch_mode);
    extern void ConfigSave(void);
    ConfigSave();
}

#ifdef __APPLE__
/* C bridges for the macOS "Gra" menu (src/platform_macos.m). Each maps
 * a menu item to the exact in-engine action its keyboard shortcut
 * triggers: the quicksave / quickload / pause "request" latches the
 * game loop consumes once per frame, a direct screenshot dump, and the
 * shared fullscreen toggle. Cocoa menu actions fire on the main thread
 * during SDL event pumping — the same thread the game loop reads these
 * on — so they're plain writes/calls with no locking. */
void PlatformMenuQuickSave(void)  { g_quicksave_request  = 1; }
void PlatformMenuQuickLoad(void)  { g_quickload_request  = 1; }
void PlatformMenuPause(void)      { g_pause_menu_request = 1; }
void PlatformMenuToggleFull(void) { toggle_fullscreen_runtime(); }
void PlatformMenuScreenshot(void)
{
    extern void ScreenshotToBmpAutoIncrement(void);
    ScreenshotToBmpAutoIncrement();
}
#endif

static void handle_keydown(const SDL_Event *ev)
{
    SDL_Keycode sym = ev->key.keysym.sym;
    /* Latch only real character keys in the low byte. SDL's non-character
     * keys (arrows, F-keys, keypad, …) carry SDLK_SCANCODE_MASK (1<<30);
     * masking them to 8 bits ALIASES letters — SDLK_LEFT (0x40000050) →
     * 0x50 = 'P' and SDLK_F9 (0x40000042) → 0x42 = 'B', which the debug-
     * screenshot handler (frame_tick.c) reads as its PCX/BMP keys. On a
     * handheld the d-pad arrives as arrow keysyms, so holding d-pad LEFT
     * latched 'P' and spammed a screenshot every 500 ms. Store 0 for
     * non-character keys so they can't impersonate a printable key. */
    g_key_state = (sym & SDLK_SCANCODE_MASK) ? 0 : (uint16_t)(sym & 0xFF);

    if (sym == SDLK_ESCAPE) s_quit = 1;

    /* T53 — quicksave / quickload latches consumed by the play_demo_
     * scene main loop once per frame. */
    if (sym == SDLK_F5)  g_quicksave_request  = 1;
    if (sym == SDLK_F9)  g_quickload_request  = 1;
    /* T56 — F3 stats dump (logs to stderr). */
    if (sym == SDLK_F3)  g_stats_dump_request = 1;
    /* T24 — F12 opens the Pytanie quit-confirmation menu. */
    if (sym == SDLK_F12) g_pause_menu_request = 1;

#ifndef WACKI_HANDHELD
    /* F11 toggles fullscreen at runtime — common convention across
     * desktop apps. Skipped on handheld builds (Miyoo has no concept
     * of windowed mode). */
    if (sym == SDLK_F11) toggle_fullscreen_runtime();
#endif
    /* F10 toggles stretch vs 4:3 letterbox at runtime — available on
     * every target (desktop included, for testing/parity); the
     * Switch's Y button calls the same exported function since
     * there's no keyboard there (see PlatformToggleAspectMode below
     * and src/platform_switch.c). */
    if (sym == SDLK_F10) toggle_aspect_mode_runtime();
    /* F8 cycles touch_mode — desktop parity for PlatformCycleTouchMode,
     * which the Switch's MINUS button calls (no keyboard there). */
    if (sym == SDLK_F8) cycle_touch_mode_runtime();

    /* Inline-edit (save-slot rename): queue Backspace / Enter as
     * typed-char events so the edit loop sees them alongside the
     * SDL_TEXTINPUT printable chars. */
    if (sym == SDLK_BACKSPACE)
        PlatformPushTypedChar(ASCII_BACKSPACE);
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
        PlatformPushTypedChar(ASCII_ENTER);

    /* Miyoo hardware buttons arrive as SDL keysyms from the mmiyoo
     * backend; platform_miyoo.c maps them to engine latches. Anbernic /
     * PortMaster instead use real SDL_GameController events (handled in
     * the pump loop), so this keysym path is Miyoo-only. Returns 1 iff a
     * face-button mouse-click latch fired (for the input-debug log). */
#ifdef WACKI_MIYOO
    extern int platform_miyoo_handle_keydown(SDL_Keycode);
    int handled = platform_miyoo_handle_keydown(sym);
#else
    int handled = 0;
#endif

    if (input_debug_enabled()) {
        SDL_Keymod mod = SDL_GetModState();
        LOG_INFO("input",
                 "KEYDOWN sym=0x%X scancode=0x%X mod=0x%X name='%s' repeat=%d → %s",
                 (unsigned)sym, (unsigned)ev->key.keysym.scancode,
                 (unsigned)mod, SDL_GetKeyName(sym),
                 ev->key.repeat,
                 handled ? "fired mouse-click latch" : "no click latch");
    }
}

static void handle_textinput(const SDL_Event *ev)
{
    /* Push the printable bytes (latin-1 single-byte only — the
     * original engine's slot-name field is 30 bytes single-byte and
     * only accepts space + '0'..'Z'). Drop UTF-8 multi-byte
     * sequences entirely. */
    for (const char *p = ev->text.text; *p; ++p) {
        uint8_t c = (uint8_t)*p;
        if (c >= UTF8_MULTIBYTE_MARK) continue;
        PlatformPushTypedChar(c);
    }
}

static void handle_mouse_motion(const SDL_Event *ev)
{
    extern int16_t s_mouse_x, s_mouse_y;
    float x = (float)ev->motion.x, y = (float)ev->motion.y;
    scale_event_coords_for_stretch(&x, &y);
    s_mouse_x = (int16_t)x;
    s_mouse_y = (int16_t)y;
}

/* ---- touchscreen → cursor (Nintendo Switch panel) ---------------- *
 *
 * SDL reports finger coordinates normalized to [0.0, 1.0] across the
 * WHOLE touch surface — independent of the logical/letterboxed render
 * size, so absolute mode multiplies by s_w/s_h directly rather than
 * going through SDL_RenderWindowToLogical (the touch surface and the
 * window surface share the same normalized space by SDL's contract,
 * unlike raw pixel mouse coordinates which DO need that conversion).
 *
 * Relative mode tracks the last finger position per touch ID (just
 * one fingerId in practice — the engine has no multitouch concept,
 * "last finger that moved" wins) and feeds the delta through the same
 * px-per-frame budget the d-pad/analog-stick path uses, so flicking
 * fast across the glass doesn't teleport past PaintCursor's hit-test
 * boundaries in one frame. */
static int      s_touch_active = 0;
static SDL_FingerID s_touch_id = 0;
static float     s_touch_last_x = 0.0f, s_touch_last_y = 0.0f;

static void handle_touch(const SDL_Event *ev)
{
    extern int16_t s_mouse_x, s_mouse_y;
    extern char    g_touch_mode[16];

    if (g_touch_mode[0] == 'o' /* "off" */) return;

    float nx, ny; /* normalized [0,1] */
    SDL_FingerID fid;
    int is_down  = 0;
    int is_up    = 0;

    switch (ev->type) {
    case SDL_FINGERDOWN:
        nx = ev->tfinger.x; ny = ev->tfinger.y; fid = ev->tfinger.fingerId;
        is_down = 1;
        break;
    case SDL_FINGERMOTION:
        nx = ev->tfinger.x; ny = ev->tfinger.y; fid = ev->tfinger.fingerId;
        break;
    case SDL_FINGERUP:
        nx = ev->tfinger.x; ny = ev->tfinger.y; fid = ev->tfinger.fingerId;
        is_up = 1;
        break;
    default:
        return;
    }

    if (g_touch_mode[0] == 'a' /* "absolute" */) {
        /* Tap-to-point: jump the cursor straight to the touched
         * location. No click is fired here — A/B (pad) or LMB/RMB
         * (desktop) remain the only way to click, by design. */
        int px = (int)(nx * s_w);
        int py = (int)(ny * s_h);
        if (px < 0) px = 0; if (px >= s_w) px = s_w - 1;
        if (py < 0) py = 0; if (py >= s_h) py = s_h - 1;
        s_mouse_x = (int16_t)px;
        s_mouse_y = (int16_t)py;
        /* Keep the virtual-cursor state in sync so a subsequent d-pad
         * nudge continues from here instead of snapping back. */
        s_vcur_x = px; s_vcur_y = py; s_vcur_initialized = 1;
        return;
    }

    /* "relative" (touchpad-style): only the delta between consecutive
     * samples for the SAME finger moves the cursor; where on the
     * panel the finger lands is irrelevant. */
    if (is_down || !s_touch_active || fid != s_touch_id) {
        s_touch_active  = 1;
        s_touch_id      = fid;
        s_touch_last_x  = nx;
        s_touch_last_y  = ny;
        if (is_up) s_touch_active = 0;
        return;
    }
    if (is_up) {
        s_touch_active = 0;
        return;
    }

    /* Scale the normalized delta by the touch surface's effective
     * pixel size (== s_w/s_h, since SDL's finger coords already span
     * the whole panel 0..1) so a full-width swipe ≈ a full-width
     * cursor traverse — feels proportional regardless of how the
     * Switch's panel is letterboxed. */
    float dxn = nx - s_touch_last_x;
    float dyn = ny - s_touch_last_y;
    s_touch_last_x = nx;
    s_touch_last_y = ny;

    s_vcur_rem_x += dxn * s_w;
    s_vcur_rem_y += dyn * s_h;
    int mvx = (int)s_vcur_rem_x; s_vcur_rem_x -= (float)mvx; s_vcur_x += mvx;
    int mvy = (int)s_vcur_rem_y; s_vcur_rem_y -= (float)mvy; s_vcur_y += mvy;
    if (s_vcur_x < 0) s_vcur_x = 0; if (s_vcur_x >= s_w) s_vcur_x = s_w - 1;
    if (s_vcur_y < 0) s_vcur_y = 0; if (s_vcur_y >= s_h) s_vcur_y = s_h - 1;
    s_vcur_initialized = 1;
    s_mouse_x = (int16_t)s_vcur_x;
    s_mouse_y = (int16_t)s_vcur_y;
}

/* Env-gated diagnostic — WACKI_INPUT_DEBUG=1 dumps every keydown and
 * mouse-button event so handheld port bugs ("button A fires both LMB
 * and RMB") can be traced. Lazy-init the flag so we don't strcmp on
 * every event. */
static int input_debug_enabled(void)
{
    static int s_flag = -1;
    if (s_flag < 0) {
        const char *e = SDL_getenv("WACKI_INPUT_DEBUG");
        s_flag = (e && *e && *e != '0') ? 1 : 0;
    }
    return s_flag;
}

static void handle_mouse_button_down(const SDL_Event *ev)
{
    if (input_debug_enabled()) {
        LOG_INFO("input",
                 "MOUSEDOWN button=%u state=0x%X clicks=%u x=%d y=%d which=%u",
                 (unsigned)ev->button.button, (unsigned)ev->button.state,
                 (unsigned)ev->button.clicks, ev->button.x, ev->button.y,
                 (unsigned)ev->button.which);
    }
    if (ev->button.button == SDL_BUTTON_LEFT)  g_lmb_clicked = 1;
    if (ev->button.button == SDL_BUTTON_RIGHT) g_rmb_clicked = 1;
}

/* ---- virtual cursor (handheld d-pad → mouse) -------------------- *
 *
 * Polled once per PlatformPumpEvents pass. Reads SDL's current keyboard
 * snapshot (not events — we need REPEAT-style "is the key held right
 * now" semantics so the cursor glides while the d-pad stays pressed).
 * Each tick adds dx/dy to s_mouse_x/y; speed ramps from 1 px/tick to
 * 8 px/tick over ~10 ticks of continuous hold, then plateaus. Release
 * resets the ramp so the next tap is precise again. */
static void poll_virtual_cursor(void)
{
    extern int16_t s_mouse_x, s_mouse_y;

    if (!s_vcur_initialized) {
        /* Seed from real-mouse position so we don't snap on first
         * d-pad press if the user had moved the real cursor. */
        s_vcur_x = s_mouse_x ? s_mouse_x : s_w / 2;
        s_vcur_y = s_mouse_y ? s_mouse_y : s_h / 2;
        s_vcur_initialized = 1;
        /* Publish the seed immediately. On handhelds there's no real
         * mouse-motion event to set s_mouse_x/y, so without this the
         * drawn cursor sits at (0,0) until the first d-pad press — which
         * then looks like it teleports to centre. The no-input path below
         * returns early and never writes s_mouse_x/y, so seed them here. */
        s_mouse_x = (int16_t)s_vcur_x;
        s_mouse_y = (int16_t)s_vcur_y;
    }

    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    int dx = (int)ks[SDL_SCANCODE_RIGHT] - (int)ks[SDL_SCANCODE_LEFT];
    int dy = (int)ks[SDL_SCANCODE_DOWN]  - (int)ks[SDL_SCANCODE_UP];

    /* Analog-stick contribution (px/tick), 0 unless a pad pushes past
     * the deadzone. The d-pad folds into the discrete dx/dy. Filled by
     * src/platform_portmaster.c on Anbernic, src/platform_switch.c on
     * Nintendo Switch; a no-op extern elsewhere. */
    float ax = 0.0f, ay = 0.0f;
#if defined(WACKI_PORTMASTER) || defined(WACKI_SWITCH)
    {
        extern void platform_pad_read_motion(int *, int *, float *, float *);
        platform_pad_read_motion(&dx, &dy, &ax, &ay);
    }
#endif

    if (dx == 0 && dy == 0 && ax == 0.0f && ay == 0.0f) {
        s_vcur_hold_ticks = 0;
        s_vcur_rem_x = s_vcur_rem_y = 0.0f;
        return;
    }

    /* Discrete d-pad / arrow keys: ±1 per axis with the accel ramp. */
    if (dx != 0 || dy != 0) {
        if (dx >  1) dx =  1;
        if (dx < -1) dx = -1;
        if (dy >  1) dy =  1;
        if (dy < -1) dy = -1;
        int speed = VCUR_BASE_PIXELS_PER_TICK +
            (s_vcur_hold_ticks * (VCUR_MAX_PIXELS_PER_TICK - VCUR_BASE_PIXELS_PER_TICK))
            / VCUR_ACCEL_TICKS;
        if (speed > VCUR_MAX_PIXELS_PER_TICK) speed = VCUR_MAX_PIXELS_PER_TICK;
        s_vcur_x += dx * speed;
        s_vcur_y += dy * speed;
        ++s_vcur_hold_ticks;
    } else {
        s_vcur_hold_ticks = 0;
    }

    /* Analog stick: proportional, carrying the sub-pixel remainder so a
     * gentle push still creeps the cursor for fine aiming. */
    s_vcur_rem_x += ax;
    s_vcur_rem_y += ay;
    int mvx = (int)s_vcur_rem_x; s_vcur_rem_x -= (float)mvx; s_vcur_x += mvx;
    int mvy = (int)s_vcur_rem_y; s_vcur_rem_y -= (float)mvy; s_vcur_y += mvy;

    if (s_vcur_x < 0)        s_vcur_x = 0;
    if (s_vcur_y < 0)        s_vcur_y = 0;
    if (s_vcur_x >= s_w)     s_vcur_x = s_w - 1;
    if (s_vcur_y >= s_h)     s_vcur_y = s_h - 1;

    s_mouse_x = (int16_t)s_vcur_x;
    s_mouse_y = (int16_t)s_vcur_y;
    ++s_vcur_hold_ticks;
}

void PlatformPumpEvents(void)
{
    /* T31 v2 — re-assert cursor hide every pump. On macOS Cocoa,
     * SDL_ShowCursor(SDL_DISABLE) called only at init is unreliable —
     * the OS restores the arrow on focus-loss / mouse-leave / re-
     * enter / sometimes after SDL_RenderPresent triggers a re-layout.
     * Re-asserting here is cheap and 100% reliable across Cocoa /
     * X11 / Win32. */
    if (!g_headless) SDL_ShowCursor(SDL_DISABLE);

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            s_quit = 1;
            break;
        case SDL_WINDOWEVENT:
            /* T140 — some window managers (X11 / Wayland) dispatch
             * WINDOWEVENT_CLOSE instead of SDL_QUIT when the user
             * clicks [×]. Treat both as a quit request so the main
             * loop unwinds + flushes saves. */
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) s_quit = 1;
#ifndef WACKI_HANDHELD
            /* Player dragged the window edge to rescale — remember the
             * nearest integer zoom so the next launch reopens at this
             * size. The live rescale itself is free (RenderSetLogical
             * Size letterboxes the 640×480 canvas at any window size);
             * we only persist the bucket. Skipped while fullscreen
             * (the resize there reports the whole display). */
            else if (ev.window.event == SDL_WINDOWEVENT_RESIZED &&
                     !g_fullscreen && s_w > 0)
            {
                int scale = (ev.window.data1 + s_w / 2) / s_w; /* round */
                if (scale < 1) scale = 1;
                if (scale > 8) scale = 8;
                if (scale != g_scale_factor) {
                    g_scale_factor = scale;
                    extern void ConfigSave(void);
                    ConfigSave();
                }
            }
#endif
            break;
        case SDL_KEYDOWN:
            handle_keydown(&ev);
            break;
        case SDL_KEYUP:
            g_key_state &= 0xFF00;
            break;
        case SDL_TEXTINPUT:
            handle_textinput(&ev);
            break;
        case SDL_MOUSEMOTION:
            handle_mouse_motion(&ev);
            break;
        case SDL_MOUSEBUTTONDOWN:
            handle_mouse_button_down(&ev);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            handle_touch(&ev);
            break;
#if defined(WACKI_PORTMASTER) || defined(WACKI_SWITCH)
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            {
                extern int platform_pad_handle_event(const SDL_Event *);
                platform_pad_handle_event(&ev);
            }
            break;
#endif
        }
    }

    /* Drive the virtual cursor AFTER draining events so a real
     * mouse-motion this frame doesn't get clobbered by the d-pad
     * snapshot. If the d-pad isn't held, this is a cheap no-op. */
    if (!g_headless) poll_virtual_cursor();
}

int PlatformShouldQuit(void) { return s_quit; }

/* Public wrapper around toggle_aspect_mode_runtime() — called from
 * src/platform_switch.c's physical-Y-button handler (the Switch has
 * no keyboard, so it can't reach the static F10 handler above
 * directly). Safe to call even before a window exists (apply_aspect_
 * mode no-ops if s_ren is NULL); ConfigSave persists the choice
 * either way. */
void PlatformToggleAspectMode(void)
{
    toggle_aspect_mode_runtime();
}

/* Public wrapper around cycle_touch_mode_runtime() — called from
 * src/platform_switch.c's MINUS-button handler. */
void PlatformCycleTouchMode(void)
{
    cycle_touch_mode_runtime();
}

/* ---- message box ------------------------------------------------- */

void PlatformShowMessageBox(const char *title, const char *body)
{
    if (g_headless) {
        /* No GUI dialog in headless mode — log to stderr so CI runs
         * still see fatal messages (CD-rom missing, archive missing
         * etc.). */
        LOG_TRACE("msgbox", "%s: %s", title ? title : "(null)", body  ? body  : "(null)");
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, body, s_win);
}
