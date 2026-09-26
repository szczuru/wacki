/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/touch_vita.c — front / rear touch for PS Vita.
 *
 * Zasady (gdy g_touch_mode != "relative" / touchpad):
 *   - przedni ekran (tap, bez istotnego ruchu) → lewy klik
 *   - tylny panel  (tap)                      → prawy klik
 *
 * W trybie relative (touchpad) ten plik nic nie robi — cursor
 * obsługuje wspólny handle_finger_relative w platform_sdl.c.
 *
 * Identyfikacja paneli (SDL2 Vita):
 *   front touchId == 0
 *   rear  touchId == 1
 * (zgodnie z dokumentacją SDL / VitaSDK).
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include <SDL.h>
#include <string.h>
#include <math.h>

extern char g_touch_mode[16];

#define VITA_FRONT_TOUCH_ID  0
#define VITA_REAR_TOUCH_ID   1
#define TAP_MAX_MOVE_NORM    0.02f   /* ~2% ekranu — powyżej to już drag, nie tap */

typedef struct {
    int   active;
    float start_x, start_y;
    float last_x,  last_y;
    float moved;          /* max |dx|+|dy| od startu */
} VitaFingerTrack;

static VitaFingerTrack s_front;
static VitaFingerTrack s_rear;

static int is_relative_mode(void)
{
    return g_touch_mode[0] == 'r';  /* "relative" */
}

static int is_off_mode(void)
{
    return g_touch_mode[0] == 'o';  /* "off" */
}

/* Wywoływane z PlatformPumpEvents przy SDL_FINGER* (WACKI_VITA). */
void vita_touch_handle(const SDL_Event *ev)
{
    if (is_off_mode() || is_relative_mode())
        return;   /* relative obsługuje wspólny kod; off = nic */

    const int is_rear = (ev->tfinger.touchId == VITA_REAR_TOUCH_ID);
    VitaFingerTrack *t = is_rear ? &s_rear : &s_front;
    float x = ev->tfinger.x;
    float y = ev->tfinger.y;

    switch (ev->type) {
    case SDL_FINGERDOWN:
        t->active  = 1;
        t->start_x = t->last_x = x;
        t->start_y = t->last_y = y;
        t->moved   = 0.f;
        if (!is_rear) {
            /* przedni: ustaw kursor absolutnie */
            extern int s_w, s_h; /* z platform_sdl — albo użyj stałych 640/480 */
            /* Bezpieczniej przez globalse myszy: */
            g_mouse_x = (int16_t)(x * 640);   /* silnik 640×480 */
            g_mouse_y = (int16_t)(y * 480);
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_x > 639) g_mouse_x = 639;
            if (g_mouse_y > 479) g_mouse_y = 479;
        }
        break;

    case SDL_FINGERMOTION:
        if (!t->active) break;
        {
            float dx = fabsf(x - t->start_x);
            float dy = fabsf(y - t->start_y);
            float m  = dx + dy;
            if (m > t->moved) t->moved = m;
            t->last_x = x; t->last_y = y;
            if (!is_rear) {
                g_mouse_x = (int16_t)(x * 640);
                g_mouse_y = (int16_t)(y * 480);
                if (g_mouse_x < 0) g_mouse_x = 0;
                if (g_mouse_y < 0) g_mouse_y = 0;
                if (g_mouse_x > 639) g_mouse_x = 639;
                if (g_mouse_y > 479) g_mouse_y = 479;
            }
        }
        break;

    case SDL_FINGERUP:
        if (!t->active) break;
        t->active = 0;
        /* Tap = mały ruch od FINGERDOWN do FINGERUP */
        if (t->moved <= TAP_MAX_MOVE_NORM) {
            if (is_rear) {
                g_rmb_clicked = 1;
                LOG_INFO("vita-touch", "rear tap → RMB");
            } else {
                g_lmb_clicked = 1;
                LOG_INFO("vita-touch", "front tap → LMB @ %d,%d",
                         (int)g_mouse_x, (int)g_mouse_y);
            }
        }
        break;

    default:
        break;
    }
}
