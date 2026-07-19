/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/gamepad_3ds.c — 3DS input handling.
 *
 * Features:
 * - A/B buttons swapped (Nintendo layout vs Xbox)
 * - X button: cycle zoom levels (1x/2x/4x/8x)
 * - SELECT: toggle hand mode (left/right handed)
 * - Touch screen: maps to mouse cursor
 * - L/ZL and R/ZR: mouse buttons (depends on hand mode) */

#include "wacki.h"
#include "wacki/input.h"
#include "wacki/log.h"

#include <3ds.h>
#include <string.h>

#define GAME_W 640
#define GAME_H 480
#define BOTTOM_W 320
#define BOTTOM_H 240

/* Zoom levels: 0=1x, 1=2x, 2=4x, 3=8x */
static int s_zoom_level = 0;
static int s_hand_mode = 0; /* 0=right-handed, 1=left-handed */

/* Mouse state */
int16_t g_mouse_x = GAME_W / 2;
int16_t g_mouse_y = GAME_H / 2;

int platform_3ds_get_zoom_level(void)
{
    return s_zoom_level;
}

void plat_gamepad_init(void)
{
    hidInit();
    LOG_INFO("3ds-input", "Input initialized (A/B swapped, zoom: X, hand mode: SELECT)");
}

void plat_gamepad_shutdown(void)
{
    hidExit();
}

static void handle_dpad(uint32_t held)
{
    int dx = 0, dy = 0;
    
    if (held & KEY_DUP)    dy -= 4;
    if (held & KEY_DDOWN)  dy += 4;
    if (held & KEY_DLEFT)  dx -= 4;
    if (held & KEY_DRIGHT) dx += 4;
    
    g_mouse_x += dx;
    g_mouse_y += dy;
    
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= GAME_W) g_mouse_x = GAME_W - 1;
    if (g_mouse_y >= GAME_H) g_mouse_y = GAME_H - 1;
    
    if (dx || dy) {
        input_set_mouse_pos(g_mouse_x, g_mouse_y);
    }
}

static void handle_touch(touchPosition *touch)
{
    if (!touch) return;
    
    int zoom = 1 << s_zoom_level;
    float zoom_factor = 1.0f / zoom;
    
    int view_w = (int)(BOTTOM_W * zoom_factor);
    int view_h = (int)(BOTTOM_H * zoom_factor);
    
    int view_x = g_mouse_x - view_w / 2;
    int view_y = g_mouse_y - view_h / 2;
    
    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;
    if (view_x + view_w > GAME_W) view_x = GAME_W - view_w;
    if (view_y + view_h > GAME_H) view_y = GAME_H - view_h;
    
    float rel_x = (float)touch->px / BOTTOM_W;
    float rel_y = (float)touch->py / BOTTOM_H;
    
    g_mouse_x = view_x + (int)(rel_x * view_w);
    g_mouse_y = view_y + (int)(rel_y * view_h);
    
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= GAME_W) g_mouse_x = GAME_W - 1;
    if (g_mouse_y >= GAME_H) g_mouse_y = GAME_H - 1;
    
    input_set_mouse_pos(g_mouse_x, g_mouse_y);
}

void plat_gamepad_poll(void)
{
    hidScanInput();
    
    uint32_t down = hidKeysDown();
    uint32_t up = hidKeysUp();
    uint32_t held = hidKeysHeld();
    
    /* Exit on START */
    if (down & KEY_START) {
        input_set_key(KEY_ESCAPE, true);
    }
    if (up & KEY_START) {
        input_set_key(KEY_ESCAPE, false);
    }
    
    /* A/B swapped (Nintendo vs Xbox layout) */
    if (down & KEY_B) input_set_key(KEY_RETURN, true);  /* B = confirm */
    if (up & KEY_B)   input_set_key(KEY_RETURN, false);
    
    if (down & KEY_A) input_set_key(KEY_ESCAPE, true);  /* A = cancel */
    if (up & KEY_A)   input_set_key(KEY_ESCAPE, false);
    
    /* X button: cycle zoom */
    if (down & KEY_Y) {  /* Y in SDL terms = X on 3DS */
        s_zoom_level = (s_zoom_level + 1) % 4;
        LOG_INFO("3ds-input", "Zoom level: %dx", 1 << s_zoom_level);
    }
    
    /* SELECT: toggle hand mode */
    if (down & KEY_SELECT) {
        s_hand_mode = !s_hand_mode;
        LOG_INFO("3ds-input", "Hand mode: %s",
                 s_hand_mode ? "LEFT" : "RIGHT");
    }
    
    /* Mouse buttons based on hand mode */
    if (s_hand_mode == 0) {
        /* Right-handed: L/ZL = left click, R/ZR = right click */
        if (down & (KEY_L | KEY_ZL)) input_set_mouse_button(MOUSE_LEFT, true);
        if (up & (KEY_L | KEY_ZL))   input_set_mouse_button(MOUSE_LEFT, false);
        
        if (down & (KEY_R | KEY_ZR)) input_set_mouse_button(MOUSE_RIGHT, true);
        if (up & (KEY_R | KEY_ZR))   input_set_mouse_button(MOUSE_RIGHT, false);
    } else {
        /* Left-handed: R/ZR = left click, L/ZL = right click */
        if (down & (KEY_R | KEY_ZR)) input_set_mouse_button(MOUSE_LEFT, true);
        if (up & (KEY_R | KEY_ZR))   input_set_mouse_button(MOUSE_LEFT, false);
        
        if (down & (KEY_L | KEY_ZL)) input_set_mouse_button(MOUSE_RIGHT, true);
        if (up & (KEY_L | KEY_ZL))   input_set_mouse_button(MOUSE_RIGHT, false);
    }
    
    /* D-pad: move cursor */
    handle_dpad(held);
    
    /* Touch screen */
    touchPosition touch;
    if (hidKeysHeld() & KEY_TOUCH) {
        hidTouchRead(&touch);
        handle_touch(&touch);
        input_set_mouse_button(MOUSE_LEFT, true);
    } else {
        if (hidKeysUp() & KEY_TOUCH) {
            input_set_mouse_button(MOUSE_LEFT, false);
        }
    }
}
