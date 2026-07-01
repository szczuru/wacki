/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/audio_3ds.c — Nintendo 3DS audio backend using ndsp.
 *
 * 3DS uses ndsp (DSP audio service) instead of SDL_mixer. This provides
 * a simplified audio implementation for the game. */

#include "wacki.h"
#include "wacki/log.h"
#include <3ds.h>
#include <string.h>

/* Audio subsystem state */
static int s_audio_initialized = 0;

/* Initialize audio subsystem */
int AudioInit(void)
{
    if (s_audio_initialized) return 1;
    
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        LOG_INFO("audio", "ndspInit failed: 0x%08lX", rc);
        return 0;
    }
    
    /* Set default output mode (stereo) */
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    
    s_audio_initialized = 1;
    LOG_INFO("audio", "3DS audio initialized via ndsp");
    return 1;
}

/* Shutdown audio subsystem */
void AudioShutdown(void)
{
    if (!s_audio_initialized) return;
    
    ndspExit();
    s_audio_initialized = 0;
    LOG_INFO("audio", "3DS audio shutdown");
}

/* Play a sound effect - stub for now */
void AudioPlaySfx(const char *name, int volume)
{
    (void)name;
    (void)volume;
    /* TODO: Implement sound effect playback using ndsp channels */
}

/* Play background music - stub for now */
void AudioPlayMusic(const char *name, int loops)
{
    (void)name;
    (void)loops;
    /* TODO: Implement music streaming using ndsp */
}

/* Stop all audio */
void AudioStopAll(void)
{
    if (!s_audio_initialized) return;
    /* TODO: Stop all active ndsp channels */
}

/* Set master volume */
void AudioSetVolume(int volume)
{
    (void)volume;
    /* TODO: Implement volume control via ndsp */
}
