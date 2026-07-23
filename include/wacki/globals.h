/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * include/wacki/globals.h — engine-wide extern globals + aliases.
 *
 * Pulled out of include/wacki.h. The umbrella header still includes
 * us, so call sites continue to write `#include "wacki.h"`.
 *
 * What lives here: every global that crosses TU boundaries — screen
 * buffer, palette, save state, script registers, panel state, frame
 * timers, audio toggles, scene flags, F-key request latches, etc.
 *
 * Function declarations live in include/wacki/api.h; type definitions
 * live in include/wacki/types.h. */

#ifndef WACKI_GLOBALS_H
#define WACKI_GLOBALS_H

#include <stdint.h>
#include "wacki/types.h"

/* ---- screen buffer + palette ------------------------------------- */

extern char      g_data_root[260];
extern uint8_t   g_palette_rgb[256*3];
extern uint16_t  g_screen_w, g_screen_h;
extern uint8_t  *g_back_shadow;          /* 320×240×8bpp paletted shadow */
extern uint16_t  g_screen_w_dim, g_screen_h_dim;

/* ---- script register file + scene state -------------------------- */

/* 0x200 entries, NOT 0x129: vm_var_get/set mask the index with
 * SCRIPT_VAR_INDEX_MASK (0x1FF), so any index up to 511 is reachable and
 * MUST be in bounds. Only the first 0x129 are persisted (WackiSlot.script_vars);
 * the rest are runtime scratch the original engine's 512-slot file also had. */
extern uint32_t  g_script_vars[0x200];
extern uint32_t  g_entity_state[0x11C];
extern uint16_t  g_active_actor;
extern uint16_t  g_cur_etap;
extern uint16_t  g_cur_komnata;

/* Current room's looping background-music tracks, resolved from the
 * komnata's Wacky.scr room-level [sampl] (see FindKomnataBgMusic). The
 * original LAYERS all of them at once, each looped; the port plays each on
 * its own looping channel. Set by LoadKomnata, consumed by LoadKomnataScene.
 * count==0 = no room music (a room with no room-level [sampl] plays silent,
 * as in the original). */
extern char      g_scene_bg_tracks[KOMNATA_BG_MUSIC_MAX_TRACKS][KOMNATA_BG_MUSIC_NAME_MAX];
extern int       g_scene_bg_track_count;

extern StageDef *g_stage;
extern StageDef *g_stage_table[5];

/* Original PE virtual address of the current stage's per-stage
 * table. Set by LoadStage / play_demo_scene. Read by
 * DispatchClickEvent to walk the per-stage verb_table (+4) and
 * object_table (+8) in PE memory via PeLoaderRead. Stage 1 =
 * 0x00428220, stage 2 = 0x004310A0, etc. — indexed by g_cur_etap.
 * 0 = no stage (DispatchClick noop). */
extern uint32_t g_stage_va;

/* ---- font + held-item -------------------------------------------- */

extern FontHandle *g_default_font;       /* "Futura.30" */

/* Currently-held inventory item id. 0 = nothing held (treat as 0x26
 * = "look at" verb). Set when the user clicks an item in the bottom
 * panel; consumed by DispatchClickEvent as `this_id`. */
extern uint16_t g_held_item;

/* ---- game-over signal aliases ------------------------------------ *
 *
 * g_game_over_code aliases g_script_vars[14] — both names refer to
 * the SAME dword (the original binary kept these at the same PE
 * address; forking them in the port stalls end-of-stage / death /
 * chapter-select because scripts write var[14] but nothing reads a
 * separate int).
 *
 * Scripts trigger transitions via SET_VAR (op 0x0D) on var index 14:
 *   val=1  → death (Dane_14.dta cutscene)
 *   val=3  → chapter-select UI (sel_tlo.pic)
 *   val=4  → stage-end death cutscene, then return to menu
 *
 * g_completed_stages aliases g_script_vars[17] — bitfield, bit i set
 * = stage (i+1) completed. Scripts set it via op 0x0A VAR_OR var[17]
 * imm=1<<stage in each stage's ending bytecode (right before
 * var[14]=3). */
#define g_game_over_code    (*(int      *)&g_script_vars[14])
#define g_completed_stages  (*(uint32_t *)&g_script_vars[17])

/* ---- save + stats ------------------------------------------------ */

extern int           g_save_request;
extern WackiSaveFile g_save;
extern WackiStats    g_stats;

/* ---- input latches ----------------------------------------------- */

extern uint8_t   g_lmb_clicked;
extern uint8_t   g_rmb_clicked;
extern uint8_t   g_lmb_handled;
extern uint16_t  g_key_state;
/* T53 — F5 / F9 latches. PlatformPumpEvents sets these on F5/F9
 * key-down; the play_demo_scene main loop consumes + clears them per
 * frame. */
extern uint8_t   g_quicksave_request;
extern uint8_t   g_quickload_request;
/* T56 — F3 stats dump latch. */
extern uint8_t   g_stats_dump_request;
/* T24 — F12 pause/exit confirmation latch (Pytanie.scr equivalent). */
extern uint8_t   g_pause_menu_request;

/* Virtual cursor position (engine-space 640×480). Driven by the SDL
 * mouse-motion handler + the d-pad/analog virtual cursor; read by the HUD,
 * hit-test, and cursor-paint paths. */
extern int16_t   g_mouse_x;
extern int16_t   g_mouse_y;

/* ---- display / CLI knobs (set by parse_cli_args + config) -------- */

extern int          g_headless;       /* --headless: skip window + present */
extern int          g_no_pacing;      /* --test-cutscenes: skip frame sleeps */
extern uint8_t      g_present_suppressed; /* hold the screen during a komnata load
                                           * (the embedded settling ticks paint new
                                           * entities on the old BG before the new
                                           * one is loaded — don't present that) */
extern int          g_scale_factor;   /* --scale N: window = 640×480 × N */
extern const char  *g_scale_mode;     /* --scaler: nearest|linear|best */
extern int          g_fullscreen;     /* --fullscreen / F11 */

/* Set for the duration of src/flic.c's PlayFlicAviFile loop (cutscene
 * AVI playback — intro, death, per-stage transitions), cleared right
 * after. Platform video backends MAY use this to skip work that's
 * wasted or actively harmful during a cutscene — e.g. the 3DS backend
 * (src/platform/3ds/video_3ds_gl.c) skips its bottom-screen zoom/touch
 * view entirely while this is set: that view exists to magnify the
 * point-and-click cursor, which isn't shown/usable during a cutscene
 * anyway, so rendering it is pure wasted per-frame cost that (per user
 * report) could push a frame over its pacing budget and contribute to
 * audio/video drift. Every other platform ignores this flag (it's a
 * pure opt-in optimization hook, not a behavior change they need). */
extern int          g_cutscene_playing;

/* ---- stereoscopic 3D background/foreground split (3DS-only) ------ *
 *
 * EXPERIMENTAL — lives on branch 3ds-stereo3d-experiment. Lets the 3DS
 * video backend (src/platform/3ds/video_3ds_gl.c) render the top screen
 * with the New3DS/3DS's real autostereoscopic display: the room
 * BACKGROUND stays flat (depth 0, "on the glass"), while every other
 * painted pixel this frame — actors, held/scene items, the HUD panel,
 * the cursor — is treated as foreground and rendered with a small
 * per-eye horizontal disparity so it visually pops out toward the
 * viewer. This is a BINARY split (bg vs. fg), not per-object depth
 * levels — the engine has no notion of "distance" for a sprite once it
 * reaches src/graphics.c's flat 8bpp shadow buffer (every blit call
 * site just writes palette-index bytes into one shared surface), so
 * there is no cheap way to recover finer-grained depth without
 * threading a depth tag through every BlitSpriteToBackbuffer /
 * PaintImageToBackbuffer call site across the whole engine. The 3-way
 * split the user actually wants (background always flat, everything
 * else may pop out) needs only bg-vs-not-bg, which this gets for free
 * by diffing two snapshots of the SAME shadow buffer — no changes to
 * any blit function or call site required.
 *
 * g_stereo3d_bg_layer_wanted — written EVERY FRAME by video_3ds_gl.c,
 * from the physical 3D slider position (osGet3DSliderState() > 0).
 * Defaults to 0 and is NEVER written by any other platform, or even by
 * the 3DS backend when the slider is centered/off (including every
 * Old/New 2DS model, which has no slider or stereo screen at all) — so
 * the extra work below costs literally nothing unless the player has
 * physically enabled 3D on real 3DS/New3DS hardware. Read by
 * src/scene/frame_tick.c (shared code — the only place in the engine
 * that reliably runs once per displayed gameplay frame, right after
 * the background is painted and right before entities/HUD/cursor are)
 * to decide whether to pay for the one-time background snapshot this
 * frame; a plain global rather than a HAL function so no stub is
 * needed in any of the other seven platform directories. */
extern int          g_stereo3d_bg_layer_wanted;

/* g_bg_layer_valid / g_bg_layer_shadow — the actual snapshot mechanism.
 * frame_tick.c's repaint_scene_background() sets g_bg_layer_valid = 0,
 * then — only if g_stereo3d_bg_layer_wanted — copies the just-painted
 * (background-only) g_back_shadow into g_bg_layer_shadow and sets
 * g_bg_layer_valid = 1. video_3ds_gl.c's plat_video_present consumes
 * (reads then clears) g_bg_layer_valid once per present call: if still
 * valid, it diffs g_bg_layer_shadow against the just-finished, fully
 * composited frame it was handed to build a per-pixel foreground mask
 * for the stereo blit; if not valid (e.g. this presented frame came
 * from a menu screen that never runs frame_tick.c's per-frame hook at
 * all — see src/menu/main_menu.c, which paints + presents directly),
 * it safely falls back to flat/non-stereo for that one frame rather
 * than reusing a stale, mismatched snapshot. This "set 0, maybe set 1,
 * consumed-and-cleared by the reader" pattern needs no shared frame
 * counter or extra bookkeeping in graphics.c. */
extern int          g_bg_layer_valid;
extern uint8_t     *g_bg_layer_shadow;   /* lazily allocated, g_screen_w × g_screen_h */

/* ---- audio gates ------------------------------------------------- *
 *
 * Options-menu toggles. When music or the global sound flag flips
 * off mid-play, the music channel is stopped; when flipped back on,
 * the last-requested track resumes. SFX flag just gates new PlaySfx
 * calls. */
extern int g_audio_music_enabled;
extern int g_audio_sfx_enabled;
extern int g_audio_voice_enabled;
extern int g_audio_sound_enabled;
/* T103 — Solund-menu non-audio gates. Set/cleared by SolundClick. */
extern uint8_t g_subtitles_on;       /* gates op 0x09 SHOW_TEXT */
extern uint8_t g_dialogues_on;       /* gates op 0x52/0x53 */

/* ---- per-frame timers -------------------------------------------- */

extern uint32_t g_tick_counter;
/* Per-frame deltas. _ms is real wall-clock ms (held-item ghost
 * interp, speech-balloon dismiss timer); _ticks is 10 ms units
 * (cursor anim, entity VM +0x3C countdown, op 0x14 / op 0x26 / op
 * 0x3D wait loops). */
extern uint32_t g_frame_delta_ms;
extern uint16_t g_frame_delta_ticks;

/* ---- komnata flags + perspective --------------------------------- *
 *
 * Komnata flag bitfield — loaded from the komnata table at scene
 * entry. Low bits gate per-room features (bit 0 = panel visible,
 * bit 1 = actors alive / has perimeter bands); the high byte is
 * shifted by ScriptCallBgMaskSetup as `(flags & 0xff02) << 1`, so
 * the full uint16_t width is load-bearing. */
extern uint16_t  g_komnata_flags;
extern uint16_t  g_cursor_speed;
extern uint16_t  g_perspective_min;
extern uint16_t  g_perspective_step;

/* ---- script objects + atlases ------------------------------------ */

extern void *g_dialogues_obj;
extern void *g_scripts_obj;
extern void *g_items_obj;
extern AnimAsset *g_panel_asset;        /* stage panel.wyc atlas */
extern AnimAsset *g_items_atlas;        /* przedm.wyc inventory icons */
extern Entity    *g_actor[2];

/* ---- entity lists ------------------------------------------------ */

extern Entity *g_render_list_head;
extern Entity *g_click_list_head;

/* ---- panel verb selection ---------------------------------------- *
 *
 * See src/hud/panel.c for the hit-test that publishes hover state. */
extern uint16_t g_panel_verb_tab[6];
extern uint16_t g_hover_panel_verb;
extern uint16_t g_hover_scene_verb;       /* T31 v2 — cursor state */

/* Inventory page rotation. */
extern uint16_t  g_panel_page_idx;
extern uint16_t  g_panel_verb_tab_backup[6];
extern uint8_t   g_panel_redraw;

#endif /* WACKI_GLOBALS_H */
