/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/sdl/platform_sdl.c — portable platform layer (SDL2).
 *
 * Touch modes (g_touch_mode):
 *   "absolute" — punkt na ekranie = kursor; tap = LMB (Vita: touch_vita.c)
 *   "relative" — gładzik: delta ruchu = kursor, bez klików z palca
 *   "off"      — touch wyłączony
 *
 * WACKI_VITA: wszystkie SDL_FINGER* → vita_touch_handle()
 * WACKI_SWITCH: skalowanie myszy w trybie stretch
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include "wacki/platform/input.h"
#include "wacki/platform/system.h"
#include "sdl_internal.h"
#ifdef __ANDROID__
#include "wacki/platform/android_touch.h"
#endif

#include <SDL.h>
#include <stdint.h>
#include <string.h>

#define TYPED_QUEUE_SZ            32
#define ASCII_BACKSPACE           0x08
#define ASCII_ENTER               0x0D
#define UTF8_MULTIBYTE_MARK       0x80
#define VCUR_BASE_PIXELS_PER_TICK 1
#define VCUR_MAX_PIXELS_PER_TICK  8
#define VCUR_ACCEL_TICKS          10

static int     s_w = 0, s_h = 0;
static int     s_quit = 0;
static uint8_t s_typed_q[TYPED_QUEUE_SZ];
static int     s_typed_head = 0, s_typed_tail = 0;

static int   s_vcur_x = 320, s_vcur_y = 240;
static int   s_vcur_initialized = 0;
static int   s_vcur_hold_ticks  = 0;
static float s_vcur_rem_x = 0, s_vcur_rem_y = 0;

extern char g_touch_mode[16];

#ifdef WACKI_VITA
/* Implemented in src/platform/vita/touch_vita.c */
void vita_touch_handle(const SDL_Event *ev);
#endif

/* ---- typed-char ring -------------------------------------------------- */

void PlatformPushTypedChar(uint8_t c)
{
    int next = (s_typed_head + 1) % TYPED_QUEUE_SZ;
    if (next == s_typed_tail) return;
    s_typed_q[s_typed_head] = c;
    s_typed_head = next;
}

uint8_t PlatformPollTypedChar(void)
{
    if (s_typed_head == s_typed_tail) return 0;
    {
        uint8_t c = s_typed_q[s_typed_tail];
        s_typed_tail = (s_typed_tail + 1) % TYPED_QUEUE_SZ;
        return c;
    }
}

void PlatformSetTextInput(int on)
{
    if (g_headless) return;
    if (on) SDL_StartTextInput(); else SDL_StopTextInput();
    s_typed_head = s_typed_tail = 0;
}

/* ---- init / shutdown -------------------------------------------------- */

int PlatformInit(int w, int h, const char *title)
{
#ifdef SDL_HINT_APP_NAME
    SDL_SetHint(SDL_HINT_APP_NAME, "Wacki");
#endif

#if defined(__ANDROID__) || defined(WACKI_VITA)
    /* Własna obsługa touch — SDL nie generuje sztucznych eventów myszy. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#else
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,
                g_touch_mode[0] == 'a' ? "1" : "0");
#endif

    if (SDL_Init(plat_video_sdl_init_flags()) != 0) {
        LOG_INFO("log", "SDL_Init: %s", SDL_GetError());
        return 0;
    }
    s_w = w;
    s_h = h;

    plat_restore_system_volume();
    if (!g_headless) platform_pad_open();

    if (g_headless) {
        LOG_INFO("platform", "SDL ready (headless): %dx%d", w, h);
        return 1;
    }
    return plat_video_init(w, h, title);
}

void PlatformShutdown(void)
{
    plat_video_shutdown();
    SDL_Quit();
}

void PlatformPresent(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (g_headless) return;
    plat_video_present(shadow, pal, w, h);
}

/* ---- event handlers --------------------------------------------------- */

#ifdef __APPLE__
void PlatformMenuQuickSave(void)  { g_quicksave_request  = 1; }
void PlatformMenuQuickLoad(void)  { g_quickload_request  = 1; }
void PlatformMenuPause(void)      { g_pause_menu_request = 1; }
void PlatformMenuToggleFull(void) { plat_video_toggle_fullscreen(); }
void PlatformMenuScreenshot(void)
{
    extern void ScreenshotToBmpAutoIncrement(void);
    ScreenshotToBmpAutoIncrement();
}
#endif

static void handle_keydown(const SDL_Event *ev)
{
    SDL_Keycode sym = ev->key.keysym.sym;
    g_key_state = (sym & SDLK_SCANCODE_MASK) ? 0 : (uint16_t)(sym & 0xFF);

    if (sym == SDLK_F5)  g_quicksave_request  = 1;
    if (sym == SDLK_F9)  g_quickload_request  = 1;
    if (sym == SDLK_F3)  g_stats_dump_request = 1;
    if (sym == SDLK_F12 || sym == SDLK_AC_BACK) g_pause_menu_request = 1;
    if (sym == SDLK_F11) plat_video_toggle_fullscreen();
#ifndef WACKI_PS2
    if (sym == SDLK_F10) platform_video_toggle_aspect_mode();
    if (sym == SDLK_F8)  platform_touch_cycle_mode();
#endif
    if (sym == SDLK_TAB) g_rmb_clicked = 1;
    if (sym == SDLK_BACKSPACE) PlatformPushTypedChar(ASCII_BACKSPACE);
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) PlatformPushTypedChar(ASCII_ENTER);
    plat_handle_platform_key((int)sym);
}

static void handle_textinput(const SDL_Event *ev)
{
    const char *p;
    for (p = ev->text.text; *p; ++p) {
        uint8_t c = (uint8_t)*p;
        if (c >= UTF8_MULTIBYTE_MARK) continue;
        PlatformPushTypedChar(c);
    }
}

static void handle_mouse_motion(const SDL_Event *ev)
{
#ifdef __ANDROID__
    if (ev->motion.which == SDL_TOUCH_MOUSEID && wacki_overlay_owns_touch())
        return;
    g_mouse_x = (int16_t)ev->motion.x;
    g_mouse_y = (int16_t)ev->motion.y;
#elif defined(WACKI_SWITCH) || defined(WACKI_VITA)
    {
        int stretch = 0, win_w = s_w, win_h = s_h, fb_w = s_w, fb_h = s_h;
        platform_video_get_present_state(&stretch, &win_w, &win_h, &fb_w, &fb_h);
        if (stretch && win_w > 0 && win_h > 0) {
            g_mouse_x = (int16_t)(ev->motion.x * fb_w / win_w);
            g_mouse_y = (int16_t)(ev->motion.y * fb_h / win_h);
        } else {
            g_mouse_x = (int16_t)ev->motion.x;
            g_mouse_y = (int16_t)ev->motion.y;
        }
    }
#else
    g_mouse_x = (int16_t)ev->motion.x;
    g_mouse_y = (int16_t)ev->motion.y;
#endif
}

static void handle_mouse_button_down(const SDL_Event *ev)
{
#ifdef __ANDROID__
    if (ev->button.which == SDL_TOUCH_MOUSEID && wacki_overlay_owns_touch())
        return;
#endif
    if (ev->button.button == SDL_BUTTON_LEFT)  g_lmb_clicked = 1;
    if (ev->button.button == SDL_BUTTON_RIGHT) g_rmb_clicked = 1;
}

/* ---- touch (Switch / desktop; Vita → touch_vita.c) -------------------- */
#if !defined(__ANDROID__) && !defined(WACKI_VITA)
static int          s_touch_fingers = 0, s_touch_peak = 0;
static int          s_touch_rel_active = 0;
static SDL_FingerID s_touch_rel_id = 0;
static float        s_touch_rel_last_x = 0, s_touch_rel_last_y = 0;

static void handle_finger_down(void)
{
    if (s_touch_fingers == 0) s_touch_peak = 0;
    if (++s_touch_fingers > s_touch_peak) s_touch_peak = s_touch_fingers;
    if (s_touch_fingers == 2) g_lmb_clicked = 0;
}

static void handle_finger_up(void)
{
    if (s_touch_fingers > 0) --s_touch_fingers;
    if (s_touch_fingers != 0) return;
    if (s_touch_peak >= 2) {
        g_rmb_clicked = 1;
        g_lmb_clicked = 0;
    }
    s_touch_peak = 0;
}

static void handle_finger_relative(const SDL_Event *ev)
{
    float nx = ev->tfinger.x, ny = ev->tfinger.y;
    SDL_FingerID fid = ev->tfinger.fingerId;
    if (ev->type == SDL_FINGERDOWN || !s_touch_rel_active || fid != s_touch_rel_id) {
        s_touch_rel_active = (ev->type != SDL_FINGERUP);
        s_touch_rel_id     = fid;
        s_touch_rel_last_x = nx;
        s_touch_rel_last_y = ny;
        return;
    }
    if (ev->type == SDL_FINGERUP) {
        s_touch_rel_active = 0;
        return;
    }
    {
        float dxn = nx - s_touch_rel_last_x, dyn = ny - s_touch_rel_last_y;
        int mvx, mvy;
        s_touch_rel_last_x = nx;
        s_touch_rel_last_y = ny;
        s_vcur_rem_x += dxn * (float)s_w;
        s_vcur_rem_y += dyn * (float)s_h;
        mvx = (int)s_vcur_rem_x;
        s_vcur_rem_x -= (float)mvx;
        s_vcur_x += mvx;
        mvy = (int)s_vcur_rem_y;
        s_vcur_rem_y -= (float)mvy;
        s_vcur_y += mvy;
        if (s_vcur_x < 0) s_vcur_x = 0;
        if (s_vcur_x >= s_w) s_vcur_x = s_w - 1;
        if (s_vcur_y < 0) s_vcur_y = 0;
        if (s_vcur_y >= s_h) s_vcur_y = s_h - 1;
        s_vcur_initialized = 1;
        g_mouse_x = (int16_t)s_vcur_x;
        g_mouse_y = (int16_t)s_vcur_y;
    }
}
#endif /* !ANDROID && !VITA */

/* ---- virtual cursor (d-pad / analog) ---------------------------------- */

static void poll_virtual_cursor(void)
{
    const uint8_t *ks;
    int dx, dy;
    float ax, ay;

    if (!s_vcur_initialized) {
        s_vcur_x = g_mouse_x ? g_mouse_x : s_w / 2;
        s_vcur_y = g_mouse_y ? g_mouse_y : s_h / 2;
        s_vcur_initialized = 1;
        g_mouse_x = (int16_t)s_vcur_x;
        g_mouse_y = (int16_t)s_vcur_y;
    }

    ks = SDL_GetKeyboardState(NULL);
    dx = (int)ks[SDL_SCANCODE_RIGHT] - (int)ks[SDL_SCANCODE_LEFT];
    dy = (int)ks[SDL_SCANCODE_DOWN]  - (int)ks[SDL_SCANCODE_UP];
    ax = 0.f;
    ay = 0.f;
    platform_pad_read_motion(&dx, &dy, &ax, &ay);

    if (dx == 0 && dy == 0 && ax == 0.f && ay == 0.f) {
        s_vcur_hold_ticks = 0;
        s_vcur_rem_x = s_vcur_rem_y = 0.f;
        return;
    }

    if (dx != 0 || dy != 0) {
        int spd;
        if (dx >  1) dx =  1;
        if (dx < -1) dx = -1;
        if (dy >  1) dy =  1;
        if (dy < -1) dy = -1;
        spd = VCUR_BASE_PIXELS_PER_TICK +
            (s_vcur_hold_ticks * (VCUR_MAX_PIXELS_PER_TICK - VCUR_BASE_PIXELS_PER_TICK))
            / VCUR_ACCEL_TICKS;
        if (spd > VCUR_MAX_PIXELS_PER_TICK) spd = VCUR_MAX_PIXELS_PER_TICK;
        s_vcur_x += dx * spd;
        s_vcur_y += dy * spd;
        ++s_vcur_hold_ticks;
    } else {
        s_vcur_hold_ticks = 0;
    }

    s_vcur_rem_x += ax;
    s_vcur_rem_y += ay;
    {
        int mvx = (int)s_vcur_rem_x;
        s_vcur_rem_x -= (float)mvx;
        s_vcur_x += mvx;
    }
    {
        int mvy = (int)s_vcur_rem_y;
        s_vcur_rem_y -= (float)mvy;
        s_vcur_y += mvy;
    }

    if (s_vcur_x < 0) s_vcur_x = 0;
    if (s_vcur_y < 0) s_vcur_y = 0;
    if (s_vcur_x >= s_w) s_vcur_x = s_w - 1;
    if (s_vcur_y >= s_h) s_vcur_y = s_h - 1;

    g_mouse_x = (int16_t)s_vcur_x;
    g_mouse_y = (int16_t)s_vcur_y;
}

/* ---- event pump ------------------------------------------------------- */

void PlatformPumpEvents(void)
{
    SDL_Event ev;

    if (!g_headless) SDL_ShowCursor(SDL_DISABLE);

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            s_quit = 1;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                s_quit = 1;
            else if (ev.window.event == SDL_WINDOWEVENT_RESIZED &&
                     !g_fullscreen && s_w > 0) {
                int sc = (ev.window.data1 + s_w / 2) / s_w;
                if (sc < 1) sc = 1;
                if (sc > 8) sc = 8;
                if (sc != g_scale_factor) {
                    g_scale_factor = sc;
                    {
                        extern void ConfigSave(void);
                        ConfigSave();
                    }
                }
            }
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
#ifdef __ANDROID__
            if (ev.type == SDL_FINGERDOWN)
                wacki_overlay_finger_down(ev.tfinger.fingerId,
                                          ev.tfinger.x, ev.tfinger.y);
            else if (ev.type == SDL_FINGERMOTION)
                wacki_overlay_finger_motion(ev.tfinger.fingerId,
                                             ev.tfinger.x, ev.tfinger.y);
            else
                wacki_overlay_finger_up(ev.tfinger.fingerId,
                                         ev.tfinger.x, ev.tfinger.y);
#elif defined(WACKI_VITA)
            /* off / absolute / relative — wszystko w touch_vita.c */
            vita_touch_handle(&ev);
#else
            if (g_touch_mode[0] == 'a') {
                if (ev.type == SDL_FINGERDOWN) handle_finger_down();
                else if (ev.type == SDL_FINGERUP) handle_finger_up();
            } else if (g_touch_mode[0] == 'r') {
                handle_finger_relative(&ev);
            }
            /* 'o' = off → ignoruj */
#endif
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            platform_pad_handle_event(&ev);
            break;

        default:
            break;
        }
    }

    if (!g_headless) poll_virtual_cursor();
#ifdef __ANDROID__
    if (!g_headless) wacki_overlay_tick();
#endif
}

int PlatformShouldQuit(void)
{
    return s_quit;
}

/* ---- touch mode cycle ------------------------------------------------- */

void platform_touch_cycle_mode(void)
{
    if (strncmp(g_touch_mode, "absolute", 8) == 0)
        strncpy(g_touch_mode, "relative", 15);
    else if (strncmp(g_touch_mode, "relative", 8) == 0)
        strncpy(g_touch_mode, "off", 15);
    else
        strncpy(g_touch_mode, "absolute", 15);
    g_touch_mode[15] = '\0';

#if !defined(__ANDROID__) && !defined(WACKI_VITA)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,
                g_touch_mode[0] == 'a' ? "1" : "0");
#endif

    LOG_INFO("platform", "touch_mode=%s", g_touch_mode);
    {
        extern void ConfigSave(void);
        ConfigSave();
    }
}

/* ---- message box ------------------------------------------------------ */

void PlatformShowMessageBox(const char *title, const char *body)
{
    if (g_headless) {
        LOG_TRACE("msgbox", "%s: %s", title, body);
        return;
    }
    plat_video_message_box(title, body);
}
