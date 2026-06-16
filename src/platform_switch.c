/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform_switch.c — Nintendo Switch (homebrew / libnx) gamepad glue.
 *
 * Modeled directly on src/platform_portmaster.c: SDL2 on Switch exposes
 * the combined Joy-Cons / Pro Controller as a single SDL_GameController
 * (index 0 is the "handheld controller" — both Joy-Cons attached to the
 * console act as one pad). We reuse the exact same software-cursor +
 * click mapping so behaviour matches the other handheld targets:
 *
 *   left stick / d-pad   → move the software cursor
 *   A (south)            → left click   (walk / interact)
 *   B (east)             → right click  (HandleSceneInput toggles actor)
 *   Y (west)             → toggle stretch / 4:3 aspect mode (no keyboard
 *                           on Switch to reach platform_sdl.c's F10)
 *   PLUS (start-equiv.)  → pause menu
 *   L  / R               → quickload / quicksave
 *
 * platform_sdl.c calls platform_pad_open() once at init, routes
 * SDL_CONTROLLER* events through platform_pad_handle_event(), and folds
 * platform_pad_read_motion() into its per-frame virtual-cursor poll.
 * Linked only for TARGET=switch (see Makefile).
 *
 * NOTE: SDL2 on Switch maps the controller's "+"/PLUS button to
 * SDL_CONTROLLER_BUTTON_START (libSDL2 keeps the desktop/Xbox naming
 * for the abstract SDL_GameController layout), so the START case below
 * covers it without any extra mapping. */

#include "wacki.h"
#include "wacki/log.h"

#include <SDL.h>

#include <stdint.h>

extern uint8_t g_lmb_clicked;
extern uint8_t g_rmb_clicked;
extern uint8_t g_quicksave_request;
extern uint8_t g_quickload_request;
extern uint8_t g_pause_menu_request;

/* Same tuning as platform_portmaster.c — keeps cursor feel consistent
 * across handheld targets. */
#define PAD_ANALOG_MAX_PX     9
#define PAD_ANALOG_DEADZONE   8000   /* of 32767 */

/* First opened controller. NULL until a pad shows up. On Switch index 0
 * is always present (handheld mode counts as one combined controller),
 * so in practice this resolves immediately at startup. */
static SDL_GameController *s_pad = NULL;

/* Init the controller subsystem and adopt the first available pad. */
void platform_pad_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_INFO("platform", "no game-controller subsystem: %s",
                 SDL_GetError());
        return;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) &&
            (s_pad = SDL_GameControllerOpen(i)) != NULL) {
            LOG_INFO("platform", "game controller: %s",
                     SDL_GameControllerName(s_pad));
            return;
        }
    }
    LOG_INFO("platform", "no game controller found at startup");
}

/* Handle one SDL_CONTROLLER* event. Returns 1 if consumed. Button
 * layout matches platform_portmaster.c / platform_miyoo.c so the same
 * muscle memory carries over from other handheld targets. */
int platform_pad_handle_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:             g_lmb_clicked        = 1; break;
        case SDL_CONTROLLER_BUTTON_B:             g_rmb_clicked        = 1; break;
        case SDL_CONTROLLER_BUTTON_Y:
            {
                extern void PlatformToggleAspectMode(void);
                PlatformToggleAspectMode();
            }
            break;
        case SDL_CONTROLLER_BUTTON_START:         g_pause_menu_request = 1; break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  g_quickload_request  = 1; break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_quicksave_request  = 1; break;
        default: return 0;
        }
        return 1;

    case SDL_CONTROLLERDEVICEADDED:
        /* Hot-plug: detaching/reattaching Joy-Cons or connecting a Pro
         * Controller fires this. Adopt a pad if we don't have one yet. */
        if (!s_pad && SDL_IsGameController(ev->cdevice.which))
            s_pad = SDL_GameControllerOpen(ev->cdevice.which);
        return 1;

    case SDL_CONTROLLERDEVICEREMOVED:
        if (s_pad && ev->cdevice.which ==
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad))) {
            SDL_GameControllerClose(s_pad);
            s_pad = NULL;
        }
        return 1;

    default:
        return 0;
    }
}

/* Fold the pad into the caller's per-frame cursor poll: the d-pad adds to
 * the discrete dx/dy (sharing the keyboard's accel ramp) and the left
 * stick sets the proportional ax/ay (px/tick). No-op without a pad. */
void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    if (!s_pad) return;

    *dx += SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
         - SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    *dy += SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
         - SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);

    int sx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX);
    int sy = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (sx > PAD_ANALOG_DEADZONE || sx < -PAD_ANALOG_DEADZONE)
        *ax = (float)sx / 32767.0f * PAD_ANALOG_MAX_PX;
    if (sy > PAD_ANALOG_DEADZONE || sy < -PAD_ANALOG_DEADZONE)
        *ay = (float)sy / 32767.0f * PAD_ANALOG_MAX_PX;
}
