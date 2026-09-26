/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * src/platform/vita/touch_vita.c — front / rear touch.
 *
 * mode != relative && != off:
 *   front tap → LMB (+ absolutna pozycja kursora)
 *   rear  tap → RMB
 */

#include "wacki.h"
#include "wacki/log.h"
#include <SDL.h>
#include <math.h>

extern char g_touch_mode[16];

#define VITA_FRONT_TOUCH_ID  0
#define VITA_REAR_TOUCH_ID   1
#define GAME_W  640
#define GAME_H  480
#define TAP_MAX_MOVE  0.025f

typedef struct {
    int   active;
    float start_x, start_y;
    float moved;
} VitaFingerTrack;

static VitaFingerTrack s_front;
static VitaFingerTrack s_rear;

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
    int is_rear;
    VitaFingerTrack *t;
    float x, y;

    if (!ev) return;
    if (g_touch_mode[0] == 'r' || g_touch_mode[0] == 'o')
        return;

    is_rear = (ev->tfinger.touchId == (SDL_TouchID)VITA_REAR_TOUCH_ID);
    t = is_rear ? &s_rear : &s_front;
    x = ev->tfinger.x;
    y = ev->tfinger.y;

    switch (ev->type) {
    case SDL_FINGERDOWN:
        t->active  = 1;
        t->start_x = x;
        t->start_y = y;
        t->moved   = 0.f;
        if (!is_rear) set_cursor_from_norm(x, y);
        break;

    case SDL_FINGERMOTION:
        if (!t->active) break;
        {
            float m = fabsf(x - t->start_x) + fabsf(y - t->start_y);
            if (m > t->moved) t->moved = m;
        }
        if (!is_rear) set_cursor_from_norm(x, y);
        break;

    case SDL_FINGERUP:
        if (!t->active) break;
        t->active = 0;
        if (t->moved <= TAP_MAX_MOVE) {
            if (is_rear) {
                g_rmb_clicked = 1;
                LOG_INFO("vita-touch", "rear tap → RMB");
            } else {
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
