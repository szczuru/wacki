/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/embedded_wacki_pe_stub.c — PE loader stub for 3DS.
 *
 * When WACKI.EXE is not embedded at build time, this stub signals
 * that the engine should load it dynamically from SD card. */

const int          g_wacki_pe_slice_count = 0;
const unsigned int g_wacki_pe_slice_0_len = 0;
const unsigned char g_wacki_pe_slice_0[1] = {0};
