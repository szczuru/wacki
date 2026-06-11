/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * tests/test_komnata_load.c — LoadKomnata room-load invariants.
 *
 * Regression guard for the "opened door renders closed on re-entry" bug
 * (korlab5 atomic-lab airlock). LoadKomnata MUST pump two
 * ProcessGameFrameTick() after the enter script UNCONDITIONALLY — the
 * same as the original FUN_00402a50 — so one-shot BG-blit entities
 * spawned by the enter script (op 0x30 flags 0x0060, e.g. the open-door
 * overlay) get a render pass and paint themselves onto the BG before
 * LoadKomnataScene presents the scene.
 *
 * The bug: an earlier port gated those ticks on (second_va != 0). For a
 * komnata with NO secondary script (korlab5), the ticks were skipped,
 * the overlay never painted, and the airlock re-rendered CLOSED even
 * though it was passable. This suite pins the tick count to 2 for both
 * the secondary-less and the with-secondary path.
 *
 * The enter/secondary scripts here are a single OP_END (0x56), so
 * RunScriptInterpreter returns without itself pumping any frame tick —
 * the only ticks counted are LoadKomnata's own settle pump.
 *
 * Reference: src/scene/komnata.c (POST_ENTER_SETUP_TICKS).
 */

#include "test.h"
#include "wacki.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern const char *LoadKomnata(uint16_t id);
extern uint32_t    g_stage_va;
extern void       *g_scripts_obj;

/* test_engine_stubs.c: ProcessGameFrameTick() bumps this counter. */
extern int  g_stub_process_frame_calls;
extern void vm_stubs_reset(void);

/* pe_loader.c: map a synthetic PE blob so PeLoaderRead resolves our VAs. */
extern int  PeLoaderInitFromMemory(const uint8_t *file, size_t fsz,
                                   const char *label);
extern void PeLoaderFree(void);

/* ---- synthetic PE: one .data section mapped at VA 0x00401000 -------- */

#define IMAGE_BASE   0x00400000u
#define SEC_VA_REL   0x00001000u            /* → absolute VA 0x00401000 */
#define SEC_RPTR     0x140u                 /* PointerToRawData          */
#define SEC_SIZE     0x200u                 /* Virtual/Raw size          */
#define BLOB_SIZE    (SEC_RPTR + SEC_SIZE)  /* 0x340                     */

/* Layout inside the section (absolute VAs). */
#define STAGE_VA     0x00401000u            /* StageDef: +0 = komnata-arr VA */
#define ARR_VA       0x00401004u            /* komnata table (14 B / entry)  */
#define NAME_VA      0x00401040u            /* room name string              */
#define ENTER_VA     0x00401060u            /* enter/secondary bytecode      */

/* Komnata entry field offsets (mirror src/scene/komnata.c). */
#define K_NAME   0
#define K_FLAGS  4
#define K_ENTER  6
#define K_SECOND 10
#define K_STRIDE 14

static uint8_t s_blob[BLOB_SIZE];

static size_t va_off(uint32_t va) { return SEC_RPTR + (va - (IMAGE_BASE + SEC_VA_REL)); }
static void put_u32(uint32_t va, uint32_t v) { memcpy(s_blob + va_off(va), &v, 4); }
static void put_u16(uint32_t va, uint16_t v) { memcpy(s_blob + va_off(va), &v, 2); }
static void put_str(uint32_t va, const char *s) { memcpy(s_blob + va_off(va), s, strlen(s) + 1); }

static void build_pe_header(void)
{
    memset(s_blob, 0, sizeof s_blob);
    s_blob[0x00] = 'M'; s_blob[0x01] = 'Z';
    uint32_t e_lfanew = 0x80;            memcpy(s_blob + 0x3C, &e_lfanew, 4);
    s_blob[0x80] = 'P'; s_blob[0x81] = 'E';
    uint16_t machine = 0x014C, nsec = 1, opt_hdr = 0x60, chars = 0x0103;
    memcpy(s_blob + 0x84, &machine, 2);  memcpy(s_blob + 0x86, &nsec, 2);
    memcpy(s_blob + 0x94, &opt_hdr, 2);  memcpy(s_blob + 0x96, &chars, 2);
    uint16_t opt_magic = 0x010B;         memcpy(s_blob + 0x98, &opt_magic, 2);
    uint32_t image_base = IMAGE_BASE;    memcpy(s_blob + 0xB4, &image_base, 4);

    /* Section header at 0xF8 (= e_lfanew + 4 + COFF 0x14 + opt_hdr 0x60). */
    memcpy(s_blob + 0xF8, ".data", 5);
    uint32_t vsize = SEC_SIZE, va = SEC_VA_REL, rsize = SEC_SIZE, rptr = SEC_RPTR;
    memcpy(s_blob + 0xF8 + 0x08, &vsize, 4);
    memcpy(s_blob + 0xF8 + 0x0C, &va,    4);
    memcpy(s_blob + 0xF8 + 0x10, &rsize, 4);
    memcpy(s_blob + 0xF8 + 0x14, &rptr,  4);
}

/* One OP_END instruction (op 0x56, len 1 dword) — terminates the VM
 * immediately, pumping no frame ticks of its own. */
static void put_end_script(uint32_t va)
{
    put_u16(va + 0, (uint16_t)((uint16_t)0x56 | (uint16_t)(1u << 8)));
    put_u16(va + 2, 0);
}

/* Build a stage with two rooms:
 *   id 1 → enter only          (second_va == 0)  ← the korlab5 shape
 *   id 2 → enter + secondary   (second_va != 0)  ← parity control
 * followed by a zeroed terminator entry. */
static void build_stage(void)
{
    build_pe_header();

    put_u32(STAGE_VA, ARR_VA);

    put_u32(ARR_VA + 0 * K_STRIDE + K_NAME,   NAME_VA);
    put_u16(ARR_VA + 0 * K_STRIDE + K_FLAGS,  0x0007);
    put_u32(ARR_VA + 0 * K_STRIDE + K_ENTER,  ENTER_VA);
    put_u32(ARR_VA + 0 * K_STRIDE + K_SECOND, 0);                /* no secondary */

    put_u32(ARR_VA + 1 * K_STRIDE + K_NAME,   NAME_VA);
    put_u16(ARR_VA + 1 * K_STRIDE + K_FLAGS,  0x0007);
    put_u32(ARR_VA + 1 * K_STRIDE + K_ENTER,  ENTER_VA);
    put_u32(ARR_VA + 1 * K_STRIDE + K_SECOND, ENTER_VA);         /* has secondary */

    /* entry 2 left zeroed → terminator. */

    put_str(NAME_VA, "korlab5.pic");
    put_end_script(ENTER_VA);
}

static void setup(void)
{
    build_stage();
    PeLoaderFree();
    PeLoaderInitFromMemory(s_blob, sizeof s_blob, "komnata-load-test");
    vm_stubs_reset();
    g_scripts_obj = NULL;          /* skip [sampl] parsing (no script obj) */
    g_stage_va    = STAGE_VA;
}

static void teardown(void)
{
    PeLoaderFree();
    g_stage_va = 0;
}

/* The regression: a secondary-less room must STILL get the two settle
 * ticks. Pre-fix this was 0 and the open-door overlay never painted. */
TEST(loadkomnata_pumps_two_ticks_without_secondary)
{
    setup();
    g_stub_process_frame_calls = 0;

    const char *name = LoadKomnata(1);

    ASSERT_NOT_NULL(name);                        /* table parsed → real path ran */
    ASSERT_STREQ(name, "korlab5.pic");
    ASSERT_EQ(g_stub_process_frame_calls, 2);     /* unconditional settle pump */

    teardown();
}

/* Parity: a room WITH a secondary script also runs exactly the two
 * ticks before the secondary — the behaviour the secondary-less path
 * must now match. */
TEST(loadkomnata_pumps_two_ticks_with_secondary)
{
    setup();
    g_stub_process_frame_calls = 0;

    const char *name = LoadKomnata(2);

    ASSERT_NOT_NULL(name);
    ASSERT_EQ(g_stub_process_frame_calls, 2);

    teardown();
}

/* A lookup miss must NOT pump ticks (it returns before the enter path). */
TEST(loadkomnata_missing_room_pumps_no_ticks)
{
    setup();
    g_stub_process_frame_calls = 0;

    const char *name = LoadKomnata(99);           /* past the terminator */

    ASSERT_NULL(name);
    ASSERT_EQ(g_stub_process_frame_calls, 0);

    teardown();
}

SUITE(komnata_load)
{
    RUN_TEST(loadkomnata_pumps_two_ticks_without_secondary);
    RUN_TEST(loadkomnata_pumps_two_ticks_with_secondary);
    RUN_TEST(loadkomnata_missing_room_pumps_no_ticks);
}
