/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — 3DS button and touch input.
 *
 * Button mapping (A/B swapped like Switch for consistency):
 * - Physical A (right position) → Left mouse button
 * - Physical B (bottom position) → Right mouse button
 * - X → Cycle zoom level (1x → 2x → 4x → 8x → 1x)
 * - Y → (unused, available for future features)
 * - SELECT → Toggle left/right hand mode
 * - START → Pause menu
 * - L/R/ZL/ZR → Depends on hand mode (see below)
 * - D-Pad → Discrete cursor movement
 * - Circle Pad → Analog cursor movement
 * - Touch screen → Direct cursor positioning (maps to bottom screen zoom view)
 *
 * Hand modes:
 * - RIGHT-HAND (default): L=quickload, ZL=LMB, R=quicksave, ZR=RMB
 * - LEFT-HAND: L=LMB, ZL=RMB, R=quicksave, ZR=quickload
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"

#include <3ds.h>
#include <SDL.h>

#define PAD_ANALOG_DEADZONE  15   /* Circle pad deadzone */
#define PAD_ANALOG_MAX_PX    8    /* Max pixels per frame from analog */
#define MAX_ZOOM_LEVEL       3    /* 0=1x, 1=2x, 2=4x, 3=8x */

/* Zoom level for bottom screen */
static int s_zoom_level = 0;

/* Hand mode: 0=right-hand (default), 1=left-hand */
static int s_hand_mode = 0;

void platform_pad_open(void)
{
    /* 3DS gamepad is always available via hidScanInput */
    LOG_INFO("3ds-input", "Gamepad ready (A/B swapped, zoom: X, hand mode: SELECT)");
}

void platform_pad_close(void)
{
    /* No cleanup needed */
}

void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    /* Scan for button input */
    hidScanInput();
    
    u32 kDown = hidKeysDown();  /* Pressed this frame */
    u32 kHeld = hidKeysHeld();  /* Held this frame */
    
    /* ===== Button Events (edge-triggered) ===== */
    
    /* START → Pause menu */
    if (kDown & KEY_START) {
        g_pause_menu_request = 1;
    }
    
    /* SELECT → Toggle hand mode */
    if (kDown & KEY_SELECT) {
        s_hand_mode = !s_hand_mode;
        LOG_INFO("3ds-input", "Hand mode: %s",
                 s_hand_mode ? "LEFT-HAND" : "RIGHT-HAND");
    }
    
    /* X → Cycle zoom level */
    if (kDown & KEY_X) {
        s_zoom_level = (s_zoom_level + 1) % (MAX_ZOOM_LEVEL + 1);
        const char *zoom_str[] = {"1x", "2x", "4x", "8x"};
        LOG_INFO("3ds-input", "Zoom: %s", zoom_str[s_zoom_level]);
    }
    
    /* Face buttons: A/B swapped (Nintendo layout vs Xbox layout)
     * Physical A button (right position) = Left click
     * Physical B button (bottom position) = Right click
     * This matches Switch port for consistency */
    if (kDown & KEY_A) {
        g_lmb_clicked = 1;
    }
    if (kDown & KEY_B) {
        g_rmb_clicked = 1;
    }
    
    /* Y button - currently unused, reserved for future features
     * Could be: cycle touch mode, toggle grid, etc. */
    
    /* Shoulder buttons - function depends on hand mode
     * This lets left-handed players use L/ZL for mouse clicks
     * while right-handed players use R/ZR */
    if (s_hand_mode == 0) {
        /* RIGHT-HAND mode (default) */
        if (kDown & KEY_L)  g_quickload_request = 1;
        if (kDown & KEY_ZL) g_lmb_clicked = 1;
        if (kDown & KEY_R)  g_quicksave_request = 1;
        if (kDown & KEY_ZR) g_rmb_clicked = 1;
    } else {
        /* LEFT-HAND mode */
        if (kDown & KEY_L)  g_lmb_clicked = 1;
        if (kDown & KEY_ZL) g_rmb_clicked = 1;
        if (kDown & KEY_R)  g_quicksave_request = 1;
        if (kDown & KEY_ZR) g_quickload_request = 1;
    }
    
    /* ===== D-Pad: Discrete cursor movement ===== */
    if (kHeld & KEY_DRIGHT) (*dx)++;
    if (kHeld & KEY_DLEFT)  (*dx)--;
    if (kHeld & KEY_DDOWN)  (*dy)++;
    if (kHeld & KEY_DUP)    (*dy)--;
    
    /* ===== Circle Pad: Analog cursor movement ===== */
    circlePosition pos;
    hidCircleRead(&pos);
    
    /* Apply deadzone and scale to pixels */
    if (pos.dx > PAD_ANALOG_DEADZONE || pos.dx < -PAD_ANALOG_DEADZONE) {
        /* Circle pad range: -156 to +156 */
        *ax = ((float)pos.dx / 156.0f) * PAD_ANALOG_MAX_PX;
    }
    
    if (pos.dy > PAD_ANALOG_DEADZONE || pos.dy < -PAD_ANALOG_DEADZONE) {
        /* Y is inverted (up = positive in hardware, but we want up = negative) */
        *ay = -((float)pos.dy / 156.0f) * PAD_ANALOG_MAX_PX;
    }
    
    /* ===== Touch Screen: Direct cursor positioning ===== */
    if (kDown & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        
        /* Touch screen is on bottom screen (320x240) showing zoomed view
         * We need to map touch position to game coordinates (640x480)
         * considering the current zoom level and view position */
        
        extern int16_t g_mouse_x, g_mouse_y;
        
        float zoom_factor = 1.0f / (float)(1 << s_zoom_level);
        int view_w = (int)(320 * zoom_factor);
        int view_h = (int)(240 * zoom_factor);
        
        /* Current view center */
        int view_x = g_mouse_x - view_w / 2;
        int view_y = g_mouse_y - view_h / 2;
        
        /* Clamp view to game bounds */
        if (view_x < 0) view_x = 0;
        if (view_y < 0) view_y = 0;
        if (view_x + view_w > 640) view_x = 640 - view_w;
        if (view_y + view_h > 480) view_y = 480 - view_h;
        
        /* Map touch position (0-319, 0-239) to game coordinates */
        float rel_x = (float)touch.px / 320.0f;
        float rel_y = (float)touch.py / 240.0f;
        
        g_mouse_x = (int16_t)(view_x + rel_x * view_w);
        g_mouse_y = (int16_t)(view_y + rel_y * view_h);
        
        /* Clamp to game bounds */
        if (g_mouse_x < 0) g_mouse_x = 0;
        if (g_mouse_x >= 640) g_mouse_x = 639;
        if (g_mouse_y < 0) g_mouse_y = 0;
        if (g_mouse_y >= 480) g_mouse_y = 479;
    }
}

void plat_input_flush(void)
{
    /* Called at startup to discard any queued input so the first button
     * press doesn't immediately skip the intro cutscene */
    hidScanInput();
    g_lmb_clicked = 0;
    g_rmb_clicked = 0;
}

/* Export zoom level for video layer */
int platform_3ds_get_zoom_level(void)
{
    return s_zoom_level;
}
