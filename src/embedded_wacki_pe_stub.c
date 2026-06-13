/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/embedded_wacki_pe_stub.c — empty embedded-PE table for builds
 * that load WACKI.EXE dynamically at runtime (TARGET=switch).
 *
 * On Nintendo Switch the build cannot ship the WACKI.EXE-derived data
 * (copyright — see BUILDING.md / README "repozytorium nie zawiera
 * materiałów z gry"). Instead of linking the generated
 * src/embedded_wacki_pe.c (produced by tools/embed-pe-data from
 * data/WACKI.EXE at build time), this stub provides the same symbols
 * with an empty slice table.
 *
 * src/pe_loader.c's embedded_read()/embedded_contains() simply find
 * nothing (g_wacki_pe_slice_count == 0) and PeLoaderRead falls through
 * to the "no embedded data" path — UNLESS a dynamic image has been
 * loaded via PeLoaderInit(), which takes precedence and is checked
 * first in PeLoaderRead. main.c calls PeLoaderInit() with the path to
 * the user's own WACKI.EXE on the SD card for WACKI_SWITCH builds —
 * see the PeLoaderInit call added to WackiMain(). */

#include "wacki/embedded_exe.h"

const PeSlice            g_wacki_pe_slices[]      = { {0, 0, 0, 0} };
const int                g_wacki_pe_slice_count   = 0;
unsigned char            g_wacki_pe_blob[]        = { 0 };
const unsigned int       g_wacki_pe_blob_len      = 0;
const uint32_t           g_wacki_pe_image_base    = 0;
