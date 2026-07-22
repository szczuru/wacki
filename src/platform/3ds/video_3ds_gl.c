/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/video_3ds_gl.c — dual-screen video HAL, direct
 * framebuffer writes (NO OpenGL / picaGL / citro3d).
 *
 * TOP SCREEN (400×240):    Main game rendering, full native resolution.
 * BOTTOM SCREEN (320×240): Zoomed view around cursor + touch input,
 *                          full native resolution.
 *
 * HISTORY / WHY NOT picaGL: an earlier version of this file used
 * picaGL (OpenGL ES 1.1 -> citro3d) — glTexImage2D/glTexSubImage2D to
 * upload each frame as a texture, then a textured quad + pglSwapBuffers
 * to present. That worked, but was slow (~9-12fps even with the
 * New3DS speedup enabled): picaGL's glTexSubImage2D does NOT do a
 * flat memcpy, it re-tiles the ENTIRE uploaded region into Morton
 * (swizzled) order, one pixel at a time, via an INDIRECT function-
 * pointer call per pixel (source/texture.c's _textureTile in picaGL,
 * confirmed against picaGL's real source) — expensive on the 3DS's
 * ARM11 (weak branch prediction for indirect calls), and that cost
 * scales with resolution. Reducing internal texture resolution helped
 * FPS but visibly hurt image quality; a "skip identical frames"
 * optimization on top of that only bought ~12fps back because this
 * game's frames are almost NEVER pixel-identical (ambient animation,
 * idle-cursor blink, etc.) — so the dirty-check rarely fires and the
 * expensive Morton-tiling path still runs on nearly every frame.
 *
 * This version sidesteps the whole GPU texture pipeline: the game's
 * source data is already a flat 8-bit indexed 2D image, so there is
 * no benefit to routing it through a 3D texture-mapping GPU pipeline
 * at all. Instead we do the same thing the simplest devkitPro
 * examples do (see 3ds-examples/graphics/bitmap/24bit-color) — write
 * pixels straight into the LCD's own framebuffer via
 * gfxGetFramebuffer(), converting each indexed pixel to RGB565 with a
 * single palette-lookup-and-pack, no GPU round-trip, no Morton tiling,
 * no intermediate RGBA buffer. This is dramatically cheaper per pixel
 * than the picaGL path, which is what makes full native resolution on
 * BOTH screens affordable again.
 *
 * SCREEN ROTATION: the 3DS's screens are physically portrait panels;
 * their framebuffers are stored ROTATED 90° from how you'd normally
 * think of them (libctru's own gfx.h: "the top screen is 240 pixels
 * wide and 400 pixels tall" — i.e. GSP_SCREEN_WIDTH=240 is the
 * FRAMEBUFFER's row length for BOTH screens, and visual X maps to
 * framebuffer COLUMNS, not rows). The standard, widely-documented
 * mapping from a normal-orientation visual pixel (x, y) — x in
 * [0, visual_width), y in [0, visual_height) — to a framebuffer pixel
 * INDEX (not yet a byte offset) is:
 *
 *     index = x * PHYS_ROW_LEN + (PHYS_ROW_LEN - 1 - y)
 *
 * where PHYS_ROW_LEN is always 240 (GSP_SCREEN_WIDTH) for both
 * screens — only visual_width/visual_height differ (400x240 top vs.
 * 320x240 bottom). See fb_pixel_index() below. */

#include "wacki.h"
#include "wacki/log.h"
#include "wacki/platform/video.h"
#include <3ds.h>
#include <malloc.h>
#include <string.h>

#define TOP_WIDTH  400
#define TOP_HEIGHT 240
#define BOT_WIDTH  320
#define BOT_HEIGHT 240

/* GSP_SCREEN_WIDTH (3ds/services/gspgpu.h) — the framebuffer's row
 * length in memory for BOTH screens (240), independent of which
 * screen's VISUAL width (400 top / 320 bottom) we're addressing. See
 * this file's top comment for why this differs from the visual
 * width/height. */
#define PHYS_ROW_LEN 240

#define GAME_WIDTH  640
#define GAME_HEIGHT 480

/* Zoom levels for bottom screen. 0 is a sentinel meaning "no zoom" —
 * show the WHOLE game surface (640x480) scaled down to fit the bottom
 * screen, like a miniature of the top screen, instead of a magnified
 * crop around the cursor. Placed LAST in the cycle (not first) so the
 * default g_zoom_level=0 index keeps its original behavior (1x crop)
 * rather than silently changing what a fresh boot shows; X still
 * reaches it, just one more press away: 1x -> 2x -> 4x -> no-zoom -> 1x. */
static int g_zoom_level = 0; /* index into ZOOM_LEVELS, 0=1x by default */
static const int ZOOM_LEVELS[] = {1, 2, 4, 0};
#define NUM_ZOOM_LEVELS 4

/* The exact source-image region the LAST bottom-screen frame was
 * extracted from — the top-left corner (s_zoom_src_x/y, in game-
 * surface pixels) and its width/height (s_zoom_region_w/h, also in
 * game-surface pixels). Cached here (written once per frame by
 * plat_video_present, right before the bottom-screen pixel loop runs)
 * so platform_video_touch_to_game() can map a touch-panel tap back
 * through the SAME region instead of re-deriving its own (potentially
 * stale, or differently-clamped) copy of the center/clamp math.
 *
 * Tracked as a region SIZE (not an integer "zoom multiplier") so the
 * same inverse-mapping math in platform_video_touch_to_game works
 * uniformly for every case, including the "no zoom" full-view mode
 * (region size == the whole w x h source, which has no single integer
 * zoom factor relative to BOT_WIDTH/BOT_HEIGHT in general). */
static int s_zoom_src_x = 0, s_zoom_src_y = 0;
static int s_zoom_region_w = BOT_WIDTH, s_zoom_region_h = BOT_HEIGHT;

/* Set by gamepad_3ds.c every frame via platform_video_set_touch_active()
 * — true for every frame a touch is currently held down. When true,
 * plat_video_present below SKIPS recentering the zoom crop on
 * g_mouse_x/y and reuses the frozen s_zoom_src_x/y from the frame the
 * touch started on. See platform_video_set_touch_active's own comment
 * for why continuously recentering while touched causes runaway drift. */
static int s_touch_active = 0;

/* g_mouse_x / g_mouse_y — the shared cursor globals (wacki/globals.h,
 * pulled in via wacki.h above). gamepad_3ds.c's touch/circle-pad code
 * writes them; using a private g_cursor_x/y here would desync the
 * bottom-screen zoom center from where the engine actually thinks the
 * cursor is. */

/* Game framebuffer (8-bit indexed). s_shadow/s_palette double as the
 * PREVIOUS frame's content, for the dirty-frame skip in
 * plat_video_present below — still worth keeping even though the
 * per-pixel cost dropped a lot: any tick that's genuinely a no-op
 * (dialog wait, idle menu) still skips real work entirely. */
static uint8_t *s_shadow = NULL;
static uint8_t *s_palette = NULL;
static int s_fb_w = 0, s_fb_h = 0;
static int s_have_prev_frame = 0; /* 0 until the first plat_video_present call */
static int s_prev_mouse_x = -1, s_prev_mouse_y = -1;
static int s_prev_zoom_level = -1;

/* Pack one 8-bit-per-channel RGB triple into RGB565 (5 bits R, 6 bits
 * G, 5 bits B — matches gfxSetScreenFormat(..., GSP_RGB565_OES) set in
 * plat_video_init below). */
static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Visual-orientation pixel (x, y) -> framebuffer u16 index. See this
 * file's top comment for the rotation rationale; PHYS_ROW_LEN (240)
 * is the same constant for both screens, only the caller's x range
 * (visual width) differs. */
static inline uint32_t fb_pixel_index(int x, int y)
{
    return (uint32_t)x * PHYS_ROW_LEN + (uint32_t)(PHYS_ROW_LEN - 1 - y);
}

/* 256-entry indexed-color -> RGB565 lookup table. Built ONCE per
 * plat_video_present call (see its call to build_palette_lut, shared
 * by both screens) — not per pixel, and not per-screen either. Both
 * blit functions below then do a single array read per pixel
 * (palette_lut[row[sx]]) instead of re-deriving pack_rgb565(e[0],e[1],
 * e[2]) — 3 loads + 2 shifts + 2 masks + an OR — from scratch for
 * EVERY one of the ~96000 (top) + ~76800 (bottom) pixels drawn each
 * frame. The palette only has 256 possible colors, so precomputing
 * this once and reusing it for every pixel is a straight win: it
 * turns the color-conversion cost from O(pixels) expensive-work into
 * O(pixels) cheap-array-lookup + O(256) expensive-work. */
static uint16_t s_palette_lut[256];

static void build_palette_lut(const uint8_t *pal)
{
    for (int i = 0; i < 256; ++i) {
        const uint8_t *e = pal + i * 3;
        s_palette_lut[i] = pack_rgb565(e[0], e[1], e[2]);
    }
}

/* Convert + downscale the full indexed game frame directly into the
 * top screen's live framebuffer (nearest-neighbor): palette-LUT
 * lookup -> rotated-offset write. No intermediate buffer, no GPU
 * texture upload — see this file's top comment for why this is so
 * much cheaper than the picaGL path it replaces.
 *
 * The per-column source-x mapping (sx for each dx) is identical for
 * every row (it only depends on x_ratio, not dy), so it's precomputed
 * ONCE into src_x_lut before the row loop instead of being
 * recalculated (a float multiply + int truncation + clamp) for every
 * single one of the TOP_HEIGHT*TOP_WIDTH pixels — cuts that part of
 * the per-pixel cost to a plain array read. */
static void blit_top_screen(const uint8_t *indexed, const uint8_t *pal,
                            uint16_t *fb, int src_w, int src_h)
{
    (void)pal; /* palette LUT built by the caller — see plat_video_present */

    static int src_x_lut[TOP_WIDTH];
    float x_ratio = (float)src_w / TOP_WIDTH;
    float y_ratio = (float)src_h / TOP_HEIGHT;
    for (int dx = 0; dx < TOP_WIDTH; ++dx) {
        int sx = (int)(dx * x_ratio);
        src_x_lut[dx] = (sx >= src_w) ? src_w - 1 : sx;
    }

    for (int dy = 0; dy < TOP_HEIGHT; ++dy) {
        int sy = (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        /* fb_pixel_index(dx, dy) - fb_pixel_index(0, dy) == dx * PHYS_ROW_LEN
         * exactly (the y-dependent term is constant across dx). Walking
         * this offset with a running += PHYS_ROW_LEN instead of
         * recomputing dx * PHYS_ROW_LEN for every pixel replaces a
         * multiply (several cycles on the 3DS's ARM11) with a single
         * add per pixel. */
        uint16_t *dst = fb + fb_pixel_index(0, dy);
        for (int dx = 0; dx < TOP_WIDTH; ++dx) {
            *dst = s_palette_lut[row[src_x_lut[dx]]];
            dst += PHYS_ROW_LEN;
        }
    }
}

/* Same idea as blit_top_screen, but samples a `zoom`-times magnified
 * crop of the source (src_x/src_y top-left corner, src_region_w/h
 * span) instead of the whole image — the bottom screen's "magnifier"
 * view around the cursor. Shares the same palette-LUT (built once by
 * blit_top_screen right before this runs — see plat_video_present's
 * call order) and the same per-column source-x precompute trick. */
static void blit_bottom_screen_zoom(const uint8_t *indexed, const uint8_t *pal,
                                    uint16_t *fb, int src_w, int src_h,
                                    int src_x, int src_y,
                                    int src_region_w, int src_region_h)
{
    (void)pal; /* palette LUT already built (see call site) */

    static int src_x_lut[BOT_WIDTH];
    float x_ratio = (float)src_region_w / BOT_WIDTH;
    float y_ratio = (float)src_region_h / BOT_HEIGHT;
    for (int dx = 0; dx < BOT_WIDTH; ++dx) {
        int sx = src_x + (int)(dx * x_ratio);
        src_x_lut[dx] = (sx >= src_w) ? src_w - 1 : sx;
    }

    for (int dy = 0; dy < BOT_HEIGHT; ++dy) {
        int sy = src_y + (int)(dy * y_ratio);
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *row = indexed + (size_t)sy * src_w;
        /* Same running-offset trick as blit_top_screen above. */
        uint16_t *dst = fb + fb_pixel_index(0, dy);
        for (int dx = 0; dx < BOT_WIDTH; ++dx) {
            *dst = s_palette_lut[row[src_x_lut[dx]]];
            dst += PHYS_ROW_LEN;
        }
    }
}

void platform_video_cycle_zoom(void)
{
    g_zoom_level = (g_zoom_level + 1) % NUM_ZOOM_LEVELS;
    int zoom = ZOOM_LEVELS[g_zoom_level];
    if (zoom == 0)
        LOG_INFO("3ds", "zoom level: no zoom (full view)");
    else
        LOG_INFO("3ds", "zoom level: %dx", zoom);
}

/* Called once per frame from gamepad_3ds.c's platform_pad_handle_buttons
 * — `active` is true for every frame a touch is currently held down,
 * false the instant it's released.
 *
 * WHY THIS EXISTS (fixes reported "cursor jumps/drifts fast" bug): the
 * bottom-screen crop recenters on g_mouse_x/y every frame it's redrawn.
 * A touch tap writes g_mouse_x/y by inverting THAT SAME crop
 * (platform_video_touch_to_game). If the crop were allowed to recenter
 * on the newly-written g_mouse_x/y again next frame WHILE THE SAME
 * TOUCH IS STILL HELD, the two feed off each other: touching a fixed
 * panel point tx away from center shifts the cursor by a constant
 * k = (tx - panel_center)/zoom EVERY frame, relative to wherever the
 * cursor ended up the previous frame — a runaway constant-velocity
 * drift for as long as the finger stays down anywhere off-center. That
 * matches "im dłużej trzymam, tym szybciej ucieka" exactly.
 *
 * Freezing the crop for the whole duration a touch is held (only
 * recentering again once the finger lifts) breaks the loop: every
 * frame of a single touch-and-drag now maps through the IDENTICAL
 * region, so a held finger at a fixed panel point always resolves to
 * the same game-surface point — sliding the finger still drags the
 * cursor smoothly (each new tx/ty maps through that one frozen crop),
 * it just stops re-centering the magnifier out from under itself. */
void platform_video_set_touch_active(int active)
{
    s_touch_active = active;
}

/* Map a touch-panel tap (tx,ty in the 320x240 BOT_WIDTH/BOT_HEIGHT
 * panel space) to game-surface coordinates, by inverting the EXACT
 * region blit_bottom_screen_zoom drew into the bottom screen on the
 * most recent plat_video_present call (cached in s_zoom_src_x/y +
 * s_zoom_region_w/h).
 *
 * Uses the same ratio-based math as blit_bottom_screen_zoom's forward
 * transform (src_region_w/dst_w, src_region_h/dst_h), inverted — NOT
 * a simple tx/zoom divide. An integer "zoom multiplier" only exists
 * for the magnified-crop levels (1x/2x/4x); the "no zoom" full-view
 * level's region is the whole w x h source, which isn't an integer
 * multiple of BOT_WIDTH/BOT_HEIGHT in general (640/320=2x exactly by
 * coincidence for the width, but 480/240=2x too — still, computing it
 * as a ratio here means this keeps working correctly even if
 * GAME_WIDTH/HEIGHT or BOT_WIDTH/HEIGHT ever change independently). */
void platform_video_touch_to_game(int tx, int ty, int *gx, int *gy)
{
    float x_ratio = (float)s_zoom_region_w / BOT_WIDTH;
    float y_ratio = (float)s_zoom_region_h / BOT_HEIGHT;
    int x = s_zoom_src_x + (int)(tx * x_ratio);
    int y = s_zoom_src_y + (int)(ty * y_ratio);

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > s_fb_w - 1) x = s_fb_w - 1;
    if (y > s_fb_h - 1) y = s_fb_h - 1;

    *gx = x;
    *gy = y;
}

void platform_video_get_present_state(int *stretch_active,
                                      int *win_w, int *win_h,
                                      int *fb_w,  int *fb_h)
{
    *stretch_active = 0;
    *win_w = TOP_WIDTH;
    *win_h = TOP_HEIGHT;
    *fb_w = s_fb_w;
    *fb_h = s_fb_h;
}

void platform_video_toggle_aspect_mode(void)
{
    /* Not applicable on 3DS - fixed screens */
}

/* wacki/platform/video.h HAL entry point — PlatformShowMessageBox routes
 * here. No native dialog on 3DS; log it (visible over 3dslink's console
 * relay / Citra's stdout) so a fatal-path message isn't silently lost. */
void plat_video_message_box(const char *title, const char *body)
{
    LOG_INFO("msgbox", "%s: %s", title ? title : "", body ? body : "");
}

int plat_video_init(int w, int h, const char *title)
{
    (void)title;
    plat_apply_video_prefs();

    s_fb_w = w;
    s_fb_h = h;

    /* Allocate buffers */
    s_shadow = (uint8_t *)linearAlloc(w * h);
    s_palette = (uint8_t *)linearAlloc(256 * 3);

    if (!s_shadow || !s_palette) {
        LOG_INFO("3ds", "Failed to allocate video buffers");
        return 0;
    }

    memset(s_shadow, 0, w * h);
    memset(s_palette, 0, 256 * 3);

    /* RGB565: 2 bytes/pixel, plenty of headroom for an 8-bit indexed
     * source palette, and half the memory traffic of RGBA8 for every
     * write in blit_top_screen/blit_bottom_screen_zoom above. No
     * picaGL/citro3d init needed at all — we write straight into the
     * LCD's own framebuffer memory (gfxGetFramebuffer), never touch
     * the GPU's 3D pipeline. */
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);

    /* Double buffering DISABLED (libctru's default is enabled) —
     * deliberately, to avoid a real bug interacting with the dirty-
     * frame skip in plat_video_present. With double buffering on,
     * gfxGetFramebuffer always returns the currently-HIDDEN buffer to
     * write into, and gfxSwapBuffers() flips which buffer is shown.
     * On a tick where a screen is skipped (not dirty, so we never
     * call gfxGetFramebuffer/write anything for it), calling
     * gfxSwapBuffers() at the end still flips ITS buffers too —
     * displaying whatever stale content is sitting in the OTHER
     * (not-just-written) buffer, which could be from several frames
     * ago. In practice this game's frames are almost never truly
     * identical (see this file's top comment) so skip runs are rare
     * and short, which is likely why this wasn't visibly caught in
     * testing — but it's a real latent bug that WOULD show as
     * flicker on a genuinely static screen (paused dialog, idle
     * menu) — precisely the case the dirty-skip targets.
     *
     * With double buffering off, gfxGetFramebuffer always returns the
     * SAME single buffer (the one currently on screen), so writing
     * into it and "swapping" (a harmless no-op re-present) can never
     * show stale content — matches the pattern used by devkitPro's
     * own simple 2D examples (graphics/bitmap/24bit-color explicitly
     * disables double buffering for exactly this "we're not
     * continuously re-rendering a 3D scene" use case). The tradeoff
     * (writing into a buffer while it's mid-scanout can in theory
     * show a tear line) is the same one every one of those examples
     * accepts, and is far less noticeable than periodic full-frame
     * flicker would be. */
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    LOG_INFO("3ds", "direct-framebuffer video initialized: %dx%d game -> top=%dx%d bot=%dx%d",
             w, h, TOP_WIDTH, TOP_HEIGHT, BOT_WIDTH, BOT_HEIGHT);

    return 1;
}

void plat_video_present(const uint8_t *shadow, const uint8_t *pal, int w, int h)
{
    if (!shadow || !pal) return;

    /* ---- Dirty-frame check --------------------------------------- *
     *
     * Cheap insurance: skip the pixel loops entirely on any tick whose
     * shadow+palette are byte-for-byte identical to the previous one
     * (dialog waits, idle menus). Most ticks in this game DO change
     * something (ambient animation, cursor blink), so this won't fire
     * every frame, but it's still a free win on the ticks where it
     * does — and unlike the previous picaGL-based version, the "real
     * work" this lets us skip is now cheap enough per-pixel that this
     * check is a minor bonus rather than the main optimization. */
    int top_dirty = !s_have_prev_frame ||
                    memcmp(s_shadow, shadow, (size_t)w * h) != 0 ||
                    memcmp(s_palette, pal, 256 * 3) != 0;

    int zoom = ZOOM_LEVELS[g_zoom_level]; /* 0 = sentinel for "no zoom" (full view) */
    int bot_dirty = top_dirty ||
                    g_mouse_x != s_prev_mouse_x ||
                    g_mouse_y != s_prev_mouse_y ||
                    g_zoom_level != s_prev_zoom_level;

    /* During cutscene AVI playback (see wacki/globals.h for the full
     * rationale), the bottom-screen magnifier is pure wasted per-frame
     * cost: the point-and-click cursor it exists to magnify isn't
     * shown/usable while a cutscene plays anyway (PlayFlicAviFile owns
     * the frame loop, not the normal input/render tick), so there is
     * nothing meaningful to show there. Forcing bot_dirty off frees up
     * a full BOT_WIDTH*BOT_HEIGHT (320x240) blit's worth of per-frame
     * budget for cutscene decode+blit+audio-pump instead — directly
     * addresses the user's own suggested fix for reported audio/video
     * drift during longer cutscenes (stutter -> compounding desync,
     * now ALSO fixed at the root cause in src/flic.c's pacing loop;
     * this is a complementary "spend the freed budget wisely" change
     * on top of that fix, not a substitute for it). The screen simply
     * keeps showing whatever it last had (stale, but no one is meant
     * to be looking at it or touching it during a cutscene). */
    if (g_cutscene_playing) bot_dirty = 0;

    if (!top_dirty && !bot_dirty) return;

    memcpy(s_shadow, shadow, (size_t)w * h);
    memcpy(s_palette, pal, 256 * 3);
    s_have_prev_frame = 1;
    s_prev_mouse_x = g_mouse_x;
    s_prev_mouse_y = g_mouse_y;
    s_prev_zoom_level = g_zoom_level;

    /* Bottom screen's zoom-crop source region is computed unconditionally
     * (cheap integer math) even when only the top screen is dirty, so
     * platform_video_touch_to_game always has an up-to-date region to
     * invert against — touch input must keep working even on frames
     * where the bottom screen's PIXELS didn't need re-drawing.
     *
     * EXCEPT while a touch is actively held (s_touch_active, set by
     * gamepad_3ds.c) — see platform_video_set_touch_active's comment
     * above for why recentering on every frame of a held touch causes
     * runaway drift. While frozen, s_zoom_src_x/y simply keep whatever
     * value they had from the frame the touch started (or from normal
     * cursor-follow recentering before that) — src_region_w/h are
     * still recomputed every frame since the ZOOM LEVEL can still
     * change (X button) while a touch is held. */
    int src_region_w, src_region_h, src_x, src_y;

    if (zoom == 0) {
        /* "No zoom": show the ENTIRE game surface, same as the top
         * screen, just scaled to BOT_WIDTH/BOT_HEIGHT instead of
         * TOP_WIDTH/TOP_HEIGHT — no cursor-following crop at all, so
         * touch-hold freezing is irrelevant here (the region never
         * moves regardless of s_touch_active). */
        src_region_w = w;
        src_region_h = h;
        src_x = 0;
        src_y = 0;
    } else {
        src_region_w = BOT_WIDTH / zoom;
        src_region_h = BOT_HEIGHT / zoom;

        if (s_touch_active) {
            src_x = s_zoom_src_x;
            src_y = s_zoom_src_y;
        } else {
            int cx = g_mouse_x;
            int cy = g_mouse_y;
            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            if (cx >= w) cx = w - 1;
            if (cy >= h) cy = h - 1;

            src_x = cx - src_region_w / 2;
            src_y = cy - src_region_h / 2;
            if (src_x < 0) src_x = 0;
            if (src_y < 0) src_y = 0;
            if (src_x + src_region_w > w) src_x = w - src_region_w;
            if (src_y + src_region_h > h) src_y = h - src_region_h;
            if (src_x < 0) src_x = 0; /* region wider than source (zoom<1 edge case) */
            if (src_y < 0) src_y = 0;
        }
    }

    s_zoom_src_x = src_x;
    s_zoom_src_y = src_y;
    s_zoom_region_w = src_region_w;
    s_zoom_region_h = src_region_h;

    /* Built once here (not inside either blit_* function) and shared by
     * both screens: whichever combination of top_dirty/bot_dirty holds,
     * the LUT must exist and be current before EITHER blit function
     * runs — building it inside just one of them (e.g. blit_top_screen)
     * would leave the bottom screen reading a stale/uninitialized LUT
     * on a frame where only the cursor moved (top_dirty false, bot_dirty
     * true). */
    if (top_dirty || bot_dirty) build_palette_lut(pal);

    if (top_dirty) {
        uint16_t *topfb = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        blit_top_screen(shadow, pal, topfb, w, h);
    }

    if (bot_dirty) {
        uint16_t *botfb = (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        blit_bottom_screen_zoom(shadow, pal, botfb, w, h,
                                src_x, src_y, src_region_w, src_region_h);
    }

    /* gfxFlushBuffers(): flush the data cache for whichever
     * framebuffer(s) we just wrote via CPU stores above — required
     * before the GSP (a separate processor) reads them for display;
     * see libctru's own docs: "preferred to call this only once per
     * frame, after all software rendering is completed." */
    gfxFlushBuffers();
    /* gfxSwapBuffers(): commit + flip both screens' framebuffers at
     * the next VBlank (equivalent to gfxScreenSwapBuffers for both
     * GFX_TOP and GFX_BOTTOM). Calling it even when only one screen
     * was dirty is harmless — the untouched screen's "swap" just
     * re-presents the same buffer it was already showing. */
    gfxSwapBuffers();
}

void plat_video_shutdown(void)
{
    if (s_shadow) linearFree(s_shadow);
    if (s_palette) linearFree(s_palette);

    s_shadow = NULL;
    s_palette = NULL;
}
