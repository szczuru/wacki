/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — Nintendo 3DS gamepad input with custom controls.
 *
 * Features:
 * - A/B button swap (like Switch) - A=left click, B=right click
 * - X button cycles zoom level (100% → 50% → 25% → 12.5%)
 * - SELECT toggles left/right hand mode for L/ZL/R/ZR mapping
 * - Circle Pad and D-Pad for cursor movement
 * - Touch screen support via SDL_PollEvent in SDL_compat.c */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include <3ds.h>
#include <math.h>

/* Zoom state - exposed to SDL_compat.c for rendering */
static int s_zoom_level = 0;  /* 0=100%, 1=50%, 2=25%, 3=12.5% */

/* Hand mode: 0=right-handed (default), 1=left-handed */
static int s_hand_mode = 0;

/* Button state tracking for edge detection */
static u32 s_prev_keys = 0;

/* Cursor speed from engine */
extern uint16_t g_cursor_speed;

/* Click latches from engine */
extern uint8_t g_lmb_clicked;
extern uint8_t g_rmb_clicked;

/* Quicksave/load latches */
extern uint8_t g_quicksave_request;
extern uint8_t g_quickload_request;

/* Pause menu latch */
extern uint8_t g_pause_menu_request;

/* Exposed to SDL_compat.c for zoom rendering */
int platform_3ds_get_zoom_level(void)
{
    return s_zoom_level;
}

void plat_gamepad_read_cursor(float *ax, float *ay)
{
    if (!ax || !ay) return;
    
    *ax = 0.0f;
    *ay = 0.0f;
    
    hidScanInput();
    
    u32 kHeld = hidKeysHeld();
    u32 kDown = hidKeysDown();
    
    /* Circle Pad (analog) */
    circlePosition pos;
    hidCircleRead(&pos);
    
    /* Deadzone and scaling */
    float stick_x = (float)pos.dx / 156.0f;  /* 3DS circle pad range is ~156 */
    float stick_y = (float)pos.dy / 156.0f;
    
    const float deadzone = 0.15f;
    if (fabsf(stick_x) > deadzone) {
        *ax += stick_x * 8.0f;  /* Scale for cursor speed */
    }
    if (fabsf(stick_y) > deadzone) {
        *ay -= stick_y * 8.0f;  /* Invert Y axis */
    }
    
    /* D-Pad (discrete) */
    if (kHeld & KEY_DRIGHT) *ax += 4.0f;
    if (kHeld & KEY_DLEFT)  *ax -= 4.0f;
    if (kHeld & KEY_DDOWN)  *ay += 4.0f;
    if (kHeld & KEY_DUP)    *ay -= 4.0f;
    
    /* X button - cycle zoom level (edge-triggered) */
    if ((kDown & KEY_X) && !(s_prev_keys & KEY_X)) {
        s_zoom_level = (s_zoom_level + 1) & 3;  /* 0→1→2→3→0 */
        LOG_INFO("3ds", "Zoom level: %d (%.1f%%)", 
                 s_zoom_level, 100.0f / (1 << s_zoom_level));
    }
    
    /* SELECT button - toggle hand mode (edge-triggered) */
    if ((kDown & KEY_SELECT) && !(s_prev_keys & KEY_SELECT)) {
        s_hand_mode = !s_hand_mode;
        LOG_INFO("3ds", "Hand mode: %s", s_hand_mode ? "LEFT" : "RIGHT");
    }
    
    /* A/B buttons - SWAPPED (like Switch)
     * 3DS physical layout: A=right, B=bottom
     * We want: A=left click, B=right click */
    if (kDown & KEY_A) {
        g_lmb_clicked = 1;
    }
    if (kDown & KEY_B) {
        g_rmb_clicked = 1;
    }
    
    /* L/ZL/R/ZR - depends on hand mode
     *
     * RIGHT-HANDED (default):
     *   L  = quickload
     *   ZL = left click (alternative)
     *   R  = quicksave
     *   ZR = right click (alternative)
     *
     * LEFT-HANDED:
     *   L  = left click
     *   ZL = right click
     *   R  = quicksave
     *   ZR = quickload
     */
    
    if (s_hand_mode == 0) {
        /* Right-handed */
        if (kDown & KEY_L)  g_quickload_request = 1;
        if (kDown & KEY_ZL) g_lmb_clicked = 1;
        if (kDown & KEY_R)  g_quicksave_request = 1;
        if (kDown & KEY_ZR) g_rmb_clicked = 1;
    } else {
        /* Left-handed */
        if (kDown & KEY_L)  g_lmb_clicked = 1;
        if (kDown & KEY_ZL) g_rmb_clicked = 1;
        if (kDown & KEY_R)  g_quicksave_request = 1;
        if (kDown & KEY_ZR) g_quickload_request = 1;
    }
    
    /* START button - pause menu */
    if (kDown & KEY_START) {
        g_pause_menu_request = 1;
    }
    
    /* Store current keys for next frame edge detection */
    s_prev_keys = kHeld;
}

int plat_pad_menu_nav(int *up, int *down, int *confirm)
{
    /* Simple menu navigation for pre-game modals */
    static u32 prev = 0;
    
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    
    *up = 0;
    *down = 0;
    *confirm = 0;
    
    /* Edge-triggered navigation */
    if ((kDown & KEY_DUP) || (kDown & KEY_CPAD_UP)) {
        *up = 1;
    }
    if ((kDown & KEY_DDOWN) || (kDown & KEY_CPAD_DOWN)) {
        *down = 1;
    }
    if (kDown & KEY_A) {
        *confirm = 1;
    }
    
    prev = kHeld;
    
    /* Return 1 if any controller is connected (3DS always has built-in controls) */
    return 1;
}

void plat_input_flush(void)
{
    /* Clear any queued input after a modal */
    hidScanInput();
    
    /* Clear click latches */
    extern uint8_t g_lmb_clicked, g_rmb_clicked, g_lmb_handled;
    g_lmb_clicked = 0;
    g_rmb_clicked = 0;
    g_lmb_handled = 0;
    
    /* Clear F-key latches */
    g_quicksave_request = 0;
    g_quickload_request = 0;
    g_pause_menu_request = 0;
}
