/* src/pe_loader.c — load WACKI.EXE as a passive memory image.
 *
 * Lets the port resolve any original bytecode/data address (verb
 * tables, object tables, embedded scripts, asset-name strings, click
 * payloads, etc.) without manually transcribing each blob into
 * binary_data.c.
 *
 * Strategy: parse the PE header just far enough to locate each section
 * (Name, VirtualAddress, SizeOfRawData, PointerToRawData), build a flat
 * in-memory image of size `max(VA + VirtualSize)`, then memcpy each
 * section from its file location to (image + VA). `PeLoaderRead(va)`
 * returns a host pointer for any original virtual address.
 *
 * No CODE is ever executed — pure data lookup. The .text section is
 * loaded too (so pointer references INTO .text still resolve), but the
 * port never interprets those bytes as x86 — only as opaque data for
 * the few cases scripts reference code pointers (almost never).
 *
 * The flat image is ~512 KB for WACKI.EXE — trivially cheap. */

#include "wacki.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- DOS / PE header offsets ------------------------------------ */

/* DOS MZ stub: e_lfanew (offset of the PE header) lives at +0x3C of the
 * file. Minimum file size that can carry an MZ + PE header at all. */
#define DOS_OFF_E_LFANEW            0x3C
#define DOS_MIN_FILE_SIZE           0x200

/* PE signature 'PE\0\0' is at file[pe_off..pe_off+3]; the COFF header
 * starts at pe_off + PE_SIG_BYTES. */
#define PE_SIG_BYTES                4

/* COFF header (20 bytes): Machine(2) NumberOfSections(2) TimeDateStamp(4)
 *   PtrToSymTable(4) NumberOfSymbols(4) SizeOfOptionalHeader(2)
 *   Characteristics(2). Offsets are relative to the COFF-header start
 *   (= pe_off + PE_SIG_BYTES). */
#define COFF_HEADER_BYTES           20
#define COFF_OFF_NUM_SECTIONS       0x02
#define COFF_OFF_OPT_HEADER_SIZE    0x10
#define COFF_OPT_HEADER_MIN_BYTES   0x60   /* PE32 needs at least this many */

/* Approximate ceiling for the PE-header tail used as a bounds check
 * when reading the PE/COFF/optional-header trio. Sized for PE32 with
 * the full data-directory array — generous so we don't reject legit
 * binaries. */
#define PE_HEADER_TAIL_GUARD        0xE0

/* Optional header (PE32) — only ImageBase needed at parse time.
 * Offset relative to the optional header's start (= pe_off +
 * PE_SIG_BYTES + COFF_HEADER_BYTES). */
#define OPT_HEADER_OFF_IMAGE_BASE   0x1C

/* Section header (40 bytes): Name[8] VirtualSize(4) VirtualAddress(4)
 *   SizeOfRawData(4) PointerToRawData(4) PointerToRelocations(4)
 *   PointerToLineNumbers(4) NumberOfRelocations(2)
 *   NumberOfLineNumbers(2) Characteristics(4). Offsets are relative
 *   to each section header's start. */
#define SECTION_HEADER_BYTES        40
#define SECTION_OFF_VSIZE           0x08
#define SECTION_OFF_VA              0x0C
#define SECTION_OFF_RSIZE           0x10
#define SECTION_OFF_RPTR            0x14

/* Sanity bounds on the flattened image's virtual extent. The lower
 * bound guards against truncated / non-PE inputs; the upper bound
 * defends against absurdly-large headers — WACKI.EXE itself maps to
 * ~512 KB, so 256 MB leaves several orders of magnitude of headroom. */
#define MIN_VIRTUAL_EXTENT          0x1000
#define MAX_VIRTUAL_EXTENT          0x10000000

/* ---- module state ----------------------------------------------- */

static uint8_t  *g_pe_image      = NULL;
static size_t    g_pe_image_size = 0;
static uint32_t  g_pe_image_base = 0;

/* ---- helpers ---------------------------------------------------- */

/* Read the entire file at `path` into a freshly-malloced buffer. Sets
 * *out_size on success. Returns NULL + closes fp on any failure. */
static uint8_t *slurp_file(FILE *fp, const char *path, size_t *out_size)
{
    fseek(fp, 0, SEEK_END);
    long fsz_l = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz_l < DOS_MIN_FILE_SIZE) {
        fclose(fp);
        return NULL;
    }
    size_t   fsz  = (size_t)fsz_l;
    uint8_t *file = (uint8_t *)malloc(fsz);
    if (!file) { fclose(fp); return NULL; }
    if (fread(file, 1, fsz, fp) != fsz) {
        fprintf(stderr, "[pe] short read on %s\n", path);
        free(file);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_size = fsz;
    return file;
}

/* Verify MZ + PE signatures. Returns the PE-header file offset on
 * success, 0 on any failure (signature mismatch / out-of-range). */
static uint32_t parse_pe_signature(const uint8_t *file, size_t fsz,
                                   const char *path)
{
    if (file[0] != 'M' || file[1] != 'Z') {
        fprintf(stderr, "[pe] %s: not an MZ executable\n", path);
        return 0;
    }
    uint32_t pe_off = *(const uint32_t *)(file + DOS_OFF_E_LFANEW);
    if (pe_off + PE_SIG_BYTES + COFF_HEADER_BYTES + PE_HEADER_TAIL_GUARD > fsz) {
        fprintf(stderr, "[pe] %s: PE header offset out of range\n", path);
        return 0;
    }
    if (file[pe_off]     != 'P' || file[pe_off + 1] != 'E' ||
        file[pe_off + 2] != 0   || file[pe_off + 3] != 0)
    {
        fprintf(stderr, "[pe] %s: missing PE signature\n", path);
        return 0;
    }
    return pe_off;
}

/* Walk the section table to find the highest VA + virtual extent —
 * that's the size we need to allocate for the flat image. */
static uint32_t compute_image_extent(const uint8_t *file, uint32_t sec_off,
                                     uint16_t num_sections)
{
    uint32_t max_va_end = 0;
    for (int i = 0; i < num_sections; ++i) {
        uint32_t shdr  = sec_off + (uint32_t)i * SECTION_HEADER_BYTES;
        uint32_t vsize = *(const uint32_t *)(file + shdr + SECTION_OFF_VSIZE);
        uint32_t va    = *(const uint32_t *)(file + shdr + SECTION_OFF_VA);
        uint32_t rsize = *(const uint32_t *)(file + shdr + SECTION_OFF_RSIZE);
        uint32_t use_size = vsize > rsize ? vsize : rsize;
        uint32_t end      = va + use_size;
        if (end > max_va_end) max_va_end = end;
    }
    return max_va_end;
}

/* memcpy each section's raw bytes from its file location to image + VA. */
static int copy_sections(const uint8_t *file, size_t fsz,
                         uint32_t sec_off, uint16_t num_sections,
                         uint8_t *image, uint32_t image_size)
{
    int copied = 0;
    for (int i = 0; i < num_sections; ++i) {
        uint32_t shdr  = sec_off + (uint32_t)i * SECTION_HEADER_BYTES;
        uint32_t va    = *(const uint32_t *)(file + shdr + SECTION_OFF_VA);
        uint32_t rsize = *(const uint32_t *)(file + shdr + SECTION_OFF_RSIZE);
        uint32_t rptr  = *(const uint32_t *)(file + shdr + SECTION_OFF_RPTR);
        if (!rsize) continue;
        if ((size_t)rptr + (size_t)rsize > fsz) {
            fprintf(stderr, "[pe] section %d points past EOF — skipping\n", i);
            continue;
        }
        if (va + rsize > image_size) continue;
        memcpy(image + va, file + rptr, rsize);
        ++copied;
    }
    return copied;
}

/* ---- public API ------------------------------------------------- */

int PeLoaderInit(const char *exe_path)
{
    if (!exe_path) return 0;

    FILE *fp = fopen(exe_path, "rb");
    if (!fp) {
        fprintf(stderr, "[pe] cannot open %s\n", exe_path);
        return 0;
    }
    size_t   fsz  = 0;
    uint8_t *file = slurp_file(fp, exe_path, &fsz);
    if (!file) return 0;

    uint32_t pe_off = parse_pe_signature(file, fsz, exe_path);
    if (!pe_off) { free(file); return 0; }

    uint32_t coff_off = pe_off + PE_SIG_BYTES;
    uint16_t num_sections = *(const uint16_t *)
        (file + coff_off + COFF_OFF_NUM_SECTIONS);
    uint16_t opt_header_size = *(const uint16_t *)
        (file + coff_off + COFF_OFF_OPT_HEADER_SIZE);
    if (opt_header_size < COFF_OPT_HEADER_MIN_BYTES) {
        fprintf(stderr, "[pe] %s: optional header too small (%u)\n",
                exe_path, opt_header_size);
        free(file);
        return 0;
    }

    uint32_t opt_off    = coff_off + COFF_HEADER_BYTES;
    uint32_t image_base = *(const uint32_t *)
        (file + opt_off + OPT_HEADER_OFF_IMAGE_BASE);

    /* Section headers immediately follow the optional header. */
    uint32_t sec_off = opt_off + opt_header_size;
    if (sec_off + (uint32_t)num_sections * SECTION_HEADER_BYTES > fsz) {
        fprintf(stderr, "[pe] %s: section table out of range\n", exe_path);
        free(file);
        return 0;
    }

    uint32_t max_va_end = compute_image_extent(file, sec_off, num_sections);
    if (max_va_end < MIN_VIRTUAL_EXTENT || max_va_end > MAX_VIRTUAL_EXTENT) {
        fprintf(stderr, "[pe] %s: implausible virtual extent %u\n",
                exe_path, max_va_end);
        free(file);
        return 0;
    }

    uint8_t *image = (uint8_t *)calloc(max_va_end, 1);
    if (!image) {
        fprintf(stderr, "[pe] cannot allocate %u bytes for image\n", max_va_end);
        free(file);
        return 0;
    }
    int sections_copied = copy_sections(file, fsz, sec_off, num_sections,
                                        image, max_va_end);

    g_pe_image      = image;
    g_pe_image_size = max_va_end;
    g_pe_image_base = image_base;
    fprintf(stderr, "[pe] mapped %s: base=0x%08X size=%u sections=%d\n",
            exe_path, image_base, (unsigned)max_va_end, sections_copied);
    free(file);
    return 1;
}

void PeLoaderFree(void)
{
    if (g_pe_image) free(g_pe_image);
    g_pe_image      = NULL;
    g_pe_image_size = 0;
    g_pe_image_base = 0;
}

const void *PeLoaderRead(uint32_t va)
{
    if (!g_pe_image) return NULL;
    if (va < g_pe_image_base) return NULL;
    uint32_t off = va - g_pe_image_base;
    if (off >= g_pe_image_size) return NULL;
    return g_pe_image + off;
}

int PeLoaderLoaded(void) { return g_pe_image != NULL; }

/* True iff `va` falls inside the loaded PE image's mapped address
 * range (= original .text/.rdata/.data sections). Used by bytecode
 * handlers to decide whether an operand is a PE VA that needs
 * xlat_binary_ptr translation, or an already-resolved native pointer
 * that should be cast through (uintptr_t).
 *
 * Earlier port used a hardcoded [0x00400000, 0x00500000) check based
 * on WACKI.EXE's image base + a generous margin. This helper uses the
 * actual image size from the PE headers so it stays correct for other
 * PE images that don't share WACKI's specific layout. */
int PeLoaderContainsVA(uint32_t va)
{
    if (!g_pe_image) return 0;
    if (va < g_pe_image_base) return 0;
    return (va - g_pe_image_base) < g_pe_image_size;
}
