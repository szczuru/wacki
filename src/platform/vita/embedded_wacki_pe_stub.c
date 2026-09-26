/* SPDX-License-Identifier: GPL-3.0-or-later
 * Empty stub used when data/WACKI.EXE is not present at build time.
 * Runtime falls back to dynamic loading from ux0:/data/wacki/ */

#include <stddef.h>

const unsigned char *g_wacki_pe_slices[] = { NULL };
const size_t         g_wacki_pe_slice_sizes[] = { 0 };
const int            g_wacki_pe_slice_count = 0;
