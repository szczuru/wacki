/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/touch_vita.c — front / rear touch for PS Vita.
 *
 * Gdy g_touch_mode != "relative" (touchpad) i != "off":
 *   - przedni ekran: absolutna pozycja kursora + lewy klik przy tapie
 *   - tylny panel:   prawy klik przy tapie
 *
 * W trybie relative obsługę przejmuje wspólny handle_finger_relative
 * w platform_sdl.c (tylko ruch kursora, bez auto-klika).
 *
 * touchId (SDL2 Vita):
 *   0 = front
 *   1 = rear
 */

#include "wacki.h"
#include "wacki/log.h"

#include <SDL.h>
#include <math.h>
#include <string.h>

extern char g_touch_mode[16];

#define VITA_FRONT_TOUCH_ID  0
#define VITA_REAR_TOUCH_ID   1

/* Silnik renderuje 640×480 — normalizowane 0..1 z SDL mapujemy na to. */
#define GAME_W  640
#define GAME_H  480

/* Max ruch od DOWN do UP (w jednostkach znormalizowanych), żeby uznać tap. */
#define TAP_MAX_MOVE  0.025f

typedef struct {
    int   active;
    float start_x, start_y;
    float moved;   /* max |dx|+|dy| od startu */
} VitaFingerTrack;

static VitaFingerTrack s_front;
static VitaFingerTrack s_rear;

static int mode_is_relative(void)
{
    return g_touch_mode[0] == 'r';
}

static int mode_is_off(void)
{
    return g_touch_mode[0] == 'o';
}

static void set_cursor_from_norm(float nx, float ny)
{
    int x = (int)(nx * (float)GAME_W);
    int y = (int)(ny * (float)GAME_H);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= GAME_W) x = GAME_W - 1;
    if (y >= GAME_H) y = GAME_H - 1;
    g_mouse_x = (int16_t)x;
    g_mouse_y = (int16_t)y;
}

void vita_touch_handle(const SDL_Event *ev)
{
    if (!ev) return;
    if (mode_is_off() || mode_is_relative())
        return;

    const int is_rear = (ev->tfinger.touchId == (SDL_TouchID)VITA_REAR_TOUCH_ID);
    VitaFingerTrack *t = is_rear ? &s_rear : &s_front;
    const float x = ev->tfinger.x;
    const float y = ev->tfinger.y;

    switch (ev->type) {
    case SDL_FINGERDOWN:
        t->active  = 1;
        t->start_x = x;
        t->start_y = y;
        t->moved   = 0.f;
        if (!is_rear)
            set_cursor_from_norm(x, y);
        break;

    case SDL_FINGERMOTION:
        if (!t->active)
            break;
        {
            float m = fabsf(x - t->start_x) + fabsf(y - t->start_y);
            if (m > t->moved)
                t->moved = m;
            if (!is_rear)
                set_cursor_from_norm(x, y);
        }
        break;

    case SDL_FINGERUP:
        if (!t->active)
            break;
        t->active = 0;
        if (t->moved <= TAP_MAX_MOVE) {
            if (is_rear) {
                g_rmb_clicked = 1;
                LOG_INFO("vita-touch", "rear tap → RMB");
            } else {
                /* Upewnij się, że kursor jest w miejscu tapu. */
                set_cursor_from_norm(x, y);
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
