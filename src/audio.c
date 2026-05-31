/* audio.c — SDL audio mixer + music/sfx/dialog dispatch.
 *
 * The cutscene playback shim (PlaySceneCutsceneAvi + InitializeDirect
 * Sound) lives in src/audio/cutscene.c — it's the AVI/FLIC entry point
 * and has no direct dependency on the mixer below.
 *
 * What stays here (for now):
 *   - The SDL_AudioDevice mixer core + per-channel WAV loader
 *   - Music API   (PlayMenuMusic / StopMenuMusic / TickMenuMusic)
 *   - SFX dispatch (Wacky.scr [sampl] parser + TriggerFrameSfx +
 *                   PlaySfx / PlaySfxLoopAndGetChannel / ...)
 *   - Dialog line (PlayDialogLine)
 *   - The per-flag gates (g_audio_music_enabled, _sfx_enabled, etc.)
 *
 * Pulled-apart split into src/audio/{mixer,music,sfx}.c is feasible
 * but blocked on exposing the s_mix array + helpers across TUs. */

#include "wacki.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- *
 * Audio mixer (T6).
 *
 * One SDL audio device with a callback that mixes N channels into stereo
 * S16 22050 Hz output. Each channel owns a pre-converted PCM buffer
 * (SDL_BuildAudioCVT + SDL_ConvertAudio at load time) and a play
 * position. Channels can loop (music) or one-shot (SFX, dialog).
 *
 * Replaces the earlier hack of opening a separate SDL_AudioDevice per
 * SFX slot (4 devices) + a music device (5 total) — wasteful, prone
 * to format mismatch surprises. Mixer fixes both: single device,
 * uniform output spec, all streams normalized at load time.
 *
 * Public API preserved (PlaySfx, PlayMenuMusic, StopMenuMusic,
 * TickMenuMusic, TickSfx all keep their signatures + semantics).
 * New: PlayDialogLine — dedicated channel for per-line dialog audio.
 * ------------------------------------------------------------------------- */
/* MIX_CHANNEL_COUNT, MIX_CHAN_*, MixChannel, s_mix[], and s_mix_dev
 * are declared in audio/mixer_internal.h so src/audio/sfx.c can read
 * the channel array for its replay-guard check. */
#include "audio/mixer_internal.h"

#define MIX_OUT_FREQ          22050
#define MIX_OUT_CHANS         2          /* stereo for max compatibility */
#define MIX_OUT_FORMAT        AUDIO_S16SYS
#define MIX_OUT_SAMPLE_BYTES  (2 * MIX_OUT_CHANS)   /* S16 stereo = 4 bytes */
#define MIX_GAIN_IDENTITY     128        /* per-channel gain: 128 = unity */
#define MIX_OPEN_SAMPLES      2048       /* device buffer in frames */

SDL_AudioDeviceID s_mix_dev = 0;
static SDL_AudioSpec     s_mix_spec;
struct MixChannel s_mix[MIX_CHANNEL_COUNT];

/* Audio callback — fires on SDL's audio thread. Mix all active channels
 * into the output buffer with saturation clipping. */
static void mixer_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    /* Start with silence. */
    SDL_memset(stream, 0, (size_t)len);
    int16_t *out       = (int16_t *)stream;
    int      n_samples = len / 2;       /* S16 = 2 bytes per sample slot */
    /* Each output frame is MIX_OUT_CHANS sample slots side-by-side. */
    int      n_frames  = n_samples / MIX_OUT_CHANS;

    for (int c = 0; c < MIX_CHANNEL_COUNT; ++c) {
        struct MixChannel *ch = &s_mix[c];
        if (!ch->active || !ch->buf || ch->len == 0) continue;
        int16_t *src           = (int16_t *)ch->buf;
        Uint32   src_frame     = ch->pos / MIX_OUT_SAMPLE_BYTES;
        Uint32   src_frame_end = ch->len / MIX_OUT_SAMPLE_BYTES;
        /* T36: per-channel gain. MIX_GAIN_IDENTITY = no attenuation;
         * anything else multiplies each sample. Cast to int so the
         * intermediate (src * gain) doesn't overflow at gain=255. */
        int      gain_l        = ch->gain_l;
        int      gain_r        = ch->gain_r;
        for (int f = 0; f < n_frames; ++f) {
            if (src_frame >= src_frame_end) {
                if (ch->loop) {
                    src_frame = 0;
                } else {
                    ch->active = 0;
                    break;
                }
            }
            int16_t sl = src[src_frame * 2 + 0];
            int16_t sr = src[src_frame * 2 + 1];
            ++src_frame;
            int ml = (int)out[f * 2 + 0] + (sl * gain_l) / MIX_GAIN_IDENTITY;
            int mr = (int)out[f * 2 + 1] + (sr * gain_r) / MIX_GAIN_IDENTITY;
            if (ml >  32767) ml =  32767;
            if (ml < -32768) ml = -32768;
            if (mr >  32767) mr =  32767;
            if (mr < -32768) mr = -32768;
            out[f * 2 + 0] = (int16_t)ml;
            out[f * 2 + 1] = (int16_t)mr;
        }
        ch->pos = src_frame * MIX_OUT_SAMPLE_BYTES;
    }
}

/* Ensure the mixer device + spec is open. Idempotent. */
int mixer_ensure_open(void)
{
    if (s_mix_dev) return 1;
    SDL_AudioSpec want;
    SDL_memset(&want, 0, sizeof want);
    want.freq     = MIX_OUT_FREQ;
    want.format   = MIX_OUT_FORMAT;
    want.channels = MIX_OUT_CHANS;
    want.samples  = MIX_OPEN_SAMPLES;
    want.callback = mixer_callback;
    want.userdata = NULL;
    s_mix_dev = SDL_OpenAudioDevice(NULL, 0, &want, &s_mix_spec, 0);
    if (!s_mix_dev) {
        fprintf(stderr, "[mixer] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(s_mix_dev, 0);   /* unpause */
    fprintf(stderr, "[mixer] open: %d Hz, %d ch, %d-bit (8 channels)\n",
            s_mix_spec.freq, s_mix_spec.channels,
            SDL_AUDIO_BITSIZE(s_mix_spec.format));
    return 1;
}

/* Load a WAV (from filesystem or DTA) and convert to output spec.
 * Returns 1 on success with out_buf/out_len owned by caller (must SDL_free
 * after audio callback no longer references it). */
int mixer_load_wav(const char *name, Uint8 **out_buf, Uint32 *out_len);

/* Lock device, assign to channel `idx`, unlock, unpause. Frees previous
 * buf if any. */
void mixer_assign(int idx, Uint8 *buf, Uint32 len, int loop,
                         const char *name)
{
    if (idx < 0 || idx >= MIX_CHANNEL_COUNT) return;
    SDL_LockAudioDevice(s_mix_dev);
    if (s_mix[idx].buf) SDL_free(s_mix[idx].buf);
    s_mix[idx].buf    = buf;
    s_mix[idx].len    = len;
    s_mix[idx].pos    = 0;
    s_mix[idx].loop   = loop;
    s_mix[idx].active = (buf && len > 0) ? 1 : 0;
    /* Default to identity gain (no pan). Callers can override after
     * the channel is loaded. */
    s_mix[idx].gain_l = MIX_GAIN_IDENTITY;
    s_mix[idx].gain_r = MIX_GAIN_IDENTITY;
    extern uint32_t g_tick_counter;
    s_mix[idx].start_tick = g_tick_counter;
    if (name) {
        size_t n = strlen(name);
        if (n >= sizeof s_mix[idx].name) n = sizeof s_mix[idx].name - 1;
        memcpy(s_mix[idx].name, name, n);
        s_mix[idx].name[n] = 0;
    } else {
        s_mix[idx].name[0] = 0;
    }
    SDL_UnlockAudioDevice(s_mix_dev);
}

void mixer_stop_channel(int idx)
{
    if (idx < 0 || idx >= MIX_CHANNEL_COUNT) return;
    SDL_LockAudioDevice(s_mix_dev);
    s_mix[idx].active = 0;
    if (s_mix[idx].buf) {
        SDL_free(s_mix[idx].buf);
        s_mix[idx].buf = NULL;
    }
    s_mix[idx].len = 0;
    s_mix[idx].pos = 0;
    s_mix[idx].name[0] = 0;
    SDL_UnlockAudioDevice(s_mix_dev);
}

/* ------------------------------------------------------------------------- *
 * Menu / background WAV music — backed by mixer channel MIX_CHAN_MUSIC.
 * ------------------------------------------------------------------------- */

static int try_load_wav_at(const char *root, const char *name,
                           SDL_AudioSpec *out_spec, Uint8 **out_buf,
                           Uint32 *out_len)
{
    char p[1024];
    if (root && *root) snprintf(p, sizeof p, "%s/%s", root, name);
    else               snprintf(p, sizeof p, "%s", name);
    if (SDL_LoadWAV(p, out_spec, out_buf, out_len))
        return 1;
    /* upper-case the basename (CD layout on macOS often) */
    size_t l = strlen(p);
    size_t i = l;
    while (i > 0 && p[i-1] != '/' && p[i-1] != '\\') --i;
    for (size_t j = i; j < l; ++j)
        if (p[j] >= 'a' && p[j] <= 'z') p[j] &= 0xDF;
    return SDL_LoadWAV(p, out_spec, out_buf, out_len) != NULL;
}

/* Try to load the WAV as an entry inside the currently mounted .dta
 * archive (wybuch.wav, items.wav, etc. live there — not on disk). */
static int try_load_wav_from_dta(const char *name, SDL_AudioSpec *out_spec,
                                 Uint8 **out_buf, Uint32 *out_len)
{
    void    *raw = NULL;
    uint32_t sz  = 0;
    if (!LoadFileFromDta(name, &raw, &sz) || !raw || sz < 12) return 0;
    const uint8_t *p = (const uint8_t *)raw;
    if (!(p[0]=='R' && p[1]=='I' && p[2]=='F' && p[3]=='F' &&
          p[8]=='W' && p[9]=='A' && p[10]=='V' && p[11]=='E')) {
        xfree(raw);
        return 0;
    }
    SDL_RWops *rw = SDL_RWFromConstMem(raw, (int)sz);
    int ok = SDL_LoadWAV_RW(rw, 1, out_spec, out_buf, out_len) != NULL;
    xfree(raw);
    return ok;
}

/* mixer_load_wav — combined disk + DTA WAV load, then convert to mixer
 * output spec (S16 stereo 22050 Hz). Returns 1 + sets *out_buf (owned by
 * caller, free via SDL_free) + *out_len. */
int mixer_load_wav(const char *name, Uint8 **out_buf, Uint32 *out_len)
{
    SDL_AudioSpec native;
    Uint8 *native_buf = NULL;
    Uint32 native_len = 0;

    int ok = 0;
    if (!ok) ok = try_load_wav_at(NULL,      name, &native, &native_buf, &native_len);
    if (!ok) ok = try_load_wav_at(g_data_root, name, &native, &native_buf, &native_len);
    if (!ok) ok = try_load_wav_at("./data",  name, &native, &native_buf, &native_len);
    if (!ok) ok = try_load_wav_from_dta(name, &native, &native_buf, &native_len);
    if (!ok) {
        *out_buf = NULL; *out_len = 0;
        return 0;
    }

    /* Already in target format? Just transfer ownership. */
    if (native.freq == MIX_OUT_FREQ &&
        native.format == MIX_OUT_FORMAT &&
        native.channels == MIX_OUT_CHANS) {
        /* Take a freshly malloc'd copy so caller can SDL_free it
         * (WAV buffer must use SDL_FreeWAV; can't mix allocators). */
        Uint8 *copy = (Uint8 *)SDL_malloc(native_len);
        if (!copy) {
            SDL_FreeWAV(native_buf);
            *out_buf = NULL; *out_len = 0;
            return 0;
        }
        SDL_memcpy(copy, native_buf, native_len);
        SDL_FreeWAV(native_buf);
        *out_buf = copy;
        *out_len = native_len;
        return 1;
    }

    /* Build CVT and convert in-place via SDL_ConvertAudio. */
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, native.format, native.channels, native.freq,
                          MIX_OUT_FORMAT, MIX_OUT_CHANS, MIX_OUT_FREQ) < 0) {
        fprintf(stderr, "[mixer] SDL_BuildAudioCVT failed for %s: %s\n",
                name, SDL_GetError());
        SDL_FreeWAV(native_buf);
        return 0;
    }
    /* CVT needs a buffer sized native_len * cvt.len_mult. Allocate
     * fresh via SDL_malloc (so SDL_free can release it), copy native
     * into it. */
    Uint32 buf_size = native_len * (cvt.len_mult ? cvt.len_mult : 1);
    Uint8 *buf = (Uint8 *)SDL_malloc(buf_size);
    if (!buf) {
        SDL_FreeWAV(native_buf);
        return 0;
    }
    SDL_memcpy(buf, native_buf, native_len);
    SDL_FreeWAV(native_buf);
    cvt.buf = buf;
    cvt.len = (int)native_len;
    if (SDL_ConvertAudio(&cvt) != 0) {
        fprintf(stderr, "[mixer] SDL_ConvertAudio failed for %s: %s\n",
                name, SDL_GetError());
        SDL_free(buf);
        return 0;
    }
    *out_buf = buf;
    *out_len = (Uint32)cvt.len_cvt;
    return 1;
}

/* Options-menu toggles — wired by Solund handler in game.c. Defaults to
 * enabled. When music is toggled off mid-play, we stop the channel and
 * remember the last track so the toggle-back-on path can resume it. */
/* T103 fix — flag mapping inferred from LoadSaveStateOrInitialize:
 *   speech_y_offset    → g_audio_sfx_enabled
 *   speech_text_attr   → g_audio_music_enabled
 *   speech_color_index → g_audio_sound_enabled (legacy master mute)
 *   fade_target        → g_audio_voice_enabled
 *
 * g_audio_sound_enabled is a hold-over master mute (the field was once
 * mis-mapped to "sfx" via fade_progress, which actually drives
 * dialogues_on). Kept so existing call sites continue to compile. */
int      g_audio_music_enabled = 1;       /* speech_text_attr mirror */
int      g_audio_sfx_enabled   = 1;       /* speech_y_offset mirror — sfx */
int      g_audio_voice_enabled = 1;       /* fade_target mirror — dialog audio */
int      g_audio_sound_enabled = 1;       /* speech_color_index mirror (legacy global) */
static char  s_last_music_name[64] = "";
static int   s_last_music_loop    = 0;

void StopMenuMusic(void)
{
    if (s_mix_dev) mixer_stop_channel(MIX_CHAN_MUSIC);
}

void PlayMenuMusic(const char *dta_name, int loop)
{
    if (!dta_name) {
        StopMenuMusic();
        s_last_music_name[0] = 0;
        s_last_music_loop = 0;
        return;
    }
    /* Remember the last requested track so AudioSetMusicEnabled(1)
     * can resume play after a mute-toggle. */
    snprintf(s_last_music_name, sizeof s_last_music_name, "%s", dta_name);
    s_last_music_loop = loop ? 1 : 0;

    if (!g_audio_music_enabled || !g_audio_sound_enabled) {
        StopMenuMusic();
        return;
    }
    if (!mixer_ensure_open()) return;
    StopMenuMusic();

    Uint8 *buf = NULL; Uint32 len = 0;
    if (!mixer_load_wav(dta_name, &buf, &len)) {
        fprintf(stderr, "[music] cannot find/decode %s as WAV\n", dta_name);
        return;
    }
    mixer_assign(MIX_CHAN_MUSIC, buf, len, loop ? 1 : 0, dta_name);
    fprintf(stderr, "[music] %s playing: %u bytes converted (loop=%d) on mixer ch %d\n",
            dta_name, len, loop ? 1 : 0, MIX_CHAN_MUSIC);
}

/* Toggle hook — called by the Solund options handler when the user
 * clicks the music on/off button. If muting mid-play, stops the
 * channel. If un-muting, re-issues PlayMenuMusic with the last
 * remembered track to resume. */
void AudioSetMusicEnabled(int on)
{
    int was = g_audio_music_enabled;
    g_audio_music_enabled = on ? 1 : 0;
    if (was && !on) {
        StopMenuMusic();
    } else if (!was && on && s_last_music_name[0]) {
        PlayMenuMusic(s_last_music_name, s_last_music_loop);
    }
}

void AudioSetSfxEnabled(int on)
{
    g_audio_sfx_enabled = on ? 1 : 0;
}

/* T103 — voice toggle. Gates PlayDialogLine via g_audio_voice_enabled
 * check at the head. If toggled off mid-line, stops the dialog channel
 * immediately so the user-facing effect is instant. */
void AudioSetVoiceEnabled(int on)
{
    int was = g_audio_voice_enabled;
    g_audio_voice_enabled = on ? 1 : 0;
    if (was && !on && s_mix_dev) mixer_stop_channel(MIX_CHAN_DIALOG);
}

/* Global sound mute — kills both music + SFX while clear,
 * resumes music when set back on. */
void AudioSetSoundEnabled(int on)
{
    int was = g_audio_sound_enabled;
    g_audio_sound_enabled = on ? 1 : 0;
    if (was && !on) {
        StopMenuMusic();
    } else if (!was && on && s_last_music_name[0] && g_audio_music_enabled) {
        PlayMenuMusic(s_last_music_name, s_last_music_loop);
    }
}

void TickMenuMusic(void)
{
    /* Mixer callback handles loop natively — no per-frame top-up
     * needed. Kept as a no-op for API compat (called from
     * play_demo_scene frame loop). */
}


/* PlayDialogLine — T6: dedicated dialog speech channel.
 * Uses MIX_CHAN_DIALOG (reserved). Returns the byte length of the
 * loaded audio (so caller can compute approximate duration) or 0 on
 * failure. */
uint32_t PlayDialogLine(const char *wav_name)
{
    if (!wav_name) return 0;
    /* T103 — voice gate: if the user disabled voice in Solund, drop
     * the sample silently. Caller still proceeds with text /
     * animation. */
    if (!g_audio_voice_enabled || !g_audio_sound_enabled) return 0;
    if (!mixer_ensure_open()) return 0;
    Uint8 *buf = NULL; Uint32 len = 0;
    if (!mixer_load_wav(wav_name, &buf, &len)) {
        fprintf(stderr, "[dialog] cannot load '%s'\n", wav_name);
        return 0;
    }
    mixer_assign(MIX_CHAN_DIALOG, buf, len, 0, wav_name);
    fprintf(stderr, "[dialog] play '%s' on mixer ch %d (%u bytes)\n",
            wav_name, MIX_CHAN_DIALOG, len);
    return len;
}

/* StopDialogLine — used to cancel mid-line (e.g. user click-to-skip). */
void StopDialogLine(void)
{
    if (!s_mix_dev) return;
    mixer_stop_channel(MIX_CHAN_DIALOG);
}

/* IsDialogLinePlaying — for lip-sync polling. */
int IsDialogLinePlaying(void)
{
    return s_mix_dev && s_mix[MIX_CHAN_DIALOG].active;
}

/* TickSfx — collect drained channels. Callback already sets active=0
 * when channel finishes; just free its buf here. Avoids touching shared
 * state during audio callback. */
void TickSfx(void)
{
    if (!s_mix_dev) return;
    for (int i = MIX_CHAN_SFX_START; i < MIX_CHANNEL_COUNT; ++i) {
        SDL_LockAudioDevice(s_mix_dev);
        if (!s_mix[i].active && s_mix[i].buf) {
            SDL_free(s_mix[i].buf);
            s_mix[i].buf = NULL;
            s_mix[i].len = 0;
        }
        SDL_UnlockAudioDevice(s_mix_dev);
    }
}
