/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/audio/music_stream.h — streaming background music.
 *
 * The main-menu BGM is a large RIFF/WAVE (~25 MB) already at the mixer's
 * output format. Loading it whole is fine on a desktop but blows the RAM
 * budget on a tight handheld, so big files are STREAMED through a small
 * looping ring on MIX_CHAN_MUSIC, topped up per-frame from the open file.
 * The ring playback itself needs no special mixer support — it's just a
 * looping channel whose bytes are refreshed behind the play head.
 *
 * NOT a public engine header — only the audio module (audio.c) uses this. */

#ifndef WACKI_AUDIO_MUSIC_STREAM_H
#define WACKI_AUDIO_MUSIC_STREAM_H

/* If `name` is a big, format-matching standalone WAV, set MIX_CHAN_MUSIC to
 * stream it as a looping ring and return 1. Returns 0 (caller should load
 * the file whole) when it's missing, small, or not already at output
 * format. */
int  music_stream_try_open(const char *name, int loop);

/* Per-frame producer: refill the already-played region of the ring from the
 * file, looping at end of PCM data. No-op when no stream is active. Call
 * once per frame (from TickMenuMusic, on the game thread). */
void music_stream_refill(void);

/* Tear down any active stream (closes the file, drops the cursors). The
 * MIX_CHAN_MUSIC channel — which owns the ring buffer — is stopped
 * separately by the caller. */
void music_stream_stop(void);

#endif /* WACKI_AUDIO_MUSIC_STREAM_H */
