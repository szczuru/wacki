/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — Nintendo 3DS input handling.
 *
 * 3DS uses its own gamepad file with custom button mapping:
 *
 * Nintendo's physical buttons (3DS layout):
 *   physical A (right)  → left click
 *   physical B (bottom) → right click
 *   physical X (top)    → cycle zoom level on bottom screen
 *   physical Y (left)   → (reserved for future use)
 *   START               → pause menu
 *   SELECT              → toggle left/right-hand mode
 *   
 * L/ZL and R/ZR shoulder buttons:
 *   In RIGHT-HAND mode (default):
 *     L  → quickload
 *     ZL → left click (alternative)
 *     R  → quicksave
 *     ZR → right click (alternative)
 *   
 *   In LEFT-HAND mode:
 *     L  → left click
 *     ZL → right click
 *     R  → quicksave
 *     ZR → quickload
 *
 * Circle Pad → cursor movement
 * Touch Screen → cursor position + zoom view on bottom screen */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include <3ds.h>
#include <string.h>

#define PAD_ANALOG_MAX_PX   9
#define PAD_ANALOG_DEADZONE 20  /* 3DS circle pad deadzone */

/* Zoom levels for bottom screen (percentage of top screen area shown) */
static int s_zoom_level = 1;  /* 0=100%, 1=50%, 2=25%, 3=12.5% */
#define MAX_ZOOM_LEVEL 3

/* Hand mode: 0 = right-hand (default), 1 = left-hand */
static int s_hand_mode = 0;

static u32 s_prev_keys = 0;

void platform_pad_open(void)
{
    /* 3DS input initialized in system_3ds.c via hidInit() */
    LOG_INFO("platform", "3DS gamepad initialized");
}

int platform_pad_handle_event(void *ev)
{
    /* 3DS doesn't use SDL events - we poll directly in platform_pad_read_motion */
    (void)ev;
    return 0;
}

void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    
    /* Button press events (edge-triggered) */
    if (kDown & KEY_START) {
        g_pause_menu_request = 1;
    }
    
    /* SELECT toggles left/right-hand mode */
    if (kDown & KEY_SELECT) {
        s_hand_mode = !s_hand_mode;
        LOG_INFO("input", "Hand mode: %s", s_hand_mode ? "LEFT" : "RIGHT");
    }
    
    /* X button cycles zoom level */
    if (kDown & KEY_X) {
        s_zoom_level = (s_zoom_level + 1) % (MAX_ZOOM_LEVEL + 1);
        LOG_INFO("input", "Zoom level: %d", s_zoom_level);
    }
    
    /* Face buttons: A/B swapped like Switch */
    if (kDown & KEY_A) {  /* physical A (right position) */
        g_lmb_clicked = 1;
    }
    if (kDown & KEY_B) {  /* physical B (bottom position) */
        g_rmb_clicked = 1;
    }
    
    /* Shoulder buttons - depends on hand mode */
    if (s_hand_mode == 0) {
        /* RIGHT-HAND mode */
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
    
    /* D-Pad for discrete cursor movement */
    if (kHeld & KEY_DRIGHT) (*dx)++;
    if (kHeld & KEY_DLEFT)  (*dx)--;
    if (kHeld & KEY_DDOWN)  (*dy)++;
    if (kHeld & KEY_DUP)    (*dy)--;
    
    /* Circle Pad for analog cursor movement */
    circlePosition pos;
    hidCircleRead(&pos);
    
    if (pos.dx > PAD_ANALOG_DEADZONE || pos.dx < -PAD_ANALOG_DEADZONE) {
        *ax = (float)pos.dx / 156.0f * PAD_ANALOG_MAX_PX;  /* 3DS circle pad range is -156 to 156 */
    }
    if (pos.dy > PAD_ANALOG_DEADZONE || pos.dy < -PAD_ANALOG_DEADZONE) {
        *ay = -(float)pos.dy / 156.0f * PAD_ANALOG_MAX_PX;  /* Invert Y for natural control */
    }
    
    s_prev_keys = kHeld;
}

int plat_pad_menu_nav(int *up, int *down, int *confirm)
{
    *up = *down = *confirm = 0;
    
    hidScanInput();
    u32 kDown = hidKeysDown();
    
    if (kDown & KEY_DUP)   *up = 1;
    if (kDown & KEY_DDOWN) *down = 1;
    if (kDown & KEY_A)     *confirm = 1;  /* A confirms in menus */
    
    return 1;  /* 3DS always has input available */
}

void plat_input_flush(void)
{
    hidScanInput();
    g_lmb_clicked = 0;
    g_rmb_clicked = 0;
}

/* Export zoom level for video layer */
int platform_3ds_get_zoom_level(void)
{
    return s_zoom_level;
}
