/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/sdl/pad_layout.c — plat_pad_is_nintendo_layout for SDL-family
 * non-Nintendo targets (desktop, PortMaster, Miyoo, PS2).
 *
 * On these targets a Nintendo pad (Joy-Con pair, Pro Controller) may or may
 * not be connected — query SDL_GameControllerGetType() to detect it. The
 * JoyCon-specific enum values (JOYCON_LEFT/RIGHT/PAIR) were added in SDL
 * 2.24.0; older builds of SDL2 (common on handheld/PS2 portlibs) only have
 * NINTENDO_SWITCH_PRO, so guard the newer values behind SDL_VERSION_ATLEAST.
 *
 * Nintendo homebrew targets (Switch, Wii, …) link
 * src/platform/nintendo/nintendo_gamepad.c instead — which always returns 1
 * without a type query, since on Nintendo hardware the layout is always
 * Nintendo's regardless of SDL's pad-type detection. */

#include "wacki/platform/input.h"
#include <SDL.h>

int plat_pad_is_nintendo_layout(SDL_GameController *pad)
{
    if (!pad) return 0;
    SDL_GameControllerType t = SDL_GameControllerGetType(pad);
    if (t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) return 1;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    if (t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT  ||
        t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
        t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)  return 1;
#endif
    return 0;
}
