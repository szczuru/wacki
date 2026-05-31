/* src/audio/sfx.c — frame-trigger SFX dispatch + Wacky.scr [sampl] parser.
 *
 * Drives the per-frame sound-trigger system. Every per-entity script
 * call to op 0x12 ADVANCE_FRAME hits the asset's [sampl] table (parsed
 * from Wacky.scr by ParseSamplTagsForKomnata at LoadKomnata time); if a
 * match is found, TriggerFrameSfx fires the WAV through the shared
 * mixer (channels MIX_CHAN_SFX_START..MIX_CHANNEL_COUNT).
 *
 * Per-(asset, frame, wav) state lives in g_sfx_state[], mirroring the
 * original engine's SampleTable layout (playing_flag + currently-
 * playing-channel). The replay guard at the bottom of TriggerFrameSfx
 * skips re-firing a WAV that's both sticky-playing AND still active on
 * its mixer channel — matching the original's `byte+2 == 0 || byte+3 ==
 * 0` semantics.
 *
 * PlaySfx / PlaySfxPanned / PlaySfxAndGetChannel / PlaySfxLoopAndGet
 * Channel are the fire-and-forget public API used by the SoundQueue
 * (sound_queue.c) and other audio callers. */

#include "wacki.h"
#include "mixer_internal.h"

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int g_audio_sfx_enabled;
extern int g_audio_sound_enabled;
extern uint32_t g_tick_counter;

/* ------------------------------------------------------------------------- *
 * PlaySfx — fire-and-forget WAV playback channel.
 *
 * Used by the per-frame sound-trigger system: every per-entity script
 * call to op 0x12 ADVANCE_FRAME consults the asset's [sampl] table
 * (parsed from Wacky.scr) for the just-entered frame; if a match is
 * found, the sound name fires here. Plays in parallel with the room
 * music (separate SDL audio device). Auto-closes when the queue
 * drains.
 *
 * (T6 refactor: SFX now share the mixer channel pool. Legacy per-slot
 * SDL device + WAV-format-per-slot logic removed — see mixer_assign +
 * MIX_CHAN_SFX_START at top of file.) */

void PlaySfx(const char *wav_name);   /* fwd decl */

/* ------------------------------------------------------------------------- *
 * Per-asset frame-trigger sound table —scr `[sampl]` tags.
 *
 * The original engine stores these per asset by parsing Wacky.scr at
 * LoadAssetFromDtaBase time ( → FindAnimationScript) and
 * then per ADVANCE_FRAME the walker calls → 
 * which looks up the current frame and plays the matching WAV via
 * DirectSound.
 *
 * Our port doesn't yet parse `[sampl]` text — hard-code the stage-1
 * triggers extracted from `/tmp/dta-list/wacky.scr`. If the asset
 * lookup hits, we PlaySfx that wav. Each entry: (asset_basename,
 * frame_index, wav_filename). Multiple entries per asset allowed.
 *
 * Source: wacky.scr lines 27-49 (stage-1 maluch.pic [komnata]). */
/* frame_end: 0xFFFF for `(N,)` (single-shot trigger, no looping); a real
 * frame index for `(N,M)` (loop wav from frame_start, stop at frame_end).
 * FrameSfxEntry (start, end) ushort pair stored by
 * — at parse, M-absent becomes sVar11=-1=0xFFFF. */
struct FrameSfxEntry { const char *asset; int frame_start; int frame_end; const char *wav; };

/* Dynamic runtime table populated by ParseSamplTagsForKomnata from the
 * current [komnata]N section of Wacky.scr. Replaces the hand-transcribed
 * table from earlier port — proper
 * tag parser. Each entry: {asset (heap str), frame, wav (heap str)}. */
#define DYNAMIC_SFX_MAX 256
static struct FrameSfxEntry g_dynamic_sfx[DYNAMIC_SFX_MAX];
static int  g_dynamic_sfx_count = 0;
static char g_dynamic_sfx_strpool[8192];
static int  g_dynamic_sfx_strpool_used = 0;

static const char *strpool_intern(const char *s, size_t n)
{
    /* Dedup — return existing entry if matched (= literal pointer
 * equality for stable TriggerFrameSfx debounce keying). */
    int i = 0;
    while (i < g_dynamic_sfx_strpool_used) {
        const char *cur = g_dynamic_sfx_strpool + i;
        size_t cur_n = strlen(cur);
        if (cur_n == n && memcmp(cur, s, n) == 0) return cur;
        i += (int)cur_n + 1;
    }
    if (g_dynamic_sfx_strpool_used + (int)n + 1 > (int)sizeof g_dynamic_sfx_strpool)
        return NULL;
    char *dst = g_dynamic_sfx_strpool + g_dynamic_sfx_strpool_used;
    memcpy(dst, s, n);
    dst[n] = 0;
    g_dynamic_sfx_strpool_used += (int)n + 1;
    return dst;
}

void ResetDynamicSfxTable(void)
{
    g_dynamic_sfx_count = 0;
    g_dynamic_sfx_strpool_used = 0;
}

/* ParseSamplTagsForKomnata — port of 's [sampl] tag parser.
 *
 * Walks a buffer between `start` and `end` (current Wacky.scr [komnata]
 * section per FindScriptByStageAndRoom). Tracks current [animacja] asset
 * name; for each [sampl] line, parses `WAV1 WAV2 .. (F1,M1) (F2,M2) ...`
 * and registers (asset, frame, wav) triples in g_dynamic_sfx[].
 *
 * Token rules:
 * - "[animacja] NAME.wyc" sets current asset (case-preserved, NUL-term).
 * - "[sampl] WAV1 WAV2 ... (F,)" / "(F,M)" — register each WAV at each F.
 * - end of section = stop. End of file = stop.
 *
 *: per-frame random-pool semantics via multiple
 * entries with same (asset, frame). The M (end frame) in (N,M) is
 * IGNORED (our TriggerFrameSfx doesn't honor "stop at M" — the WAV
 * plays naturally). */
/* Lenient `[keyword]` matcher — tolerates the `[ keyword]` and
 * `[keyword ]` typos found 13× in shipped Wacky.scr (e.g. `[ sampl]
 * CamKlik2.wav (31,)` in the foto3.pic camera animation). On success
 * advances *pp past the closing `]`, returns 1; otherwise leaves *pp
 * unchanged and returns 0. */
static int match_bracket_tag(const uint8_t **pp, const uint8_t *end,
                             const char *keyword)
{
    const uint8_t *p = *pp;
    if (p >= end || *p != '[') return 0;
    ++p;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    size_t klen = strlen(keyword);
    if (p + klen > end) return 0;
    if (memcmp(p, keyword, klen) != 0) return 0;
    p += klen;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end || *p != ']') return 0;
    ++p;
    *pp = p;
    return 1;
}

void ParseSamplTagsForKomnata(const uint8_t *start, const uint8_t *end)
{
    if (!start || !end || start >= end) return;
    const uint8_t *p = start;
    char cur_asset[32] = {0};
    int  cur_asset_set = 0;
    int  added = 0;

    while (p < end) {
        /* Skip whitespace + comments. */
        while (p < end && (*p == ' ' || *p == '\t' ||
                           *p == '\r' || *p == '\n')) ++p;
        if (p >= end) break;
        /* Stop at next major section. */
        if (p + 9 <= end && memcmp(p, "[komnata]", 9) == 0) break;

        /* Look for [animacja] tag (tolerant of `[ animacja]` typos). */
        if (match_bracket_tag(&p, end, "animacja")) {
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            const uint8_t *name_start = p;
            while (p < end && *p != ' ' && *p != '\t' &&
                   *p != '\r' && *p != '\n') ++p;
            size_t n = (size_t)(p - name_start);
            if (n >= sizeof cur_asset) n = sizeof cur_asset - 1;
            memcpy(cur_asset, name_start, n);
            cur_asset[n] = 0;
            /* Normalise to lowercase (Wacky.scr uses mixed case but
 * AnimAsset.name from LoadAssetFromDtaBase is lowercased). */
            for (size_t i = 0; i < n; ++i)
                if (cur_asset[i] >= 'A' && cur_asset[i] <= 'Z')
                    cur_asset[i] = (char)(cur_asset[i] + 32);
            cur_asset_set = 1;
            continue;
        }

        /* Look for [sampl] tag (tolerant of `[ sampl]` typos, must
 * follow an [animacja]). */
        if (cur_asset_set && match_bracket_tag(&p, end, "sampl")) {
            /* Parse rest of line. Collect tokens until '(' (= start
 * of frame trigger list), then per (N,M) tuple register
 * each WAV at frame N. */
            const char *wavs[8];
            char wav_buf[8][32];
            int wav_count = 0;
            while (p < end && *p != '(' && *p != '\r' && *p != '\n') {
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p >= end || *p == '(' || *p == '\r' || *p == '\n') break;
                const uint8_t *tok_start = p;
                while (p < end && *p != ' ' && *p != '\t' &&
                       *p != '(' && *p != '\r' && *p != '\n') ++p;
                size_t tn = (size_t)(p - tok_start);
                if (tn == 0) break;
                if (wav_count < 8) {
                    if (tn >= sizeof wav_buf[0]) tn = sizeof wav_buf[0] - 1;
                    memcpy(wav_buf[wav_count], tok_start, tn);
                    wav_buf[wav_count][tn] = 0;
                    /* Normalise lowercase. */
                    for (size_t i = 0; i < tn; ++i)
                        if (wav_buf[wav_count][i] >= 'A' &&
                            wav_buf[wav_count][i] <= 'Z')
                            wav_buf[wav_count][i] = (char)(wav_buf[wav_count][i] + 32);
                    wavs[wav_count] = wav_buf[wav_count];
                    ++wav_count;
                }
            }
            /* Parse (N,M) tuples number parser:
 * digits → N (start). Comma then digits → M (end). Missing
 * digits → -1 (= 0xFFFF for ushort). actually
 * REQUIRES the comma to add the entry at all (no comma →
 * skip), but shipped Wacky.scr never uses `(N)` so we keep
 * the original-equivalent path. */
            while (p < end && *p == '(') {
                ++p;
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                int n_start = -1;
                {
                    const uint8_t *digit0 = p;
                    int n = 0;
                    while (p < end && *p >= '0' && *p <= '9') {
                        n = n * 10 + (*p - '0'); ++p;
                    }
                    if (p > digit0) n_start = n;
                }
                int n_end = -1;
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p < end && *p == ',') {
                    ++p;
                    while (p < end && (*p == ' ' || *p == '\t')) ++p;
                    const uint8_t *digit0 = p;
                    int n = 0;
                    while (p < end && *p >= '0' && *p <= '9') {
                        n = n * 10 + (*p - '0'); ++p;
                    }
                    if (p > digit0) n_end = n;
                }
                while (p < end && *p != ')' && *p != '\n') ++p;
                if (p < end && *p == ')') ++p;
                if (n_start < 0) {
                    while (p < end && (*p == ' ' || *p == '\t')) ++p;
                    continue;
                }
                int frame_end = (n_end < 0) ? 0xFFFF : n_end;
                /* Register each WAV at frame n_start. */
                const char *asset_interned = strpool_intern(cur_asset, strlen(cur_asset));
                if (asset_interned) {
                    for (int wi = 0; wi < wav_count; ++wi) {
                        if (g_dynamic_sfx_count >= DYNAMIC_SFX_MAX) break;
                        const char *wav_interned =
                            strpool_intern(wavs[wi], strlen(wavs[wi]));
                        if (!wav_interned) continue;
                        g_dynamic_sfx[g_dynamic_sfx_count++] =
                            (struct FrameSfxEntry){asset_interned, n_start, frame_end, wav_interned};
                        ++added;
                    }
                }
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
            }
            continue;
        }

        /* Skip to next newline. */
        while (p < end && *p != '\n') ++p;
    }
    fprintf(stderr, "[sampl] parser: %d entries from this [komnata]\n", added);
}

/* Static fallback table REMOVED (B4 from REVIEW-2026-05) — the D7
 * `Wacky.scr [sampl]` parser (ParseSamplTagsForKomnata, called from
 * LoadKomnata @ ) populates g_dynamic_sfx[] with every
 * (asset, frame, wav) triple needed by the current room. The only
 * code path that triggers per-frame SFX is the per-entity VM tick
 * which can't fire before LoadKomnata has spawned any entities — so
 * the dynamic table is always populated before TriggerFrameSfx fires.
 * No safety net needed. */

/*
 * (t->wav_state[t->cur_wav].hash); // stop
 * t->wav_state[t->cur_wav].playing_flag = 0;
 * // start_frame matched
 * (t->wav_state[t->cur_wav].hash);
 * t->wav_state[t->cur_wav].playing_flag = 0;
 * SampleEntry *w = &t->wav_state[random_idx];
 *
 * Key invariant: the play guard at 0x40A292 is `byte+3 == 0 ||
 * byte+2 == 0`. byte+2 (playing_flag) is sticky-on-play, cleared
 * only by an end-frame trigger or pool swap. byte+3 (channel /
 * "currently-playing") is set when DSound starts the buffer and
 * cleared when it drains. Net effect: a wav RETRIGGERS at the same
 * frame iff its previous instance has finished (drained channel)
 * or a different pool member is picked. Earlier port mis-read this
 * as "byte+2 alone gates retrigger" → single-pool (N,) entries (the
 * majority — slimak.wyc, footsteps, ambient loops) fired exactly
 * once per scene load.
 *
 * Our port doesn't have the original's nested SampleTable list. We
 * track state per (asset, frame, wav) tuple in g_sfx_state[]. The
 * `channel` field mirrors byte+3 by storing the mixer channel a
 * wav was last assigned to; "drained" is detected via
 * s_mix[channel].active flipping to 0 in mixer_callback. */

#define SFX_STATE_MAX 256
struct SfxState {
    const char *asset;        /* keyed by table entry pointer identity */
    int         frame;
    const char *wav;
    uint8_t     playing_flag; /* mirror of wav_state[].playing_flag */
    int8_t      channel;      /* mixer channel this WAV is currently on,
 * or -1 if free / never played. Used to
 * stop in flight when cur_wav swaps to a
 * different pool entry —
 * original's stop call. */
};
static struct SfxState g_sfx_state[SFX_STATE_MAX];
static int             g_sfx_state_count = 0;
static int             g_sfx_cur_wav     = -1;  /* mirror of t->cur_wav */
static const char     *g_sfx_cur_asset   = NULL;

/* Reset all SFX state. Mirrors what the original does on scene transition
 * (+ ): every asset on the komnata is freed →
 * stops every wav in each asset's SampleTable.
 *
 * Three responsibilities:
 * 1. Stop any still-playing mixer channels that g_sfx_state[] points to
 * — looping (N,M) wavs whose end-frame the previous komnata never
 * ticked (e.g. player leaves the room mid-rakieta) would otherwise
 * keep mixing into the next komnata's audio.
 * 2. Zero the slot array. Without this the (asset, wav) pointers in
 * g_sfx_state[] become dangling — ResetDynamicSfxTable is called
 * right after us in LoadKomnata and clears g_dynamic_sfx_strpool,
 * so any retained pointer is unsafe to dereference.
 * 3. Reset the global cur_wav/cur_asset mirror of 's
 * per-table fields (port shortcut). */
void ResetFrameSfxState(void)
{
    for (int i = 0; i < g_sfx_state_count; ++i) {
        struct SfxState *st = &g_sfx_state[i];
        if (st->channel >= MIX_CHAN_SFX_START &&
            st->channel < MIX_CHANNEL_COUNT && st->wav) {
            SDL_LockAudioDevice(s_mix_dev);
            int ours = s_mix[st->channel].active &&
                       strcmp(s_mix[st->channel].name, st->wav) == 0;
            SDL_UnlockAudioDevice(s_mix_dev);
            if (ours) {
                fprintf(stderr, "[sfx] stop '%s' (asset=%s ch=%d) — komnata reset\n",
                        st->wav, st->asset ? st->asset : "(null)", st->channel);
                mixer_stop_channel(st->channel);
            }
        }
    }
    g_sfx_state_count = 0;
    g_sfx_cur_wav     = -1;
    g_sfx_cur_asset   = NULL;
}

/* Get-or-create state slot keyed by (asset_name, frame, wav). Asset names
 * arrive from two paths with different pointers — TriggerFrameSfx passes
 * `atlas->name` (a buffer inside AnimAsset), while sfx_handle_end_frames
 * and StopAllSfxForAsset pass interned pointers from g_dynamic_sfx_strpool.
 * Both strings compare equal but the pointers don't. Use strcmp on asset
 * to bind both paths to the same slot — otherwise the lookup creates a
 * fresh slot with channel=-1 and the mixer_stop_channel call no-ops.
 *
 * `wav` is always interned (both paths derive it from g_dynamic_sfx[].wav)
 * so pointer-equality is fine there. */
static struct SfxState *sfx_state_for(const char *asset, int frame, const char *wav)
{
    for (int i = 0; i < g_sfx_state_count; ++i) {
        if (g_sfx_state[i].frame == frame &&
            g_sfx_state[i].wav   == wav &&
            g_sfx_state[i].asset && asset &&
            strcmp(g_sfx_state[i].asset, asset) == 0)
            return &g_sfx_state[i];
    }
    if (g_sfx_state_count >= SFX_STATE_MAX) return NULL;
    g_sfx_state[g_sfx_state_count] = (struct SfxState){asset, frame, wav, 0, -1};
    return &g_sfx_state[g_sfx_state_count++];
}

extern uint32_t g_tick_counter;

/* Pass 1 — end-frame handling. inner loop @ 0x40A23E:
 * if (entry.end == frame) { STOP cur_wav; flag = 0; cur_wav = -1; return; }
 * `(N,M)` entries in Wacky.scr establish a (start=N, stop=M) range where
 * the wav loops between N and M. Hitting M stops the loop. Without this,
 * marsz.wav / rakiet1a.wav play once and fall silent halfway through
 * the rakieta.wyc animation (frames 1..464 needing ~30s of audio from
 * a 10s wav). */
/* (SampleTable destructor, called from
 * when an animation asset is freed): iterates the wav
 * pool and calls to stop each currently-playing wav.
 *
 * Our port mirror: stop every currently-playing SfxState whose asset
 * matches, and zero its flag. Called when an asset slot is overwritten
 * (ScriptCallLoadAsset) — the per-entity VM may not tick frame_end
 * for looping (N,M) entries if the bytecode skips frames or the
 * script switches assets mid-animation. */
void StopAllSfxForAsset(const char *asset_name)
{
    if (!asset_name) return;
    for (int i = 0; i < g_sfx_state_count; ++i) {
        struct SfxState *st = &g_sfx_state[i];
        if (!st->asset || strcmp(st->asset, asset_name) != 0) continue;
        if (st->channel >= MIX_CHAN_SFX_START &&
            st->channel < MIX_CHANNEL_COUNT) {
            /* Only stop if this channel is still playing OUR wav —
 * the slot may have been stolen for another sound. */
            SDL_LockAudioDevice(s_mix_dev);
            int ours = s_mix[st->channel].active &&
                       strcmp(s_mix[st->channel].name, st->wav) == 0;
            SDL_UnlockAudioDevice(s_mix_dev);
            if (ours) {
                fprintf(stderr, "[sfx] stop '%s' (asset=%s ch=%d) — asset reload\n",
                        st->wav, asset_name, st->channel);
                mixer_stop_channel(st->channel);
            }
        }
        st->channel = -1;
        st->playing_flag = 0;
    }
}

static void sfx_handle_end_frames(const char *asset_name, int frame)
{
    for (int i = 0; i < g_dynamic_sfx_count; ++i) {
        struct FrameSfxEntry *e = &g_dynamic_sfx[i];
        if (e->frame_end == 0xFFFF || e->frame_end != frame) continue;
        if (strcmp(e->asset, asset_name) != 0) continue;
        struct SfxState *st = sfx_state_for(e->asset, e->frame_start, e->wav);
        if (!st) continue;
        fprintf(stderr, "[sfx] stop '%s' at end-frame %d (asset=%s start=%d ch=%d)\n",
                e->wav, frame, asset_name, e->frame_start, st->channel);
        if (st->channel >= MIX_CHAN_SFX_START &&
            st->channel < MIX_CHANNEL_COUNT) {
            mixer_stop_channel(st->channel);
        }
        st->channel = -1;
        st->playing_flag = 0;
    }
}

/* ---- TriggerFrameSfx constants + helpers -------------------------- */

/* `[sampl] WAV1 WAV2 .. (F,M)` — at most this many WAVs can sit in a
 * random pool for one (asset, frame) pair. Matches the static cap in
 * ParseSamplTagsForKomnata's per-tag wav_count. */
#define SFX_RANDOM_POOL_MAX     8

/* Frame-end sentinel — `(F,)` with no second arg encodes "no end
 * frame", which means the wav plays once (no loop) instead of looping
 * until a matching end-frame trigger stops it. */
#define SFX_FRAME_END_NONE      0xFFFF

/* Collect every dynamic SFX entry whose (asset, frame_start) matches
 * the current trigger. Returns the pool size (0..SFX_RANDOM_POOL_MAX). */
static int collect_random_sfx_pool(const char *asset_name, int frame,
                                   const char **pool, int *pool_end)
{
    int n = 0;
    for (int i = 0;
         i < g_dynamic_sfx_count && n < SFX_RANDOM_POOL_MAX;
         ++i)
    {
        if (g_dynamic_sfx[i].frame_start == frame &&
            strcmp(g_dynamic_sfx[i].asset, asset_name) == 0)
        {
            pool[n]     = g_dynamic_sfx[i].wav;
            pool_end[n] = g_dynamic_sfx[i].frame_end;
            ++n;
        }
    }
    return n;
}

/* Stop any previously-picked WAV from the same asset's pool that's
 * still active on its mixer channel. Used when the random pick swaps
 * mid-stream to a different WAV. */
static void stop_previous_pool_pick(const char *asset_name, int frame,
                                    const char **pool, int pool_n)
{
    if (g_sfx_cur_asset != asset_name) return;
    if (g_sfx_cur_wav < 0 || g_sfx_cur_wav >= pool_n) return;
    struct SfxState *prev = sfx_state_for(asset_name, frame,
                                          pool[g_sfx_cur_wav]);
    if (!prev) return;
    prev->playing_flag = 0;
    if (prev->channel >= 0) {
        mixer_stop_channel(prev->channel);
        prev->channel = -1;
    }
}

/* True iff `st` is currently active on its mixer channel playing
 * exactly `wav`. The lock is held only for the channel snapshot. */
static int sfx_is_currently_playing(struct SfxState *st, const char *wav)
{
    if (st->channel < MIX_CHAN_SFX_START ||
        st->channel >= MIX_CHANNEL_COUNT)
    {
        return 0;
    }
    int playing = 0;
    SDL_LockAudioDevice(s_mix_dev);
    if (s_mix[st->channel].active &&
        strcmp(s_mix[st->channel].name, wav) == 0)
    {
        playing = 1;
    }
    SDL_UnlockAudioDevice(s_mix_dev);
    return playing;
}

void TriggerFrameSfx(const char *asset_name, int frame)
{
    if (!asset_name) return;

    /* Pass 1: stop any (N,M) loops whose M matches this frame. */
    sfx_handle_end_frames(asset_name, frame);

    /* Pass 2: collect the random pool for (asset, start=frame). */
    const char *pool[SFX_RANDOM_POOL_MAX];
    int         pool_end[SFX_RANDOM_POOL_MAX];
    int         n = collect_random_sfx_pool(asset_name, frame, pool, pool_end);
    if (n == 0) return;

    /* Uniform random pick in [0, n). */
    int         random_idx = (int)WackiRand((uint16_t)n);
    const char *wav        = pool[random_idx];

    /* If the previous pick was a different wav from this asset, stop
     * it on its mixer channel before launching the new one. */
    if (g_sfx_cur_asset == asset_name && g_sfx_cur_wav != random_idx) {
        stop_previous_pool_pick(asset_name, frame, pool, n);
    }
    g_sfx_cur_asset = asset_name;
    g_sfx_cur_wav   = random_idx;

    /* Replay guard: skip ONLY when the same wav is BOTH still actively
     * playing on its mixer channel AND has fired at least once. The
     * sticky playing_flag alone never blocks replay — it just records
     * that we've started this trigger before. */
    struct SfxState *st = sfx_state_for(asset_name, frame, wav);
    if (!st) return;
    if (st->playing_flag && sfx_is_currently_playing(st, wav)) return;
    st->playing_flag = 1;

    /* loop=1 for (N,M) entries — the wav loops until the M-frame
     * trigger calls sfx_handle_end_frames → mixer_stop_channel. */
    int want_loop = (pool_end[random_idx] != SFX_FRAME_END_NONE);
    extern int PlaySfxLoopAndGetChannel(const char *wav_name, int loop);
    st->channel = (int8_t)PlaySfxLoopAndGetChannel(wav, want_loop);
}

/* PlaySfx — fire-and-forget WAV via mixer SFX channel pool.
 * Picks free channel from [MIX_CHAN_SFX_START .. MIX_CHANNEL_COUNT) or
 * steals oldest if all busy. Auto-frees converted buffer when callback
 * drains the channel (TickSfx). */
extern void PlaySfxPanned(const char *wav_name, uint8_t gain_l, uint8_t gain_r);
int PlaySfxPannedAndGetChannel(const char *wav_name,
                               uint8_t gain_l, uint8_t gain_r);   /* fwd */

void PlaySfx(const char *wav_name)
{
    PlaySfxPanned(wav_name, 128, 128);
}

/* T36 — PlaySfxPanned: same as PlaySfx with explicit L/R gain.
 * Caller computes gains from positional source vs listener via
 * SoundQueueMixForListener (which returns packed 0xRRCCLL — split
 * the C contribution equally into L+R for our 2-channel mixer). */
void PlaySfxPanned(const char *wav_name, uint8_t gain_l, uint8_t gain_r)
{
    (void)PlaySfxPannedAndGetChannel(wav_name, gain_l, gain_r);
}

/* Internal — returns the mixer channel the WAV landed on (or -1 if it
 * couldn't be loaded / mixer not open). Used by TriggerFrameSfx so it
 * can stop the previous WAV in flight when cur_wav swaps. */
int PlaySfxPannedAndGetChannel(const char *wav_name,
                               uint8_t gain_l, uint8_t gain_r)
{
    if (!wav_name) return -1;
    if (!g_audio_sfx_enabled || !g_audio_sound_enabled) return -1;
    if (!mixer_ensure_open()) return -1;

    extern uint32_t g_tick_counter;
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    int oldest_slot = MIX_CHAN_SFX_START;
    for (int i = MIX_CHAN_SFX_START; i < MIX_CHANNEL_COUNT; ++i) {
        if (!s_mix[i].active) { slot = i; break; }
        if (s_mix[i].start_tick < oldest) {
            oldest = s_mix[i].start_tick;
            oldest_slot = i;
        }
    }
    if (slot < 0) slot = oldest_slot;

    Uint8 *buf = NULL; Uint32 len = 0;
    if (!mixer_load_wav(wav_name, &buf, &len)) {
        fprintf(stderr, "[sfx] cannot load '%s'\n", wav_name);
        return -1;
    }
    mixer_assign(slot, buf, len, 0, wav_name);
    /* mixer_assign sets gain to 128/128 (identity); override here for
 * positional pan. Lock once for atomic L+R update. */
    SDL_LockAudioDevice(s_mix_dev);
    s_mix[slot].gain_l = gain_l;
    s_mix[slot].gain_r = gain_r;
    SDL_UnlockAudioDevice(s_mix_dev);
    fprintf(stderr, "[sfx] play '%s' on mixer ch %d (%u bytes, gain L=%u R=%u)\n",
            wav_name, slot, len, gain_l, gain_r);
    return slot;
}

int PlaySfxAndGetChannel(const char *wav_name)
{
    return PlaySfxPannedAndGetChannel(wav_name, 128, 128);
}

/* Looping variant — same as PlaySfxAndGetChannel but assigns the mixer
 * channel with `loop=1` so the audio callback wraps src_frame back to 0
 * instead of marking the channel inactive. Used by TriggerFrameSfx for
 * `[sampl] WAV (N,M)` entries — caller must stop the channel at frame
 * M via sfx_handle_end_frames. */
int PlaySfxLoopAndGetChannel(const char *wav_name, int loop)
{
    int ch = PlaySfxPannedAndGetChannel(wav_name, 128, 128);
    if (ch >= 0 && loop) {
        SDL_LockAudioDevice(s_mix_dev);
        s_mix[ch].loop = 1;
        SDL_UnlockAudioDevice(s_mix_dev);
    }
    return ch;
}
