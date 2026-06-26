/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/nintendo/nintendo_gamepad.c — plat_pad_is_nintendo_layout
 * implementation for Nintendo homebrew targets (Switch today; intended to
 * cover Wii, Wii U, 3DS and any other Nintendo target in the future).
 *
 * On Nintendo hardware the controller is ALWAYS in Nintendo's button layout
 * (south=B, east=A, west=Y, north=X) — no runtime SDL_GameControllerGetType()
 * query is needed; we're definitionally on a Nintendo pad. Linked by
 * mk/switch.mk (and future mk/wii.mk, mk/3ds.mk, …) instead of
 * src/platform/sdl/pad_layout.c, which the SDL-family non-Nintendo targets
 * (desktop, PortMaster, PS2) use. */

#include "wacki/platform/input.h"
#include <SDL.h>

int plat_pad_is_nintendo_layout(SDL_GameController *pad)
{
    (void)pad; /* unconditionally true on Nintendo hardware */
    return 1;
}
