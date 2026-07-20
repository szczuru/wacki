/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — 3DS button + touch + D-Pad input.
 *
 * BUTTON MAPPING (Nintendo layout - swapped A/B vs Xbox):
 *   Physical A (east)  → left click (KEY_B in ctrulib)
 *   Physical B (south) → right click (KEY_A in ctrulib)
 *   Physical X (north) → cycle zoom level (KEY_Y in ctrulib)
 *   Physical Y (west)  → toggle aspect mode (KEY_X in ctrulib)
 *   START              → pause menu
 *   SELECT             → toggle hand mode (left/right)
 *
 * HAND MODES:
 *   - LEFT_HAND:  L=left click,  ZL=right click,  R=quicksave, ZR=quickload
 *   - RIGHT_HAND: L=quicksave,   ZL=quickload,    R=left click, ZR=right click
 *
 * TOUCH: Bottom screen touch → mouse position (mapped to game coordinates)
 * D-PAD + CIRCLE PAD: Move cursor
 */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/input.h"
#include "wacki/platform/video.h"
#include <3ds.h>
#include <string.h>

#define ANALOG_DEADZONE 20
#define ANALOG_SPEED    5

/* Hand mode toggle */
typedef enum {
    HAND_MODE_LEFT = 0,
    HAND_MODE_RIGHT = 1
} HandMode;

static HandMode s_hand_mode = HAND_MODE_LEFT;

/* Cursor position (exported to video_3ds_gl.c) */
int g_cursor_x = 320;
int g_cursor_y = 240;

/* Touch state tracking */
static int s_touch_active = 0;
static int s_last_touch_x = 0;
static int s_last_touch_y = 0;

/* Previous button state for edge detection */
static u32 s_prev_keys = 0;

extern void platform_video_cycle_zoom(void);

/* Map touch coordinates (320x240) to game coordinates (640x480) */
static void touch_to_game_coords(int tx, int ty, int *gx, int *gy)
{
    /* Bottom screen is 320x240, game is 640x480 */
    *gx = (tx * 640) / 320;
    *gy = (ty * 480) / 240;
}

void platform_pad_open(void)
{
    /* 3DS input initialized in system_3ds.c */
    LOG_INFO("3ds", "Input initialized (hand_mode=left)");
}

void platform_pad_handle_buttons(void)
{
    hidScanInput();
    u32 keys_down = hidKeysDown();
    u32 keys_held = hidKeysHeld();
    u32 keys_up = hidKeysUp();

    /* --- Button presses (edge-triggered) --- */
    
    /* Physical A (KEY_B) = left click */
    if (keys_down & KEY_B) {
        g_lmb_clicked = 1;
    }
    
    /* Physical B (KEY_A) = right click */
    if (keys_down & KEY_A) {
        g_rmb_clicked = 1;
    }
    
    /* Physical X (KEY_Y) = cycle zoom */
    if (keys_down & KEY_Y) {
        platform_video_cycle_zoom();
    }
    
    /* Physical Y (KEY_X) = toggle aspect mode (no-op on 3DS but kept for consistency) */
    if (keys_down & KEY_X) {
        platform_video_toggle_aspect_mode();
    }
    
    /* START = pause menu */
    if (keys_down & KEY_START) {
        g_pause_menu_request = 1;
    }
    
    /* SELECT = toggle hand mode */
    if (keys_down & KEY_SELECT) {
        s_hand_mode = (s_hand_mode == HAND_MODE_LEFT) ? HAND_MODE_RIGHT : HAND_MODE_LEFT;
        LOG_INFO("3ds", "hand_mode=%s", s_hand_mode == HAND_MODE_LEFT ? "left" : "right");
    }
    
    /* --- Shoulder buttons (hand mode dependent) --- */
    if (s_hand_mode == HAND_MODE_LEFT) {
        /* LEFT HAND: L/ZL = clicks, R/ZR = save/load */
        if (keys_down & KEY_L) g_lmb_clicked = 1;
        if (keys_down & KEY_ZL) g_rmb_clicked = 1;
        if (keys_down & KEY_R) g_quicksave_request = 1;
        if (keys_down & KEY_ZR) g_quickload_request = 1;
    } else {
        /* RIGHT HAND: L/ZL = save/load, R/ZR = clicks */
        if (keys_down & KEY_L) g_quicksave_request = 1;
        if (keys_down & KEY_ZL) g_quickload_request = 1;
        if (keys_down & KEY_R) g_lmb_clicked = 1;
        if (keys_down & KEY_ZR) g_rmb_clicked = 1;
    }

    /* --- Touch input --- */
    if (keys_held & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        
        int gx, gy;
        touch_to_game_coords(touch.px, touch.py, &gx, &gy);
        
        /* Update cursor position */
        g_cursor_x = gx;
        g_cursor_y = gy;
        
        /* Touch down = left click */
        if (!s_touch_active) {
            g_lmb_clicked = 1;
            s_touch_active = 1;
        }
        
        s_last_touch_x = touch.px;
        s_last_touch_y = touch.py;
    } else if (s_touch_active) {
        /* Touch released */
        s_touch_active = 0;
    }

    s_prev_keys = keys_held;
}

void platform_pad_read_motion(int *dx, int *dy, float *ax, float *ay)
{
    hidScanInput();
    u32 keys = hidKeysHeld();

    /* D-Pad movement */
    if (keys & KEY_DRIGHT) (*dx)++;
    if (keys & KEY_DLEFT)  (*dx)--;
    if (keys & KEY_DDOWN)  (*dy)++;
    if (keys & KEY_DUP)    (*dy)--;

    /* Circle Pad analog movement */
    circlePosition pos;
    hidCircleRead(&pos);
    
    if (pos.dx > ANALOG_DEADZONE || pos.dx < -ANALOG_DEADZONE) {
        *ax = (float)pos.dx / 156.0f * ANALOG_SPEED;
    }
    if (pos.dy > ANALOG_DEADZONE || pos.dy < -ANALOG_DEADZONE) {
        *ay = -(float)pos.dy / 156.0f * ANALOG_SPEED; /* Invert Y */
    }

    plat_pad_read_extra(ax, ay);
}

int plat_pad_menu_nav(int *up, int *down, int *confirm)
{
    *up = *down = *confirm = 0;
    
    hidScanInput();
    u32 keys_down = hidKeysDown();
    u32 keys_held = hidKeysHeld();
    
    /* D-Pad or Circle Pad for navigation */
    circlePosition pos;
    hidCircleRead(&pos);
    
    int u = (keys_down & KEY_DUP) || (pos.dy > ANALOG_DEADZONE);
    int d = (keys_down & KEY_DDOWN) || (pos.dy < -ANALOG_DEADZONE);
    
    /* Physical A (KEY_B) = confirm */
    int c = (keys_down & KEY_B);
    
    if (u) *up = 1;
    if (d) *down = 1;
    if (c) *confirm = 1;
    
    return 1;
}

void plat_input_flush(void)
{
    hidScanInput();
    g_lmb_clicked = 0;
    g_rmb_clicked = 0;
    g_quicksave_request = 0;
    g_quickload_request = 0;
    g_pause_menu_request = 0;
}

/* Get current mouse position (for engine) */
void platform_input_get_mouse_pos(int *x, int *y)
{
    *x = g_cursor_x;
    *y = g_cursor_y;
}

/* Check if should quit (e.g., HOME button pressed) */
int platform_input_should_quit(void)
{
    return !aptMainLoop();
}
