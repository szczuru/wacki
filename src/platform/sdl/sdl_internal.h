/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/sdl/sdl_internal.h — cross-file declarations within the SDL
 * platform family (mirrors src/platform/ps2/ps2_internal.h). NOT a public
 * engine header; it just gives platform_sdl.c (the input/event pump),
 * gamepad_sdl.c (the controller glue), and video_sdl.c (presentation) a
 * single declaration point for cross-file calls instead of ad-hoc local
 * externs.
 */
#ifndef WACKI_PLATFORM_SDL_INTERNAL_H
#define WACKI_PLATFORM_SDL_INTERNAL_H

#include <SDL.h>

/* gamepad_sdl.c — the shared SDL_GameController → cursor glue, driven by the
 * input/event pump in platform_sdl.c. Runtime no-ops where no pad is open. */
void platform_pad_open(void);
int  platform_pad_handle_event(const SDL_Event *ev);
void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay);

/* video_sdl.c — toggle between "stretch" (fills the output edge to edge,
 * disproportionate on a non-4:3 display) and "4:3" (true aspect, letterboxed
 * with bars) at runtime, persisting the choice. Wired to the X button
 * (gamepad_sdl.c) and a desktop key (platform_sdl.c). Mainly matters on
 * fixed-panel handhelds whose screen isn't 4:3 (Switch's 16:9 panel being
 * the motivating case); harmless on displays that are already ~4:3. */
void platform_video_toggle_aspect_mode(void);

/* video_sdl.c — lets platform_sdl.c's mouse/touch handlers know whether
 * logical-size scaling is currently active. SDL only auto-rescales
 * SDL_MOUSEMOTION / SDL_FINGER* coordinates into framebuffer space when
 * logical-size scaling is ON ("4:3" mode); in "stretch" mode it's disabled,
 * so the event pump has to do that scaling itself using the window size
 * this reports. */
void platform_video_get_present_state(int *stretch_active, int *win_w, int *win_h,
                                      int *fb_w, int *fb_h);

/* platform_sdl.c — cycle touch-input mode absolute → relative → off →
 * absolute … at runtime, persisting the choice. Wired to BACK/MINUS
 * (gamepad_sdl.c) and a desktop key. No-op in effect on a target that never
 * emits SDL_FINGER* events (no touch panel) — safe to wire up everywhere. */
void platform_touch_cycle_mode(void);

#endif /* WACKI_PLATFORM_SDL_INTERNAL_H */
