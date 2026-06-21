/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/switch/embedded_wacki_pe_stub.c — empty embedded-PE table.
 *
 * The public repo / CI can't embed WACKI.EXE's copyrighted .rdata/.data
 * bytes (see mk/switch.mk's header comment). This stub provides the same
 * symbols tools/embed-pe-data.c would generate, with an empty slice table,
 * so the linker is satisfied without ever touching data/WACKI.EXE.
 *
 * src/pe_loader.c's PeLoaderRead checks a DYNAMICALLY loaded image (set via
 * PeLoaderInit) first and only falls back to this (empty) embedded table —
 * see src/platform/switch/switch.c's plat_apply_video_prefs(), which loads
 * the player's own WACKI.EXE from the SD card at startup. */

#include "wacki/embedded_exe.h"

const PeSlice      g_wacki_pe_slices[]    = { {0, 0, 0, 0} };
const int          g_wacki_pe_slice_count = 0;
unsigned char      g_wacki_pe_blob[]      = { 0 };
const unsigned int g_wacki_pe_blob_len    = 0;
const uint32_t     g_wacki_pe_image_base  = 0;
