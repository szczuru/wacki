/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/platform_3ds.c — Nintendo 3DS platform layer (non-SDL). */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include "wacki/platform/input.h"
#include "wacki/platform/system.h"
#include <3ds.h>
#include <stdint.h>
#include <string.h>

#define TYPED_QUEUE_SZ 32

static int     s_w = 0, s_h = 0;
static int     s_quit = 0;
static uint8_t s_typed_q[TYPED_QUEUE_SZ];
static int     s_typed_head = 0, s_typed_tail = 0;

/* ---- typed-char ring (minimal - 3DS has no keyboard) ------------------ */

void PlatformPushTypedChar(uint8_t c)
{
    int next = (s_typed_head + 1) % TYPED_QUEUE_SZ;
    if (next == s_typed_tail) return;
    s_typed_q[s_typed_head] = c;
    s_typed_head = next;
}

uint8_t PlatformPollTypedChar(void)
{
    if (s_typed_head == s_typed_tail) return 0;
    uint8_t c = s_typed_q[s_typed_tail];
    s_typed_tail = (s_typed_tail + 1) % TYPED_QUEUE_SZ;
    return c;
}

void PlatformSetTextInput(int on)
{
    /* 3DS doesn't have software keyboard support in this context */
    (void)on;
    s_typed_head = s_typed_tail = 0;
}

/* ---- init / shutdown -------------------------------------------------- */

int PlatformInit(int w, int h, const char *title)
{
    s_w = w;
    s_h = h;
    
    if (!g_headless) platform_pad_open();
    
    if (g_headless) {
        LOG_INFO("platform", "3DS ready (headless): %dx%d", w, h);
        return 1;
    }
    
    return plat_video_init(w, h, title);
}

void PlatformShutdown(void)
{
    plat_video_shutdown();
}

void PlatformPresent(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (g_headless) return;
    plat_video_present(shadow, pal, w, h);
}

/* ---- event pump ------------------------------------------------------- */

void PlatformPumpEvents(void)
{
    /* 3DS input is polled directly in gamepad_3ds.c, not event-based */
    
    /* Check if we should quit (HOME button) */
    if (!aptMainLoop()) {
        s_quit = 1;
    }
}

int PlatformShouldQuit(void)
{
    return s_quit;
}

/* ---- message box ------------------------------------------------------ */

void PlatformShowMessageBox(const char *title, const char *body)
{
    if (g_headless) {
        LOG_TRACE("msgbox", "%s: %s", title, body);
        return;
    }
    plat_video_message_box(title, body);
}
