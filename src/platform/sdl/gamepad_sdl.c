/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/sdl/gamepad_sdl.c — SDL_GameController → cursor glue.
 *
 * Shared by every pad-driven target — PortMaster (Anbernic & friends), the
 * PS2's DualShock, the Vita, the Nintendo Switch — wherever standard SDL2
 * exposes the controls as a real SDL_GameController. This module owns the
 * controller handle and maps it onto the engine's existing software-cursor +
 * click model (the keysym-button counterpart for the Miyoo lives in
 * miyoo/miyoo.c).
 *
 *   left stick / d-pad   → move the software cursor
 *   A (south position)   → left click   (walk / interact)
 *   B (east position)    → right click  (HandleSceneInput toggles actor)
 *   X (west position)    → toggle stretch / true-aspect video mode
 *   BACK / MINUS         → cycle touch-input mode (absolute / relative / off)
 *   START                → pause menu
 *   L1 / R1              → quickload / quicksave
 *
 * Nintendo button layout: SDL names buttons by DIAMOND POSITION using the
 * Xbox convention (A=south, B=east, X=west, Y=north), not the letter printed
 * on the pad. Nintendo's physical silkscreen is the mirror image at those
 * same four positions (B=south, A=east, Y=west, X=north). Detection of
 * whether a connected pad uses Nintendo's layout is delegated to the
 * platform-HAL hook plat_pad_is_nintendo_layout() (input.h), implemented in:
 *   src/platform/nintendo/nintendo_gamepad.c — always 1 on Nintendo HW
 *   src/platform/sdl/pad_layout.c            — SDL type query on other hosts
 * This keeps the detection logic out of this shared file.
 *
 * platform_sdl.c calls platform_pad_open() once at init, routes
 * SDL_CONTROLLER* events through platform_pad_handle_event(), and folds
 * platform_pad_read_motion() into its per-frame virtual-cursor poll. Linked
 * for the pad targets (see Makefile); not desktop or miyoo. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include "sdl_internal.h"

#include <SDL.h>
#include <stdint.h>

#define PAD_ANALOG_MAX_PX     9
#define PAD_ANALOG_DEADZONE   8000

static SDL_GameController *s_pad = NULL;

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
            LOG_INFO("platform", "game controller: %s%s",
                     SDL_GameControllerName(s_pad),
                     plat_pad_is_nintendo_layout(s_pad) ? " (Nintendo layout)" : "");
            return;
        }
    }
}

int platform_pad_handle_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_CONTROLLERBUTTONDOWN: {
        SDL_GameControllerButton btn = ev->cbutton.button;
        /* Remap physical A/B/X/Y to match the correct diamond position when
         * the pad uses Nintendo's layout instead of Xbox's. Delegated to the
         * HAL hook so no Nintendo-specific code lives in this shared file. */
        if (plat_pad_is_nintendo_layout(s_pad)) {
            if      (btn == SDL_CONTROLLER_BUTTON_A) btn = SDL_CONTROLLER_BUTTON_B;
            else if (btn == SDL_CONTROLLER_BUTTON_B) btn = SDL_CONTROLLER_BUTTON_A;
            else if (btn == SDL_CONTROLLER_BUTTON_X) btn = SDL_CONTROLLER_BUTTON_Y;
            else if (btn == SDL_CONTROLLER_BUTTON_Y) btn = SDL_CONTROLLER_BUTTON_X;
        }
        switch (btn) {
        case SDL_CONTROLLER_BUTTON_A:             g_lmb_clicked        = 1; break;
        case SDL_CONTROLLER_BUTTON_B:             g_rmb_clicked        = 1; break;
        case SDL_CONTROLLER_BUTTON_X:             platform_video_toggle_aspect_mode(); break;
        case SDL_CONTROLLER_BUTTON_BACK:          platform_touch_cycle_mode();         break;
        case SDL_CONTROLLER_BUTTON_START:         g_pause_menu_request = 1; break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  g_quickload_request  = 1; break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_quicksave_request  = 1; break;
        default: return 0;
        }
        return 1;
    }

    case SDL_CONTROLLERDEVICEADDED:
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

void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    if (s_pad) {
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
    plat_pad_read_extra(ax, ay);
}

int plat_pad_menu_nav(int *up, int *down, int *confirm)
{
    *up = *down = *confirm = 0;
    if (!s_pad) return 0;

    SDL_GameControllerUpdate();

    int sy = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY);
    int u = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)
            || sy < -PAD_ANALOG_DEADZONE;
    int d = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            || sy >  PAD_ANALOG_DEADZONE;
    int c = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_A);

    static int p_u = 0, p_d = 0, p_c = 0;
    if (u && !p_u) *up = 1;
    if (d && !p_d) *down = 1;
    if (c && !p_c) *confirm = 1;
    p_u = u; p_d = d; p_c = c;
    return 1;
}

void plat_input_flush(void)
{
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    g_lmb_clicked = 0;
    g_rmb_clicked = 0;
}
