/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.h — private declarations shared between
 * gamepad_3ds.c (implementation) and platform_3ds.c (PlatformInit /
 * PlatformPumpEvents call sites).
 *
 * Mirrors src/platform/sdl/sdl_internal.h's role for platform_sdl.c:
 * these two entry points are 3DS-internal wiring, not part of the
 * portable wacki/platform/input.h HAL (that header only exposes the
 * capability queries every platform must answer, e.g. plat_input_has_keyboard). */
#ifndef WACKI_3DS_GAMEPAD_H
#define WACKI_3DS_GAMEPAD_H

/* Opens/prepares 3DS input (called once from PlatformInit). */
void platform_pad_open(void);

/* Scans hidScanInput() once and latches every edge-triggered action
 * (clicks, quicksave/load, pause, hand-mode toggle, zoom cycle, touch)
 * into the shared globals. Called once per frame from PlatformPumpEvents. */
void platform_pad_handle_buttons(void);

#endif /* WACKI_3DS_GAMEPAD_H */
