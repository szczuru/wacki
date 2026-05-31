/*
 * game.c — top-level game state machine, scene runner, click dispatcher,
 * per-frame tick.
 *
 * Original addresses:
 * InitializeGameSubsystems 0x00403A30
 * PreloadCommonAssets 0x00403790
 * RunMainGameLoop 0x0040BBF0
 * RunGameStageLoop 0x0040BEA0
 * RunMenuScene 0x0040B5E0
 * LoadStage 0x00403320
 * ProcessGameFrameTick 0x004025C0
 * DispatchClickEvent 0x004094A0
 * ScreenshotPCX/BMP (debug 'P' / 'B' inside ProcessGameFrameTick)
 */
#include "wacki.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>

/* Portable key codes — match SDL_Keycode for ASCII keys (matches the
 * original where Win32 VK_ESCAPE = 0x1B happens to equal ASCII ESC). */
#define VK_ESCAPE 0x1B
#define VK_SPACE  0x20
#define VK_F12    0x7B

/* Forward decl: stubs.c owns ScriptObj for the portable build. */
struct ScriptObj { const uint8_t *start; const uint8_t *end; uint32_t size; uint8_t *buf; };

/* ---- shared state ------------------------------------------------------- */
StageDef *g_stage = NULL;                       /* g_actor_walk_anim_table */
uint16_t  g_cur_etap    = 0;                    /* */
uint16_t  g_cur_komnata = 0;                    /* g_cur_komnata */
/* g_game_over_code is a macro alias for g_script_vars[14] — see wacki.h.
 * g_completed_stages is similarly aliased to g_script_vars[17] (Fix #21).
 * No separate storage; the values live inside the script_vars array so
 * the SET_VAR / VAR_OR opcodes (op 0x0D / 0x0A, used by scripts) and the
 * post-loop switch / SelTloRefreshButtons see the SAME memory. */
int       g_save_request = 0;                   /* g_script_vars */
uint32_t  g_tick_counter = 0;                   /* g_tick_counter */
uint8_t   g_lmb_handled = 0;                    /* g_lmb_handled */

extern Entity *g_actor[2];                      /* g_actor/728 */

/* ---- T2 phase B: scene/walkability state promoted to globals -------------- *
 * Previously locals inside play_demo_scene; promoted so ProcessGameFrameTick
 * Inner can run the click handler + hotspot scan without scene-locals
 * threading. Set by play_demo_scene at scene-load top, cleared at end. */
const DemoScene *g_current_scene = NULL;            /* hotspot + walk-bounds */
const uint8_t *g_walk_fld_pixels = NULL;            /* .fld walkability bitmap (1bpp) */
int            g_walk_fld_w = 0,    g_walk_fld_h = 0;
int            g_walk_fld_ox = 0,   g_walk_fld_oy = 0;
int            g_walk_fld_stride = 0;
int            g_walk_x0 = 0, g_walk_x1 = 0;        /* fallback bbox (no .fld) */
int            g_walk_y0 = 0, g_walk_y1 = 0;
/* g_scene_quit — set by OpenOptionsMenu when Pytanie TAK confirms quit
 * mid-game. play_demo_scene's main loop polls it as a quit signal.
 * T35 removed g_pending_scene_exit (hotspot-click out-signal) entirely
 * after verb-driven exits via op 0x20 + LoadKomnataScene now work. */
int            g_scene_quit = 0;

/* T-trail: scene BG published so ProcessGameFrameTickInner can repaint
 * underneath entities every frame — RestorePrevFrameRects
 * at top of (PGFT). Without this, blocking-wait pumps from
 * inside scripts (op 0x09 SHOW_TEXT, dialog runner, op 0x10/0x11/0x12 walks)
 * tick + render entities without clearing the prior frame → sprite trails.
 * Set by LoadKomnataScene (T22 phase B). */
void      *g_scene_bg_raw  = NULL;
uint32_t   g_scene_bg_size = 0;
/* T22 phase B — fld_asset hoisted from play_demo_scene local to global
 * so LoadKomnataScene can free old + load new in-place during op 0x20
 * transitions (no play_demo_scene unwinding). */
AnimAsset *g_scene_fld_asset = NULL;

/* ---- forward decls for things we use ------------------------------------ */
extern void InstallPalette(const uint8_t *rgb, uint16_t first);
extern void PaletteFadeInOut(uint16_t pct, const uint8_t *pal,
                             uint16_t first, uint32_t flags,
                             void *cb);
extern void *g_dialogues_obj;
extern void *g_scripts_obj;
extern void *g_items_obj;                       /* */
extern AnimAsset *g_panel_cursor;               /* g_panel_cursor (Krazek.pic) */

/* g_stage_table is defined in stubs.c — see PTR_PTR_00442FA8 in the binary. */

/* ------------------------------------------------------------------------- *
 * PreloadCommonAssets — 0x00403790
 *
 * Loaded once at startup. Holds the universally-shared sprites resident
 * in memory between stage swaps.
 * ------------------------------------------------------------------------- */
FontHandle *g_default_font = NULL;     /* — "Futura.30" bitmap font */

extern AnimAsset *g_items_atlas;       /* g_items_atlas — przedm.wyc */

/* T4 step 1: actor atlas singletons. Loaded once at startup, persist
 * across all scene transitions. Original engine spawns Ebek/Fjej once
 * at game start with these atlases bound; verb scripts post-GO_EXIT
 * can reposition the SAME actor entity without atlas reload. */
AnimAsset *g_ebek_atlas = NULL;        /* ebek.wyc — Ebek sprite frames */
AnimAsset *g_fjej_atlas = NULL;        /* fjej.wyc — Fjej sprite frames */
AnimAsset *g_ebfj_atlas = NULL;        /* g_ebfj_atlas — ebfj.wyc actor-portrait
 atlas (4 frames: 0/1 = Ebek/Fjej
 active w/ frame, 2/3 = inactive). */

/* T31 — cursor state atlases. PreloadCommonAssets which
 * loaded these into g_cursor_atlas..0x004514A4 then 's state
 * table @ indexed them per cursor state (0..7).
 *
 * Naming note: in the original PE these slots are loaded with
 * olowek1.wyc / kaseta.wyc / magnes1*.wyc / drzwi1*.wyc — but they're
 * NOT the puzzle items. They're CURSOR SPRITE ATLASES with cursor-
 * shaped frames (the names just happened to be assigned to the slots
 * in PreloadCommonAssets; in RE we see them used as cursor only by
 * ). The 8-state cursor anim table maps states 0..7 to
 * indices into the slot array. */
AnimAsset *g_cursor_atlas[8] = {0};    /* g_cursor_atlas..0x004514A4 */
uint8_t    g_cursor_state    = 0;      /* g_script_running */
uint16_t   g_cursor_frame    = 0;      /* cursor_state_struct + 0x30 */
uint16_t   g_cursor_frame_acc = 0;     /* cursor_state_struct + 0x3C accumulator */

extern void BuildStageTable(void);                /* stubs.c — T26 */

/* PreloadCommonAssets moved to src/scene/preload.c. */

/* ------------------------------------------------------------------------- *
 * ProcessGameFrameTick — 0x004025C0
 * ------------------------------------------------------------------------- */
extern void ScreenshotToPcxAutoIncrement(void);  /* extracted from RE'd 'P' branch */
extern void ScreenshotToBmpAutoIncrement(void);  /* 'B' branch */
/* UpdateAllEntities removed — its responsibilities are split between
 * EntityWalkerTick (per-entity VM ticks) and EntityRenderAll (z-sorted
 * render), both wired into ProcessGameFrameTick. */
extern void FlushQueuedClicks(void);

/* ProcessGameFrameTick —
 *
 * Original sequence:
 * PeekMessage / WaitMessage if backgrounded
 * screenshot keys ('P' PCX, 'B' BMP)
 * — composite/blit frame
 * — cursor sprite update (inventory pickup)
 * — bottom-panel hit-test (sets g_hover_panel_verb verb)
 * — mouse hit-test (sets g_hover_scene_verb hover_verb)
 * UpdateActorMovement — drives g_actor[] walkers per cursor
 * — per-entity VM tick (ExecEntityScript)
 * / 00406EB0— prop/dialogue tick (deferred)
 * — EntityRenderAll with z-sort
 * drain click_queue — FlushQueuedClicks → DispatchClickEvent
 *
 * Blocking script ops (op 0x12 ANIM_BOTH_WAIT, op 0x14 WAIT_MS, op 0x15
 * WAIT_ENTITY) loop on this; without driving the walker + render here,
 * those waits visually freeze the scene. */
/* Forward decl for PaintHudOverlay use. */
void paint_anim_button_at(AnimAsset *atlas, uint16_t frame,
                          int16_t base_x, int16_t base_y, int paint);
/* Forward decl for ProcessGameFrameTickInner use. */
void HandleSceneInput(void);
int paint_rawb_pic(const void *blob, uint32_t size, int as_overlay);

/* Functions moved to other TUs but still called from game.c. */
extern void PaintHudOverlay(void);
extern void ProcessGameFrameTickInner(void);
extern void PaintCursor(void);
extern void UpdateCursorState(void);
extern int  PreloadCommonAssets(void);
extern int  is_walkable_at(int sx, int sy);
extern int  ClickHitTest(int16_t mouse_x, int16_t mouse_y, uint16_t *out_verb);
extern int  g_no_pacing;                /* main.c — --no-pacing flag */
extern const void *PeLoaderRead(uint32_t va);

/* Cursor state + paint (UpdateCursorState, PaintCursor) moved to src/hud/cursor.c. */

/* PaintHudOverlay moved to src/scene/hud_paint.c. */

/* ProcessGameFrameTick(Inner) moved to src/scene/frame_tick.c. */

/* ------------------------------------------------------------------------- *
 * DispatchClickEvent
 *
 * SCUMM-style verb/noun dispatcher. The per-stage descriptor at
 * g_actor_walk_anim_table (= the entry from PTR_PTR_00442FA8[etap-1]) carries two
 * tables of 6-byte entries:
 *
 * +0x04 verb_table = { u16 verb_id; u32 script_ptr; } *
 * +0x08 object_table = { u16 obj_id; u32 script_ptr; } *
 *
 * - terminator is the first entry with id == 0
 * - entries are 6 bytes (the 2 bytes between u16 and u32 are unused)
 *
 * Flow: search verb_table for verb_id, run that script — if it returns
 * non-zero, *also* search object_table for obj_id and run that script.
 *
 * Original reads both tables out of static PE memory (g_actor_walk_anim_table =
 * absolute pointer). The port resolves them through PeLoaderRead, then
 * xlats the script pointer either to a manually-embedded blob in
 * binary_data.c or back to PE memory.
 * ------------------------------------------------------------------------- */
uint32_t g_stage_va = 0;            /* g_actor_walk_anim_table — original VA of current stage def */
uint16_t g_held_item = 0x26;        /* g_held_item — currently held inventory item
 * (0x26 = neutral / nothing held; see
 * RunGameStageLoop @ 0x0040C0C6 where the
 * post-dispatch reset writes 0x26). */
extern const void *xlat_binary_ptr(uint32_t addr);

/* Read 6-byte entry id+ptr from PE memory at table_va + idx*6. */
int read_dispatch_entry(uint32_t table_va, int idx,
                               uint16_t *out_id, uint32_t *out_script_va)
{
    extern const void *PeLoaderRead(uint32_t va);
    const uint8_t *p = (const uint8_t *)PeLoaderRead(table_va + (uint32_t)idx * 6u);
    if (!p) return 0;
    *out_id        = (uint16_t)(p[0] | (p[1] << 8));
    *out_script_va = (uint32_t)(p[2] | (p[3] << 8) | (p[4] << 16) | (p[5] << 24));
    return 1;
}

/* find_dispatch_script + DispatchClickEvent moved to src/scene/dispatch.c. */

/* ------------------------------------------------------------------------- *
 * LoadStage — 0x00403320
 * ------------------------------------------------------------------------- */
extern uint32_t g_stage_va_table[5];                 /* stubs.c — T26 */
extern void     LoadActorWalkAnims(uint32_t stage_va); /* stubs.c */

int LoadStage(uint16_t stage)
{
    if (stage == 0) return 0;
    int idx = stage - 1;
    if (idx >= 5 || !g_stage_table[idx]) return 0;
    g_stage    = g_stage_table[idx];
    g_cur_etap = stage;
    /* T26: also propagate the raw PE VA so subsequent LoadKomnata /
 * DispatchClickEvent / LoadActorWalkAnims pick the right stage
 * descriptor. play_demo_scene's hard-coded stage-1 assignment
 * (0x00428220) is kept as a fallback for the demo entry path. */
    g_stage_va = g_stage_va_table[idx];
    LoadActorWalkAnims(g_stage_va);

    /*:
 *; // ResetInventory — clear g_inventory + page=0
 * Original calls this before loading the new stage's panel + palette
 * so any held-from-previous-stage items get dropped. */
    ResetInventory();

    /* find Wacky.scr section for "[etap]N" */
    char buf[2] = { (char)('0' + g_cur_etap), 0 };
    FindScriptByStageAndRoom(g_scripts_obj, buf, "[komnata]");

    /* load stage panel —:
 * The asset has 6 frames (one per button slot) sharing a single
 * (panel_x, panel_y) origin on its 0th frame, used by PanelHitTest
 * to convert cursor coords into panel-local space.
 *
 * T27: each stage has its own panel.wyc / panel2.wyc / panel3.wyc /
 * panel4.wyc (stage 5 = credits, panel=NULL). Free any previously
 * loaded panel before reload to avoid leaking the old asset.
 * Note: ebek.wyc + fjej.wyc are the SAME filename across stages
 * 1-4 (verified via BuildStageTable log), so g_ebek_atlas /
 * g_fjej_atlas remain singletons loaded once in PreloadCommonAssets.
 *
 * Stage 5 (Monter finale = ACME assembly + end credits) has
 * panel_wyc/ebek_wyc/fjej_wyc all NULL — it's a cutscene-only
 * "stage" with no HUD and no playable actors. Without the
 * NULL-side branch below, g_panel_asset / g_actor[0] / g_actor[1]
 * kept their stage 4 values, so PaintHudOverlay + EntityRenderAll
 * still drew the panel and Ebek/Fjej during the finale.
 * actor-list management gated on komnata flag
 * g_settings_anim_active & 2 — when the new stage's atlas slot is NULL, the
 * actor must be unlinked from the render list. */
    if (g_stage->panel_wyc) {
        if (g_panel_asset) {
            FreeAsset(g_panel_asset);
            g_panel_asset = NULL;
        }
        g_panel_asset = LoadAssetFromDtaBase(g_stage->panel_wyc);
        if (!g_panel_asset) return 0;
    } else if (g_panel_asset) {
        FreeAsset(g_panel_asset);
        g_panel_asset = NULL;
    }
    {
        extern void UnlinkEntity(Entity *e);
        if (!g_stage->ebek_wyc && g_actor[0]) {
            UnlinkEntity(g_actor[0]);
            g_actor[0] = NULL;
        }
        if (!g_stage->fjej_wyc && g_actor[1]) {
            UnlinkEntity(g_actor[1]);
            g_actor[1] = NULL;
        }
    }
    /* Clear "HUD visible" bit immediately when the new stage has no
 * panel — needed BEFORE play_demo_scene runs (which clears it again
 * later) because LoadKomnata (called from LoadKomnataScene inside
 * play_demo_scene's prologue) executes 2 embedded ProcessGameFrameTick
 * iterations as part of its enter_va + second_va run. Those ticks
 * call PaintHudOverlay which gates portrait + pasek paints on this
 * bit only (not on g_panel_asset). Leaving the bit at the previous
 * stage's value = 1 would draw stale portraits + health bar for two
 * frames before play_demo_scene's `&= ~1u` later in the prologue —
 * visible as the "HUD flash" right after picking the ACME button. */
    if (!g_stage->panel_wyc) g_settings_anim_active &= ~1u;
    /* load palette */
    if (g_stage->paleta_pal) {
        void *pal = NULL; uint32_t n;
        if (!LoadFileFromDta(g_stage->paleta_pal, &pal, &n)) return 0;
        memcpy(g_palette_rgb, pal, sizeof g_palette_rgb);
    }
    /* enter the start room */
    g_cur_komnata = g_stage->start_komnata;
    PaletteFadeInOut(100, g_palette_rgb, 0, 0, NULL);
    return 1;
}

/* ------------------------------------------------------------------------- *
 * RunMenuScene — 0x0040B5E0
 *
 * A condensed, faithful implementation; the original was much more verbose.
 * ------------------------------------------------------------------------- */
extern AnimAsset *LoadAssetFromDtaBase(const char *);

/* ------------------------------------------------------------------------- *
 * HandleMainMenuClick — port of (the on_click callback for the
 * main menu's SceneDef at ).
 *
 * Param 1 is a trigger value:
 * 0 INIT — load palettes (Tlo.pal/menu.pal) and start CD-audio
 * playback of Dane_01.dta (the menu music).
 * 0x12 Load-game submenu (returns 2/3/5)
 * 0x13 New game (returns 6)
 * 0x14 Options/save toggle (sets g_save_request)
 * 0x15 Quit (returns 8)
 * 0x16 Credits (returns 9)
 *
 * Per-frame work runs regardless of trigger: advance the title-animation
 * frame and blit it. We don't yet have CD-audio or the full animation
 * pipeline, so this is reduced to the dispatch-only logic.
 */
static uint8_t s_menu_flags    = 0;   /* */
static int     s_anim_delay    = 0;   /* anim_delay_counter (anim delay counter) */
static int     s_anim_frame    = 10;  /* anim_frame_index (anim frame index) */
static int     s_save_request  = 0;   /* */

/* Forward decls for the menu-BG snapshot used by RunMenuScene overlay
 * branch (see comments at the definitions below near paint_slot_list).
 * Declared early because RunMenuScene at 0x0040B5E0 lives above the
 * save/load menu code that owns them. */
/* SAVE_THUMB_W / SAVE_THUMB_H moved to wacki.h. Non-static so
 * src/menu/slot_picker.c can read it from the save-commit path
 * (CapturePendingThumbnail writes here before opszyns opens). */
uint8_t g_save_thumb_pending[SAVE_THUMB_W * SAVE_THUMB_H];

/* g_menu_bg_snapshot + SnapshotBackbufferForMenu moved to src/menu/
 * menu_loop.c (tightly coupled with paint_menu_background). */
extern void SnapshotBackbufferForMenu(void);

/* "asset (1, 10)" hook used by HandleMainMenuClick — set in RunMenuScene
 * to the mask atlas asset (matching the engine's RegisterEntityForUpdate
 * call at 0x0040B5E0). */
AnimAsset *g_menu_asset_10 = NULL;

/* ---- Title + main-menu constants ---------------------------------- */

/* Title-screen filenames (Tlo.pal background palette, Tlo.wyc button
 * mask, menu.pal blended-fade buffer the original cross-fades into,
 * Dane_01.dta looping menu BGM). */
#define TITLE_PALETTE_FILENAME          "Tlo.pal"
#define TITLE_MASK_FILENAME             "Tlo.wyc"
#define TITLE_BLEND_PALETTE_FILENAME    "menu.pal"
#define TITLE_INTRO_AVI                 "Dane_10.dta"
#define CREDITS_AVI                     "Dane_12.dta"
#define MAIN_MENU_BGM_FILENAME          "Dane_01.dta"

/* Title-screen button triggers (Tlo.wyc mask). Frames in the .wyc are
 * laid out so that hover frame = def frame + TITLE_HOVER_FRAME_OFFSET.
 * Note MALUCH (0x14) is the "click the Maluch to start a new game"
 * button — an earlier RE annotation called it "Options" but the
 * dispatch (s_save_request → rc=PROLOGUE) makes it the prologue start. */
#define TITLE_BTN_LOAD                  0x12
#define TITLE_BTN_NEW                   0x13
#define TITLE_BTN_MALUCH                0x14
#define TITLE_BTN_QUIT                  0x15
#define TITLE_BTN_CREDITS               0x16
#define TITLE_HOVER_FRAME_OFFSET        5
#define TITLE_BUTTON_COUNT              5

/* The engine's "neutral / no verb" sentinel — sprinkled through every
 * SceneDef-walking helper and through HandleSceneInput. 0x26 was the
 * original engine's default fall-through value for verb dispatch. */
#define SCENE_NEUTRAL_VERB              0x26

/* HandleMainMenuClick trigger sentinel: 0 = INIT (one-time per-frame
 * setup pass when the title is first entered). */
#define MAIN_MENU_TRIGGER_INIT          0

/* HandleMainMenuClick return codes. RunMainGameLoop's switch dispatches
 * on them; BACK_TO_MAIN keeps the inner loop running. */
#define MAIN_MENU_RC_NONE               0
#define MAIN_MENU_RC_BACK_TO_MAIN       2   /* Load cancel → re-enter title */
#define MAIN_MENU_RC_QUIT_CONFIRM_A     4
#define MAIN_MENU_RC_QUIT_CONFIRM_B     8
#define MAIN_MENU_RC_LOAD_SAVE          5
#define MAIN_MENU_RC_NEW_GAME           6
#define MAIN_MENU_RC_PROLOGUE           7
#define MAIN_MENU_RC_CREDITS            9
#define MAIN_MENU_RC_HARD_QUIT          99

/* WACKI-logo flipbook in the title's mask atlas (sel_guz.wyc). Frames
 * 10..(count-1) form the animated logo; FRAME_DOORS_FIRST..DOORS_LAST
 * are the "doors closing" pose during which the Maluch-click latch
 * is NOT honoured (let the doors finish before transitioning). */
#define MAIN_MENU_ANIM_FIRST_FRAME      10
#define MAIN_MENU_ANIM_DOORS_FIRST      0x0F
#define MAIN_MENU_ANIM_DOORS_LAST       0x12
#define MAIN_MENU_ANIM_TICKS_PER_FRAME  6

/* s_menu_flags bit 0 — set on every HandleMainMenuClick call and
 * cleared once the rc has switched to a "leave the menu" code. */
#define MAIN_MENU_FLAG_LATCH            0x01u

/* Pytanie (Y/N) quit-confirm. */
#define PYTANIE_TRIGGER_TAK             0x12
#define PYTANIE_TRIGGER_NIE             0x13
#define PYTANIE_RC_TAK                  3
#define PYTANIE_RC_NIE                  4
#define PYTANIE_FRAME_NONE              0xFFFF

/* RunGameStageLoop flag bits. */
#define STAGE_LOAD_FLAG_FULL_RESET      0x02
#define STAGE_LOAD_FLAG_SAVE_LOAD       0x10

/* Entity-state table layout (flag-2 FULL RESET only zeroes the
 * in_inventory_flag field per entry, preserving panel_verb_id). */
#define ENTITY_STATE_ENTRY_COUNT        0x8E
#define ENTITY_STATE_FIELDS_PER_ENTRY   4
#define ENTITY_STATE_INVENTORY_FIELD    1

/* g_game_over_code progress signals + the "user-confirmed quit to main
 * menu" sentinel set by ESC / F12→TAK / OPCJE→Quit. Anything other than
 * NONE / DEATH / CHAPTER_PICK / STAGE_END_AVI is treated as a user-quit
 * intent by the dev start-stage flow. */
#define GAME_OVER_NONE                  0
#define GAME_OVER_DEATH                 1
#define GAME_OVER_USER_QUIT             2
#define GAME_OVER_CHAPTER_PICK          3
#define GAME_OVER_STAGE_END_AVI         4

/* Dev-mode --start-stage clamps + the "Monter finale" pick value. */
#define DEV_START_STAGE_MIN             1
#define DEV_START_STAGE_MAX             5
#define DEV_PICK_FINALE                 5

/* ------------------------------------------------------------------------- *
 * HandlePytanieClick — on_click for the "Pytanie?" quit-confirmation
 * SceneDef. The two buttons are TAK (yes, quit) and NIE (no, keep
 * playing); the caller treats PYTANIE_RC_TAK as "quit confirmed". */
static int HandlePytanieClick(int trigger)
{
    if (trigger == PYTANIE_TRIGGER_TAK) return PYTANIE_RC_TAK;
    if (trigger == PYTANIE_TRIGGER_NIE) return PYTANIE_RC_NIE;
    return MAIN_MENU_RC_NONE;
}

/* Install the Tlo.pal primary + load menu.pal into the secondary
 * (blend-target) palette buffer. The original engine cross-fades
 * between the two on entry; the port doesn't have the blend pipeline
 * wired so we just free menu.pal to avoid a leak. */
static void install_main_menu_palettes(void)
{
    void    *pal = NULL;
    uint32_t psz = 0;
    if (LoadFileFromDta(TITLE_PALETTE_FILENAME, &pal, &psz) && pal) {
        InstallPalette((uint8_t *)pal, 0);
        xfree(pal);
    }
    if (LoadFileFromDta(TITLE_BLEND_PALETTE_FILENAME, &pal, &psz) && pal) {
        xfree(pal);
    }
}

/* INIT-trigger handler — runs once when the title is first entered:
 * clear the backbuffer, install both palettes, reset the flipbook
 * counters, and start the looping menu BGM. */
static void enter_main_menu(void)
{
    FlipBuffersClearWith(0);
    FlushFrameToPrimary();
    install_main_menu_palettes();
    s_anim_frame   = MAIN_MENU_ANIM_FIRST_FRAME;
    s_anim_delay   = 0;
    s_save_request = 0;
    PlayMenuMusic(MAIN_MENU_BGM_FILENAME, 1);
}

/* TITLE_BTN_LOAD handler — runs the Load-game sub-menu and translates
 * its rc into the appropriate main-menu rc. */
static int dispatch_load_submenu(void)
{
    extern SceneDef g_load_menu_scene;
    /* Snapshot the title-screen backbuffer so the Load overlay's
     * margins show the title art instead of an uninitialised buffer
     * (palette index 0 in the new palette can be white/garbage). */
    SnapshotBackbufferForMenu();
    int r = RunMenuScene(1, &g_load_menu_scene);
    return (r == 3) ? MAIN_MENU_RC_LOAD_SAVE : MAIN_MENU_RC_BACK_TO_MAIN;
}

/* Returns true once the Maluch-click latch is set AND the title
 * flipbook is OUTSIDE the "doors closing" frames — when both hold,
 * HandleMainMenuClick returns rc=PROLOGUE so RunMainGameLoop starts
 * a new playthrough. */
static int maluch_latch_ready_to_fire(void)
{
    return s_save_request &&
           (s_anim_frame <  MAIN_MENU_ANIM_DOORS_FIRST ||
            s_anim_frame >  MAIN_MENU_ANIM_DOORS_LAST);
}

/* Advance the title-screen WACKI-logo flipbook by one frame (or wait
 * for the per-frame tick countdown). Frames 10..(count-1) form the
 * animated logo painted with colour-key 0 so the buttons underneath
 * remain visible. */
static void tick_title_animation(void)
{
    AnimAsset *a = g_menu_asset_10;
    if (a && s_anim_delay < 1) {
        if (s_anim_frame >= a->frame_count)
            s_anim_frame = MAIN_MENU_ANIM_FIRST_FRAME;
        if (s_anim_frame < a->frame_count && a->pixel_ptrs[s_anim_frame]) {
            uint16_t w  = a->off_widths [s_anim_frame];
            uint16_t h  = a->off_heights[s_anim_frame];
            uint16_t dx = a->off_drawX  [s_anim_frame];
            uint16_t dy = a->off_drawY  [s_anim_frame];
            static int once = 0;
            if (once < 5) {
                fprintf(stderr, "[anim] frame=%d at (%u,%u) %ux%u\n",
                        s_anim_frame, dx, dy, w, h);
                ++once;
            }
            BlitSpriteToBackbuffer(dx, dy, 0, 0, w, h, w, h,
                                   a->pixel_ptrs[s_anim_frame], 0);
        }
        ++s_anim_frame;
        /* ~10 fps at the 60 fps menu pacing — one new frame every
         * MAIN_MENU_ANIM_TICKS_PER_FRAME (≈100 ms). */
        s_anim_delay = MAIN_MENU_ANIM_TICKS_PER_FRAME;
    } else {
        --s_anim_delay;
    }
}

static int HandleMainMenuClick(int trigger)
{
    int rc = MAIN_MENU_RC_NONE;
    s_menu_flags |= MAIN_MENU_FLAG_LATCH;

    switch (trigger) {
    case MAIN_MENU_TRIGGER_INIT:
        enter_main_menu();
        break;

    case TITLE_BTN_LOAD:
        rc = dispatch_load_submenu();
        break;

    case TITLE_BTN_NEW:
        rc = MAIN_MENU_RC_NEW_GAME;
        s_menu_flags &= ~MAIN_MENU_FLAG_LATCH;
        break;

    case TITLE_BTN_MALUCH:
        s_save_request = 1;
        break;

    case TITLE_BTN_QUIT:
        rc = MAIN_MENU_RC_QUIT_CONFIRM_B;
        break;

    case TITLE_BTN_CREDITS:
        rc = MAIN_MENU_RC_CREDITS;
        break;
    }

    /* Maluch-latch trailing block: once s_save_request is set AND the
     * title flipbook is outside the "doors closing" pose, return rc=
     * PROLOGUE so RunMainGameLoop starts a new playthrough. The frame
     * gate gives the title animation time to finish its rotation. */
    if (maluch_latch_ready_to_fire()) {
        rc = MAIN_MENU_RC_PROLOGUE;
        s_menu_flags &= ~MAIN_MENU_FLAG_LATCH;
        s_anim_delay = 1;
    }

    if (trigger > MAIN_MENU_TRIGGER_INIT) {
        fprintf(stderr, "[menu] click trigger=0x%02X rc=%d\n", trigger, rc);
    }

    tick_title_animation();

    /* Any non-zero rc means we're leaving the menu — stop the BGM so
     * it doesn't bleed into the next scene. */
    if (rc != MAIN_MENU_RC_NONE) StopMenuMusic();
    return rc;
}

/* Decode a .pic ("RAWB") backbuffer:
 * +0 uint32 magic = 'RAWB'
 * +4 uint16 width (LE)
 * +6 uint16 height (LE)
 * +8 uint8 palette[256*3]
 * +776 uint8 pixels[w*h]
 *
 * → : the image is centered using
 * drawX = (640 - w) / 2, drawY = (400 - h) / 2 (clipped to ≥ 0).
 * For fullscreen 640×480 that's (0, 0); for the 344×319 Pytanie dialog
 * that's (148, 40), matching the on-disk button coords (194,287) / (354,292).
 *
 * `as_overlay`:
 * 0 — fullscreen scene background: install the embedded palette
 * and paint opaque (the original raw-copy path).
 * 1 — dialog overlay: keep current palette and color-key index 0
 * so the underlying menu shows through (matches 
 * mode 0 — alpha-blend with transparent-on-0).
 * Returns 1 if it painted, 0 otherwise.
 */
/* MergePalette — fold a .pic file's embedded palette into the live one,
 * preserving entries the .pic has black (0,0,0) for.
 *
 * Why the merge (and not a straight InstallPalette like the original):
 *
 * The shipped scene .pic files only fill ~70 of 256 palette indices
 * with real colours; the other ~180 are (0,0,0) — placeholder. The
 * .pic's BG pixels only use those ~70 "filled" indices. But the SPRITE
 * assets loaded into the same scene (drut, barstoi, pies, pijaki,
 * kufle, …) use a much wider range that paleta.pal sets up — including
 * the earth-tone / green ramp at indices 32..47 (greens 32-39, yellow
 * 40, oranges 41-46, red 47), the pink/violet ramp at 23-25, the
 * teal/blue ramp at 53-55, etc. If we naïvely InstallPalette the
 * .pic header, those sprite colours get overwritten with (0,0,0) and
 * the corresponding sprite pixels render BLACK on the user's screen.
 *
 * The original engine never installs the .pic palette directly — it
 * loads the .pic as a kind=3 entity and its renderer
 * leaves the live palette alone. So in practice paleta.pal stays the
 * active palette across all scenes, and per-room .pic palettes are
 * only there as a hint of which indices the artist actually used.
 *
 * Our port doesn't go through the entity-rendered BG path; we paint
 * the .pic pixels directly. So we MUST keep the .pic palette merged
 * in for any indices it sets that paleta.pal doesn't (kiosk21 fills
 * 11 new indices at 162..172, plac fills 16 more), while still
 * preserving paleta.pal entries where the .pic has black.
 *
 * This is a one-line merge: for each of the 256 RGB triplets in the
 * .pic palette, only overwrite g_palette_rgb if the .pic entry is
 * non-black. */
static void MergePalette(const uint8_t *src256_rgb)
{
    extern uint8_t g_palette_rgb[256*3];
    for (int i = 0; i < 256; ++i) {
        uint8_t r = src256_rgb[i*3 + 0];
        uint8_t g = src256_rgb[i*3 + 1];
        uint8_t b = src256_rgb[i*3 + 2];
        if (r | g | b) {
            g_palette_rgb[i*3 + 0] = r;
            g_palette_rgb[i*3 + 1] = g;
            g_palette_rgb[i*3 + 2] = b;
        }
    }
}

int paint_rawb_pic(const void *blob, uint32_t size, int as_overlay)
{
    if (size <= 776) return 0;
    const uint8_t *p = (const uint8_t *)blob;
    if (p[0]!='R' || p[1]!='A' || p[2]!='W' || p[3]!='B') return 0;
    uint16_t w = (uint16_t)(p[4] | (p[5] << 8));
    uint16_t h = (uint16_t)(p[6] | (p[7] << 8));
    if ((uint32_t)w * h + 776u > size) return 0;
    int dx = (WACKI_SCREEN_W - (int)w) / 2;
    int dy = (400              - (int)h) / 2;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    if (!as_overlay) {
        /* Fullscreen bg: MERGE palette (not overwrite), then opaque blit.
 * See MergePalette comment for the why — naïve install kills
 * the sprite-shared earth/green/orange colour ramps. */
        MergePalette(p + 8);
        PaintImageToBackbuffer((uint16_t)dx, (uint16_t)dy, w, h, p + 776);
    } else {
        /* Dialog overlay: color-key 0. Merge the .pic's palette so the
 * UI-color indices used by paint_slot_list (0x12 for slot text,
 * 0x01 for inset bg, 0xFE for inline-edit) resolve to whatever
 * the .pic's artist intended — otherwise 0x12 picks up whichever
 * palette happens to be active (red in menu.pal, dark blue in
 * gameplay palette), and the menu looks different depending on
 * where it was opened from. MergePalette only overwrites
 * non-black entries, so the underlying gameplay snapshot in the
 * margins keeps most of its colours. */
        MergePalette(p + 8);
        BlitSpriteToBackbuffer((uint16_t)dx, (uint16_t)dy, 0, 0,
                               w, h, w, h, (uint8_t *)(p + 776), 0);
    }
    return 1;
}

/* Per-asset scratch buffer for RLE-decoded frames (kind=3 rich ANIM).
 * Sized to the asset's max bounding box (max_w * max_h). Released by the
 * caller via paint_anim_release. */
static uint8_t *s_rle_scratch  = NULL;
static int      s_rle_scratch_sz = 0;

static uint8_t *get_rle_scratch(int sz)
{
    if (sz <= s_rle_scratch_sz) return s_rle_scratch;
    free(s_rle_scratch);
    s_rle_scratch    = (uint8_t *)malloc((size_t)sz);
    s_rle_scratch_sz = s_rle_scratch ? sz : 0;
    return s_rle_scratch;
}

/* Blit one frame of an ANIM atlas using its embedded hot-spot.
 * kind=3 ("rich") frames are RLE-compressed → decode into the scratch
 * buffer first, then blit raw. kind=2 ("passive") frames are already raw. */
void paint_anim_button_at(AnimAsset *atlas, uint16_t frame,
                          int16_t override_dx, int16_t override_dy,
                          int use_override)
{
    if (!atlas || frame >= atlas->frame_count || !atlas->pixel_ptrs) return;
    uint16_t w  = atlas->off_widths [frame];
    uint16_t h  = atlas->off_heights[frame];
    uint16_t dx = use_override ? (uint16_t)override_dx : atlas->off_drawX[frame];
    uint16_t dy = use_override ? (uint16_t)override_dy : atlas->off_drawY[frame];
    uint8_t *px = atlas->pixel_ptrs[frame];
    if (!px || w == 0 || h == 0) return;

    if (atlas->kind == 3) {
        int need = (int)w * (int)h;
        uint8_t *scratch = get_rle_scratch(need);
        if (!scratch) return;
        DepackRleFrame(px, scratch, need);
        px = scratch;
    }
    /* mode 0 = colour-key 0 (transparent) */
    BlitSpriteToBackbuffer(dx, dy, 0, 0, w, h, w, h, px, 0);
}

/* ------------------------------------------------------------------------- *
 * play_bomb_explosion — visual port of the bomb cutscene. The original
 * Quit-TAK ( with &). The real script is a Wacky.scr
 * bytecode sequence that drives the menu's bomb icon — fuse burns, then
 * BOOM, then fade-out — before the process exits.
 *
 * The fullscreen flipbook for this is **bomba.wyc**: 20 frames of raw
 * 640×480 (kind=2), each at (0,0). Frame 0 holds the menu title with the
 * bomb's fuse lit; mid-frames show the blooming fireball; frame 19 is a
 * fade-to-white. We play it linearly with the wybuch.wav SFX once the
 * fuse is roughly through (start audio at ~frame 8 instead of frame 0
 * so the bang lines up with the visible explosion).
 * ------------------------------------------------------------------------- */
static void play_bomb_explosion(void)
{
    StopMenuMusic();

    AnimAsset *a = LoadAssetFromDtaBase("bomba.wyc");
    int started_fuse = 0, played_bang = 0;
    if (a && a->frame_count > 0) {
        for (uint16_t f = 0; f < a->frame_count; ++f) {
            if (PlatformShouldQuit()) break;
            PumpWin32Messages();
            /* bomba.wyc frames are fullscreen at (0,0) — paint raw. */
            paint_anim_button_at(a, f, 0, 0, 1);
            FlushFrameToPrimary();
            /* Sound timing:
 * frame 0: start lont.wav (burning fuse, 1.30 s mono) — runs
 * until wybuch overrides it.
 * frame 8: start wybuch.wav (explosion, 2.53 s) — implicitly
 * stops lont via PlayMenuMusic's StopMenuMusic call,
 * so the bang cleanly cuts the fuse hiss.
 * Both SFX live inside Dane_02.dta; PlayMenuMusic's .dta
 * fallback handles loading. */
            if (!started_fuse) {
                PlayMenuMusic("lont.wav", 0);
                started_fuse = 1;
            }
            if (!played_bang && f >= 8) {
                /* Wacky.scr [animacja]Bomba.wyc → [sampl] Bum.wav (7,)
 * means "play bum.wav from frame 7 onwards" — that's the
 * actual asset (wybuch.wav is the in-game level-explosion,
 * different SFX). */
                PlayMenuMusic("bum.wav", 0);
                played_bang = 1;
            }
            TickMenuMusic();
            SDL_Delay(100);                     /* ~10 fps */
        }
        FreeAsset(a);
    }
    SDL_Delay(800);                             /* let the bang fully tail */
    StopMenuMusic();
}

/* ------------------------------------------------------------------------- *
 * RunMainGameLoop — 0x0040BBF0
 *
 *'s two-loop structure:
 * // either way: inner loop CONTINUES (rc2==4 → menu)
 *
 * Crucially: NIE from Pytanie does NOT replay the intro — control returns
 * to the inner do/while which re-enters RunMenuScene with the main menu.
 * Only "New game" (case 6) breaks the inner loop and the outer re-plays AVI.
 * ------------------------------------------------------------------------- */
extern void LoadSaveStateOrInitialize(void);

/* fwd-decl — defined after RunMainGameLoop so it can see all helpers.
 * T39 removed play_first_scene_demo (body inlined into RunGameStageLoop). */
static void play_fiacik_intro(void);
static void play_loading_screen(void);

/* DEV (Fix #22 / Bug 7 helper): if set via --start-stage N or
 * WACKI_START_STAGE=N CLI/env, RunMainGameLoop skips the intro AVI +
 * main menu and jumps straight into stage N gameplay with stages
 * 1..(N-1) marked completed. 0 = normal flow. */
int g_dev_start_stage = 0;

/* ---- RunMainGameLoop constants ------------------------------------ */

/* Title/main-menu constants moved to the top of the file (above
 * HandlePytanieClick + HandleMainMenuClick) so both early click
 * handlers and the late RunMainGameLoop builder share one source of
 * truth. See the "Title + main-menu constants" block above. */

/* Build the title-screen SceneDef (Tlo.wyc mask + HandleMainMenuClick). */
static SceneDef make_title_scene(void)
{
    SceneDef s = {
        .background_pic = NULL,
        .mask_file      = TITLE_MASK_FILENAME,
        .on_click       = HandleMainMenuClick,
        .button_count   = TITLE_BUTTON_COUNT,
        .flags          = SCENE_FLAG_FORCE_CB,
        .buttons = {
            { .id = TITLE_BTN_LOAD,    .def_anim = 0,
              .hover_anim = TITLE_HOVER_FRAME_OFFSET + 0 },
            { .id = TITLE_BTN_NEW,     .def_anim = 1,
              .hover_anim = TITLE_HOVER_FRAME_OFFSET + 1 },
            { .id = TITLE_BTN_MALUCH,  .def_anim = 2,
              .hover_anim = TITLE_HOVER_FRAME_OFFSET + 2 },
            { .id = TITLE_BTN_QUIT,    .def_anim = 3,
              .hover_anim = TITLE_HOVER_FRAME_OFFSET + 3 },
            { .id = TITLE_BTN_CREDITS, .def_anim = 4,
              .hover_anim = TITLE_HOVER_FRAME_OFFSET + 4 },
        },
    };
    return s;
}

/* Build the Pytanie Y/N quit-confirm SceneDef. */
static SceneDef make_pytanie_scene(void)
{
    SceneDef s = {
        .background_pic = "Pytanie.pic",
        .mask_file      = "Pytanie.wyc",
        .on_click       = HandlePytanieClick,
        .button_count   = 2,
        .flags          = SCENE_FLAG_MOUSE_ONLY,
        .buttons = {
            { .id = PYTANIE_TRIGGER_TAK,
              .def_anim = PYTANIE_FRAME_NONE, .hover_anim = 0 },
            { .id = PYTANIE_TRIGGER_NIE,
              .def_anim = PYTANIE_FRAME_NONE, .hover_anim = 1 },
        },
    };
    return s;
}

/* Install the Tlo.pal background palette. Done here because we haven't
 * ported the original InitializeStage that does it on engine boot. */
static void install_title_palette(void)
{
    void    *pal = NULL;
    uint32_t psz = 0;
    if (LoadFileFromDta(TITLE_PALETTE_FILENAME, &pal, &psz) && pal) {
        InstallPalette((uint8_t *)pal, 0);
        xfree(pal);
    }
}

/* Mirror RunGameStageLoop's flag-2 FULL RESET cleanup: zero script vars,
 * clear each entity_state[i].in_inventory_flag (preserving the
 * panel_verb_id identity mapping), reset the inventory state. */
static void apply_full_reset(void)
{
    memset(g_script_vars, 0, sizeof g_script_vars);
    uint16_t *es = (uint16_t *)g_entity_state;
    for (int idx = 0; idx < ENTITY_STATE_ENTRY_COUNT; ++idx) {
        es[idx * ENTITY_STATE_FIELDS_PER_ENTRY +
           ENTITY_STATE_INVENTORY_FIELD] = 0;
    }
    ResetInventory();
}

/* Mask of "stage prior to N is completed" bits used to seed the
 * dev-mode chapter-select map. (1<<(N-1)) - 1 = bits 0..N-2. */
static uint32_t dev_completed_mask_for_stage(int n)
{
    return (uint32_t)((1u << (n - 1)) - 1u);
}

/* g_game_over_code values 0/1/3/4 mean "stage progressed normally" —
 * anything else (2 = ESC/F12 quit, 99 = hard-quit, unknown codes)
 * means the user is done with the dev session. */
static int game_over_is_progress_signal(int code)
{
    return code == GAME_OVER_NONE
        || code == GAME_OVER_DEATH
        || code == GAME_OVER_CHAPTER_PICK
        || code == GAME_OVER_STAGE_END_AVI;
}

/* Forward-declared because g_sel_tlo_scene + helpers live further down
 * the file (in the sel_tlo.pic chapter-select section). */
extern SceneDef g_sel_tlo_scene;
extern int      s_chapter_pick;
extern int      SelTloRefreshButtons(void);

/* DEV --start-stage N: skip menu+intro, show chapter-select map with
 * stages 1..(N-1) marked completed. User picks stage from the map,
 * then that stage runs normally. Loop so that returning from one
 * stage re-shows the map (e.g. ESC out of stage 2 → back to map).
 * Returns 1 if the dev flow handled the run (RunMainGameLoop should
 * return), 0 if dev mode is off and the normal flow should proceed. */
static int run_dev_start_stage_flow(void)
{
    if (g_dev_start_stage < DEV_START_STAGE_MIN ||
        g_dev_start_stage > DEV_START_STAGE_MAX) return 0;

    int N = g_dev_start_stage;
    apply_full_reset();
    g_completed_stages = dev_completed_mask_for_stage(N);
    fprintf(stderr, "[wacki] dev-start: chapter-select map, "
                    "completed_mask=0x%X (stages 1..%d done)\n",
            g_completed_stages, N - 1);

    while (!PlatformShouldQuit()) {
        (void)SelTloRefreshButtons();
        s_chapter_pick = 0;
        RunMenuScene(0, &g_sel_tlo_scene);
        if (PlatformShouldQuit()) return 1;

        if (s_chapter_pick < 1 || s_chapter_pick > DEV_PICK_FINALE) {
            fprintf(stderr, "[wacki] dev-start: no stage picked — exit\n");
            return 1;
        }
        fprintf(stderr, "[wacki] dev-start: stage %d picked from map\n",
                s_chapter_pick);
        if (!LoadStage((uint16_t)s_chapter_pick)) {
            fprintf(stderr, "[wacki] dev-start: LoadStage(%d) failed\n",
                    s_chapter_pick);
            continue;
        }

        int played_stage = s_chapter_pick;
        RunGameStageLoop(STAGE_LOAD_FLAG_SAVE_LOAD);

        /* Stage 5 (Monter finale) is terminal — the original returns
         * to the main-menu loop after the credits sting. In dev mode
         * the title was skipped on startup, so the equivalent
         * terminal action is to return from the dev loop entirely. */
        if (played_stage == DEV_PICK_FINALE) {
            fprintf(stderr, "[wacki] dev-start: Monter finale complete "
                            "→ exit (= main menu in normal flow)\n");
            return 1;
        }

        /* Loop back to the map only on stage-progress signals; on an
         * explicit user quit (game_over_code 2, 99, or any unknown
         * value) bail back to the OS instead of bouncing to sel_tlo. */
        if (!game_over_is_progress_signal(g_game_over_code)) {
            fprintf(stderr, "[wacki] dev-start: game_over=%d → exit\n",
                    g_game_over_code);
            return 1;
        }

        /* Re-mark all stages prior to N as completed so the map shows
         * progress even if the played stage cleared a bit via script. */
        g_completed_stages |= dev_completed_mask_for_stage(N);
    }
    return 1;
}

/* Pytanie Y/N quit-confirm cascade — runs the menu, bombs + returns 1
 * on TAK, returns 0 on NIE. Caller breaks out of the menu re-entry
 * loop on TAK. */
static int prompt_quit_with_bomb(void)
{
    SceneDef q = make_pytanie_scene();
    int rc = RunMenuScene(1, &q);
    if (rc == PYTANIE_RC_TAK) {
        play_bomb_explosion();
        return 1;
    }
    return 0;
}

/* Dispatch one return code from RunMenuScene(title). Returns 1 if the
 * outer / inner loop should keep running, 0 if the caller should
 * break out of the inner loop (back to outer's intro replay). */
static int dispatch_main_menu_rc(int rc, int *should_return)
{
    *should_return = 0;
    switch (rc) {
    case MAIN_MENU_RC_QUIT_CONFIRM_A:
    case MAIN_MENU_RC_QUIT_CONFIRM_B:
        if (prompt_quit_with_bomb()) {
            *should_return = 1;
            return 0;
        }
        return 1;

    case MAIN_MENU_RC_LOAD_SAVE: {
        /* LoadSaveSlot restores g_cur_komnata + g_script_vars +
         * g_entity_state from Wacki.sav slot N. */
        extern uint16_t g_selected_save_slot;
        LoadSaveSlot(g_selected_save_slot);
        RunGameStageLoop(STAGE_LOAD_FLAG_SAVE_LOAD);
        return 1;
    }

    case MAIN_MENU_RC_NEW_GAME:
        /* Film-reel button — the original runs an intro script which
         * plays the credits/film cutscene. The VM isn't wired to
         * assets here, so break the inner loop and let the outer
         * replay the title intro AVI (Dane_10.dta). */
        return 0;

    case MAIN_MENU_RC_PROLOGUE:
        /* [etap]1 [komnata]init prologue:
         *   1. fiacik.wyc Maluch driving across the title
         *   2. load.pic / load.wyc "Lołding" progress bar
         *   3. hand off to RunGameStageLoop(FULL_RESET). */
        play_fiacik_intro();
        play_loading_screen();
        RunGameStageLoop(STAGE_LOAD_FLAG_FULL_RESET);
        return 0;

    case MAIN_MENU_RC_CREDITS:
        PlaySceneCutsceneAvi(CREDITS_AVI);
        return 1;
    }
    return 1;
}

void RunMainGameLoop(void)
{
    LoadSaveStateOrInitialize();

    /* Push Wacki.sav settings into in-memory s_opt_* + audio mixer.
     * Without this the saved prefs were loaded into g_save.settings
     * but never had any effect — opszyns always opened with all-on
     * regardless of what the user toggled last session. */
    ApplySavedSettings();
    install_title_palette();

    if (run_dev_start_stage_flow()) return;

    SceneDef title = make_title_scene();

    /* OUTER: each iteration plays the title intro then drives the menu
     * re-entry inner loop. INNER: each iteration shows the title menu
     * and dispatches one click. */
    while (1) {
        if (PlatformShouldQuit()) return;
        PlaySceneCutsceneAvi(TITLE_INTRO_AVI);

        int inner = 1;
        while (inner) {
            int rc = RunMenuScene(1, &title);
            if (PlatformShouldQuit() || rc == MAIN_MENU_RC_HARD_QUIT) return;

            int should_return = 0;
            inner = dispatch_main_menu_rc(rc, &should_return);
            if (should_return) return;
            if (g_game_over_code > GAME_OVER_STAGE_END_AVI) return;
        }
    }
}

/* ------------------------------------------------------------------------- *
 * play_fiacik_intro — Maluch driving away across the title screen.
 *
 *scr [etap]1 [komnata]init line:
 * [animacja] fiacik.wyc
 * [sampl] fiacik.wav (0,20)
 *
 * fiacik.wyc is a 10-frame raw atlas (kind=2). Each frame has its own
 * (drawX, drawY) hot-spot — frame 0 at (201,244), frame 5 at (16,247),
 * frame 9 at (429,404) (off-screen end-marker). The Maluch slides from
 * the right edge of the title to the left. fiacik.wav (engine sound, ~1s)
 * plays alongside the visible motion.
 *
 * The animation overlays the title screen (we keep the WACKI logo
 * underneath, just paint the car on top with color-key 0). This matches
 * the bomb-explosion structure user already recognised.
 * ------------------------------------------------------------------------- */
static void play_fiacik_intro(void)
{
    StopMenuMusic();
    PlayMenuMusic("fiacik.wav", 0);

    /* The menu loop exited with the HOVER overlay still composited onto
 * the shadow — Maluch's hover frame 7 is 105×60 @ (213,241), which
 * sticks out 3 px to the LEFT of mal_back's 113×74 patch @ (216,228).
 * That sliver would survive mal_back and show up as the "yellow trash"
 * the user reported.
 *
 * IMPORTANT: RunMenuScene's cleanup did `FreeAsset(buttons); g_menu_
 * asset_10 = NULL;` BEFORE we got here — so we have to re-load Tlo.wyc
 * locally; relying on g_menu_asset_10 silently no-ops the re-render
 * and leaves the hover sprite on the shadow.
 *
 * Re-render the menu cleanly first (Tlo flipbook fullscreen wipe +
 * def_anim buttons only, no hover) so the snapshot starts from the
 * exact same state as the engine's pre-fiacik tick. Equivalent to the
 * original which despawns the hover entity before the script runs. */
    AnimAsset *bg = LoadAssetFromDtaBase("Tlo.wyc");
    if (bg && bg->pixel_ptrs) {
        int bgf = s_anim_frame;
        if (bgf < 10) bgf = 10;
        if (bgf >= bg->frame_count) bgf = bg->frame_count - 1;
        if (bg->pixel_ptrs[bgf])
            BlitSpriteToBackbuffer(
                bg->off_drawX[bgf], bg->off_drawY[bgf], 0, 0,
                bg->off_widths[bgf], bg->off_heights[bgf],
                bg->off_widths[bgf], bg->off_heights[bgf],
                bg->pixel_ptrs[bgf], 1);                  /* opaque wipe */
        /* def_anim only for buttons 0..4 (no hover overlay) */
        for (uint16_t i = 0; i < 5; ++i)
            paint_anim_button(bg, (uint16_t)i);
    }

    /* Per prologue script @ 0x00427af8 the original engine spawns TWO
 * entities for this animation:
 * LOAD_ASSET id=0x01 → mal_back.wyc (113×74 orange patch @216,228)
 * LOAD_ASSET id=0x3C → fiacik.wyc
 * mal_back covers the def_anim "yellow car" button so the Fiat can
 * drive across without leaving the icon behind. */
    AnimAsset *mal_back = LoadAssetFromDtaBase("mal_back.wyc");
    if (mal_back) {
        paint_anim_button_at(mal_back, 0, 0, 0, 0);   /* atlas hot-spot = 216,228 */
        FreeAsset(mal_back);
    }

    /* Snapshot the (now button-erased) menu shadow so we can restore it
 * cleanly under each fiacik frame. equivalent of the engine's
 * RestorePrevFrameRects — no per-frame ghosting. */
    extern uint8_t *g_back_shadow;
    size_t shadow_bytes = (size_t)WACKI_SCREEN_W * WACKI_SCREEN_H;
    uint8_t *snapshot = (uint8_t *)malloc(shadow_bytes);
    if (snapshot && g_back_shadow) memcpy(snapshot, g_back_shadow, shadow_bytes);

    AnimAsset *a = LoadAssetFromDtaBase("fiacik.wyc");
    if (a && a->frame_count > 0) {
        for (uint16_t f = 0; f < a->frame_count; ++f) {
            if (PlatformShouldQuit()) break;
            PumpWin32Messages();
            /* Restore the clean menu under us before painting this frame. */
            if (snapshot && g_back_shadow)
                memcpy(g_back_shadow, snapshot, shadow_bytes);
            paint_anim_button_at(a, f, 0, 0, 0);   /* honour atlas hot-spot */
            FlushFrameToPrimary();
            TickMenuMusic();
            SDL_Delay(80);                        /* ~12 fps */
        }
        FreeAsset(a);
    }
    if (bg) FreeAsset(bg);
    free(snapshot);
    SDL_Delay(200);                               /* let engine note tail */
    StopMenuMusic();
}

/* ------------------------------------------------------------------------- *
 * play_loading_screen — the "LOLDING" screen between Maluch and the scene.
 *
 * Per LoadStage @ 0x00403320: `g_panel_cursor = krazek.pic` (a 203×220 RAWB
 * of a vinyl-CD shape with the text "LOLDING" baked in, preloaded by
 * PreloadCommonAssets). The engine paints it as a centred overlay on top
 * of whatever is on screen during every stage transition:
 * ( (screen_w - pic_w) / 2,
 * (screen_h - pic_h) / 2,
 * pic_w, pic_h,
 * pic_pixels);
 * FlushFrameToPrimary;
 *
 * No animation — it's a static drop while assets stream. We hold it for
 * ~1.5 s as the visible "loading time" placeholder.
 *
 * NOTE: load.pic / load.wyc are the save-slot menu — completely different
 * assets. Easy to confuse by filename. The Wacki "loading" overlay is
 * krazek.pic (Polish-pun text "LOLDING" on a CD/krążek).
 * ------------------------------------------------------------------------- */
static void play_loading_screen(void)
{
    void *bg_raw = NULL; uint32_t bg_size = 0;
    int   bg_ok  = LoadFileFromDta("krazek.pic", &bg_raw, &bg_size);
    if (!bg_ok) return;

    /* Paint it centred (paint_rawb_pic does the math + uses color-key 0
 * so the corners are transparent — the menu shows through where the
 * CD shape doesn't cover). */
    uint32_t start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 1500) {
        if (PlatformShouldQuit()) break;
        PumpWin32Messages();
        FlipBuffersClearWith(0);                  /* black underneath */
        paint_rawb_pic(bg_raw, bg_size, 0);       /* fullscreen-ish; small RAWB centers itself */
        FlushFrameToPrimary();
        SDL_Delay(33);
        if (HasPendingKey()) {
            uint16_t k = WaitForKey();
            if (k == VK_ESCAPE) break;
        }
    }
    xfree(bg_raw);
}


/* play_demo_scene — single-call scene loop (T22 phase B). Initial
 * komnata is loaded via LoadKomnataScene at prologue; subsequent
 * transitions happen IN-PLACE via ScriptGoToKomnata (op 0x20) without
 * unwinding the main loop. Returns NULL on quit (ESC / F12 TAK). */
extern const char *play_demo_scene(const DemoScene *scene); /* scene/play_loop.c */

/* HandleSceneInput externs (forward decls so the helper can call them). */
extern int  BindActorWalker(int actor_idx, int target_x, int target_y);
extern void DispatchClickEvent(uint16_t obj_id, uint16_t verb_id);

/* =================== Sejw.pic + Load.pic — save/load slot lists =========
 *
 * Slot picker UI (SaveSlotClick, LoadSlotClick, g_save_menu_scene,
 * g_load_menu_scene, paint_slot_list, inline-edit, ...) moved to
 * src/menu/slot_picker.c. */

/* =================== sel_tlo.pic — chapter-select UI ====================
 *
 * SelTloClick, g_sel_tlo_scene, SelTloRefreshButtons, and the s_chapter_
 * pick global moved to src/menu/chapter_select.c. The forward externs
 * for them live earlier in the file (near run_dev_start_stage_flow). */

/* is_walkable_at moved to src/scene/walkability.c. */

/* HandleSceneInput + helpers moved to src/scene/scene_input.c. */

/* ------------------------------------------------------------------------- *
 * T39: play_first_scene_demo removed (inlined into RunGameStageLoop).
 * What follows is the externs/helpers that body used.
 * ------------------------------------------------------------------------- */
/* Run the embedded enter_script for each scene through the bytecode VM.
 * Spawns the per-room NPCs (drut/barstoi/pies in maluch, babcia/deska/
 * domofon in klatka2, pijaki/ptak in kiosk21, dziewczynki/hustawki in
 * plac) so EntityRenderAll has actual entities to draw. */
extern Entity *g_render_list_head;
extern void    EntityWalkerTick(Entity *head);
extern void    EntityRenderAll (Entity *head);

/* T130 — s_entry_dir global retired (was kept as no-op stub since T22B).
 * Replaced all writes (HandleSceneInput, play_demo_scene prologue, etc.)
 * with explicit comment annotations referencing T22B; verb-driven
 * exits via LoadKomnataScene handle the role this global had. */

/* T39 (shipped): play_first_scene_demo removed. Its body was inlined
 * into RunGameStageLoop, the only caller after T22 phase B. */


/* ------------------------------------------------------------------------- *
 * RunGameStageLoop —
 *
 * Original control flow (decoded from ):
 *
 * zero script_vars + entity_state;
 *; // ResetInventory
 *;; // clear lists + reset panel
 * if (stage && !(flags & 0x10)) PlaySceneCutsceneAvi(intro_avi);
 * (g_cur_komnata); // LoadKomnata
 * // click dispatch with use-on-item / actor-toggle branch
 * // save UI if requested
 * // SPACE-mid-frame toggle
 * ProcessGameFrameTick;
 * // game-over handling (cases 1/3/4)
 * if (exit_signal) return;
 *
 * Flags:
 * bit 1 (0x02) = FULL RESET (new game): zero vars + ResetInventory + LoadStage
 * bit 4 (0x10) = SKIP INTRO AVI (came from save load)
 * bit 0+4 (0x11) combos used after F12 menu / save UI
 *
 * T39 (shipped): play_first_scene_demo legacy entry inlined here.
 * Previously RunGameStageLoop delegated to play_first_scene_demo which
 * was a thin wrapper after T22 phase B. Inlining removes the
 * indirection and pulls the actor-walk-anim setup + initial DemoScene
 * lookup directly into the canonical RunGameStageLoop body. */

/* ---- RunGameStageLoop constants + helpers ------------------------- */

/* Stage-1 fallback: PE VA of stage 1's StageDef + its etap id. Used
 * when RunGameStageLoop was entered without FULL_RESET (so LoadStage(1)
 * hasn't run) AND without SAVE_LOAD (so LoadSaveSlot didn't restore
 * g_stage_va). Without the fallback, DispatchClickEvent can't find
 * the verb tables and clicks silently no-op. */
#define STAGE_FALLBACK_VA               0x00428220u
#define STAGE_FALLBACK_ETAP             1
#define KOMNATA_FALLBACK                1

/* Stage-end AVI filenames. DEATH and CREDITS_STING are global to the
 * RunGameStageLoop epilogue; per-stage outros + transitions live on
 * the StageDef as alt_avi / alt3_avi. */
#define DEATH_AVI                       "Dane_14.dta"
#define CREDITS_STING_AVI               "Dane_13.dta"

/* `g_completed_stages` is a 32-bit bitmask of finished etaps. We only
 * set a bit if g_cur_etap fits in the mask. */
#define COMPLETED_STAGES_BITMASK_MAX    32

/* Initialize stage state on entry. FULL_RESET (flag 0x02) zeroes script
 * vars + ResetInventory + LoadStage(1); SAVE_LOAD (flag 0x10) trusts
 * LoadSaveSlot's prior g_stage_va / g_cur_komnata restore but falls
 * back to stage-1 defaults if either was missed. */
static void prepare_stage_state(uint8_t flags)
{
    if (flags & STAGE_LOAD_FLAG_FULL_RESET) {
        apply_full_reset();
        LoadStage(STAGE_FALLBACK_ETAP);
    }
    if (flags & STAGE_LOAD_FLAG_SAVE_LOAD) {
        if (!g_stage_va) {
            g_stage_va  = STAGE_FALLBACK_VA;
            g_cur_etap  = STAGE_FALLBACK_ETAP;
        }
        if (g_cur_komnata == 0) g_cur_komnata = KOMNATA_FALLBACK;
    }
}

/* Demo-fallback: if neither flag bit ran, set g_stage_va to stage 1 so
 * DispatchClickEvent finds the verb tables. */
static void ensure_stage_va_default(void)
{
    if (!g_stage_va) {
        g_stage_va = STAGE_FALLBACK_VA;
        g_cur_etap = STAGE_FALLBACK_ETAP;
    }
}

/* Drive one stage's gameplay loop: load the start komnata, run
 * play_demo_scene until it returns, then clear entity list + silence
 * any looping per-room SFX. */
static void run_initial_komnata_scene(uint16_t cur_komnata)
{
    LoadKomnataScene(cur_komnata);
    if (!g_current_scene) {
        fprintf(stderr,
                "[scene] RunGameStageLoop: LoadKomnataScene(%u) yielded no scene\n",
                cur_komnata);
        return;
    }

    (void)play_demo_scene((const DemoScene *)g_current_scene);

    extern void EntityListClearAll(void);
    extern void ResetFrameSfxState(void);
    EntityListClearAll();
    /* Silence any looping (N,M) SFX still mixing — the original frees
     * every per-komnata asset on gameplay exit; SampleTable destructors
     * stop their wavs. Without this, a [sampl] WAV (N,M) loop started
     * mid-scene keeps playing into the menu after ESC / Pytanie TAK. */
    ResetFrameSfxState();
}

/* Mark the current etap as completed in g_completed_stages. The
 * original sets this elsewhere (likely via a script-op write through
 * a var-indirection); we set it defensively in the epilogue so the
 * chapter-select map shows progress regardless of trigger source. */
static void mark_current_stage_completed(void)
{
    if (g_cur_etap >= 1 && g_cur_etap <= COMPLETED_STAGES_BITMASK_MAX) {
        g_completed_stages |= 1u << (g_cur_etap - 1);
        fprintf(stderr,
                "[game-over=%d] stage %u completed (g_completed_stages=0x%X)\n",
                GAME_OVER_STAGE_END_AVI,
                (unsigned)g_cur_etap, g_completed_stages);
    }
}

/* Play one of the StageDef's per-stage AVIs if it's non-NULL. */
static void play_stage_avi_if(const char *avi, int code, const char *kind)
{
    if (!avi) return;
    fprintf(stderr, "[game-over=%d] %s AVI: %s\n", code, kind, avi);
    PlaySceneCutsceneAvi(avi);
}

/* game_over_code == GAME_OVER_CHAPTER_PICK (3). Plays the just-finished
 * stage's outro, stashes its transition AVI, shows the chapter-select
 * map, loads the picked stage, then plays the stashed transition.
 *
 * 8-step order mirrors the original engine exactly. Step 4 (all_done →
 * credits sting + DROP into the map) intentionally does NOT bail so
 * the user can see the assembled ACME map and click the green finale
 * button — an earlier port `break`ed here and made stage 5 unreachable
 * from a regular playthrough. */
static void handle_game_over_chapter_pick(void)
{
    /* Step 1: refresh buttons; tells us whether all 4 stages are done. */
    int all_done = SelTloRefreshButtons();

    /* Step 2: outro of just-finished stage + stash alt3 for later. */
    const char *stashed_alt3 = NULL;
    if (!all_done && g_stage) {
        play_stage_avi_if(g_stage->alt_avi, GAME_OVER_CHAPTER_PICK, "outro");
        stashed_alt3 = g_stage->alt3_avi;
    }

    /* Step 3: re-refresh — belt-and-braces in case the AVI ran a script
     * op that flipped a completion bit. */
    all_done = SelTloRefreshButtons();

    /* Step 4: all stages done → credits sting, then drop into the map. */
    if (all_done) {
        fprintf(stderr, "[game-over=%d] all stages done → %s + map with "
                        "green button\n",
                GAME_OVER_CHAPTER_PICK, CREDITS_STING_AVI);
        PlaySceneCutsceneAvi(CREDITS_STING_AVI);
    }

    /* Step 5: chapter-select menu. */
    s_chapter_pick = 0;
    RunMenuScene(0, &g_sel_tlo_scene);

    /* Step 6: load picked stage (1..4 = regular, 5 = Monter finale). */
    if (s_chapter_pick >= 1 && s_chapter_pick <= DEV_PICK_FINALE) {
        fprintf(stderr, "[game-over=%d] LoadStage(%d)\n",
                GAME_OVER_CHAPTER_PICK, s_chapter_pick);
        LoadStage((uint16_t)s_chapter_pick);
    }

    /* Step 7: third pass — post-load button rebuild. */
    all_done = SelTloRefreshButtons();

    /* Step 8: intro AVI of newly-loaded stage (the prev stage's "going
     * to X" transition stashed in step 2). Skipped on finale path. */
    if (!all_done && stashed_alt3) {
        play_stage_avi_if(stashed_alt3, GAME_OVER_CHAPTER_PICK, "transition");
    }
}

/* game_over_code == GAME_OVER_STAGE_END_AVI (4). Plays both the outro
 * and transition AVIs back-to-back without the chapter-select menu —
 * used when stage-end is "automatic" (no player choice). */
static void handle_game_over_stage_end_avi(void)
{
    if (g_stage) {
        play_stage_avi_if(g_stage->alt_avi,
                          GAME_OVER_STAGE_END_AVI, "outro");
        play_stage_avi_if(g_stage->alt3_avi,
                          GAME_OVER_STAGE_END_AVI, "transition");
    }
    mark_current_stage_completed();
}

/* Post-loop game-over dispatcher: 1=death AVI, 3=chapter-pick UI,
 * 4=stage-end double AVI + completion bit. Unknown codes are no-ops. */
static void run_game_over_epilogue(void)
{
    switch (g_game_over_code) {
    case GAME_OVER_DEATH:
        PlaySceneCutsceneAvi(DEATH_AVI);
        return;
    case GAME_OVER_CHAPTER_PICK:
        handle_game_over_chapter_pick();
        return;
    case GAME_OVER_STAGE_END_AVI:
        handle_game_over_stage_end_avi();
        return;
    }
}

void RunGameStageLoop(uint8_t flags)
{
    g_game_over_code = GAME_OVER_NONE;

    prepare_stage_state(flags);

    extern uint32_t g_tick_counter;
    extern void     LoadActorWalkAnims(uint32_t stage_va);

    g_stats.boot_tick = g_tick_counter;
    ensure_stage_va_default();
    LoadActorWalkAnims(g_stage_va);

    uint16_t cur_komnata = (g_cur_komnata != 0) ? g_cur_komnata
                                                : KOMNATA_FALLBACK;
    g_cur_komnata = cur_komnata;

    run_initial_komnata_scene(cur_komnata);

    if (g_game_over_code) run_game_over_epilogue();
}

/* ------------------------------------------------------------------------- *
 * InitializeGameSubsystems — 0x00403A30
 * ------------------------------------------------------------------------- */
int InitializeGameSubsystems(void)
{
    if (InitializeDirectSound() != 0) {
        PlatformShowMessageBox("Wacki",
            "Program wymaga Direct Sound w wersji 5.0 lub nowszej.");
        return 0;
    }

    /* main archive — try local pwd first, then CD path */
    int opened = OpenDtaArchiveFile("Dane_02.dta");
    if (!opened) {
        char buf[280];
        snprintf(buf, sizeof buf, "%s/Dane_02.dta", g_cd_path);
        opened = OpenDtaArchiveFile(buf);
    }
    if (!opened) {
        fprintf(stderr, "\nBrak pliku bazy : Dane_02.dta (na CD: %s)\n", g_cd_path);
        PlatformShowMessageBox("Wacki",
            "Nie znaleziono Dane_02.dta — sprawd\xC5\xBA p\xC5\x82yt\xC4\x99 CD.");
        return 0;
    }
    fprintf(stderr, "[init] mounted archive Dane_02.dta\n");

    g_items_obj     = malloc(sizeof(struct ScriptObj));
    g_scripts_obj   = malloc(sizeof(struct ScriptObj));
    g_dialogues_obj = malloc(sizeof(struct ScriptObj));
    if (g_items_obj)     memset(g_items_obj,     0, sizeof(struct ScriptObj));
    if (g_scripts_obj)   memset(g_scripts_obj,   0, sizeof(struct ScriptObj));
    if (g_dialogues_obj) memset(g_dialogues_obj, 0, sizeof(struct ScriptObj));
    /* Item.scr has its OWN format ([N]filename per line, not the standard
 * [tag]<body> ScriptObj layout) — parsed by LoadItemNamesTable into
 * a fixed-width name table. The g_items_obj generic-ScriptObj load
 * stays for any code that still treats it as a ScriptObj, but the
 * voice-over uses the dedicated table. */
    LoadScriptFile(g_items_obj,     "Item.scr");
    LoadItemNamesTable();
    LoadScriptFile(g_scripts_obj,   "Wacky.scr");
    LoadScriptFile(g_dialogues_obj, "Gadki.scr");

    if (!PreloadCommonAssets())
        fprintf(stderr, "[init] some resident assets missing — continuing\n");

    extern int InitializeMmTimer(void *);
    static uint8_t mmt[32];
    InitializeMmTimer(mmt);
    return 1;
}
