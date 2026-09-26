/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła / szczuru
 *
 * absolute: pozycja + LMB na tapie (tylko przedni ekran)
 * relative: tylko ruch kursora (bez klików)
 * off:      nic
 * tylny panel w absolute: RMB na tapie
 */

#include "wacki.h"
#include "wacki/log.h"
#include <SDL.h>
#include <math.h>
#include <string.h>

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

/* relative (touchpad) state */
static int          s_rel_active;
static SDL_FingerID s_rel_id;
static float        s_rel_last_x, s_rel_last_y;

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

static void handle_relative(const SDL_Event *ev)
{
    float nx = ev->tfinger.x, ny = ev->tfinger.y;
    SDL_FingerID fid = ev->tfinger.fingerId;

    /* tylko przedni panel jako touchpad */
    if (ev->tfinger.touchId == (SDL_TouchID)VITA_REAR_TOUCH_ID)
        return;

    if (ev->type == SDL_FINGERDOWN || !s_rel_active || fid != s_rel_id) {
        s_rel_active = (ev->type != SDL_FINGERUP);
        s_rel_id = fid;
        s_rel_last_x = nx;
        s_rel_last_y = ny;
        return;
    }
    if (ev->type == SDL_FINGERUP) {
        s_rel_active = 0;
        return;
    }
    /* FINGERMOTION — delta → kursor, BEZ klików */
    {
        float dx = (nx - s_rel_last_x) * (float)GAME_W;
        float dy = (ny - s_rel_last_y) * (float)GAME_H;
        int x = (int)g_mouse_x + (int)dx;
        int y = (int)g_mouse_y + (int)dy;
        s_rel_last_x = nx;
        s_rel_last_y = ny;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= GAME_W) x = GAME_W - 1;
        if (y >= GAME_H) y = GAME_H - 1;
        g_mouse_x = (int16_t)x;
        g_mouse_y = (int16_t)y;
    }
}

static void handle_absolute(const SDL_Event *ev)
{
    int is_rear = (ev->tfinger.touchId == (SDL_TouchID)VITA_REAR_TOUCH_ID);
    VitaFingerTrack *t = is_rear ? &s_rear : &s_front;
    float x = ev->tfinger.x, y = ev->tfinger.y;

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
            } else {
                set_cursor_from_norm(x, y);
                g_lmb_clicked = 1;
            }
        }
        break;
    default:
        break;
    }
}

void vita_touch_handle(const SDL_Event *ev)
{
    if (!ev) return;
    if (g_touch_mode[0] == 'o') /* off */
        return;
    if (g_touch_mode[0] == 'r') /* relative / touchpad */
        handle_relative(ev);
    else
        handle_absolute(ev); /* absolute (domyślnie) */
}
