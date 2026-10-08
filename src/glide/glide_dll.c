/*
 * GLIDE2X.DLL, Glide 2.x over the render interface
 * (docs\plans\glide-2x-wrapper.md). Phase 0 made it a census of the calls
 * Need for Speed II SE makes; Phase 2 gives it a device. grSstWinOpen takes
 * the screen (glide_surface.c); clears, swaps, frame-buffer locks and
 * untextured triangles and lines reach it through the render interface,
 * with the state mapped by glide_state.c and the vertices by
 * glide_vertex.c. Textured draws are counted and skipped until Phase 3.
 *
 * All 130 retail exports exist (src\glide\glide_entrypoints.psd1). The 50
 * that NFS2SEA.EXE imports are written here: each logs its arguments and
 * returns what a working Voodoo Graphics board would, closely enough that
 * the game carries on. The other 80 are generated stubs that log their
 * first call and return zero.
 *
 * The log is C:\V9XDIAG\V9XGLIDE.LOG, appended a line at a time with the
 * file opened and closed each time, so a game that dies mid-run leaves
 * everything it logged (the ICD's rule, gl_icd.c). Volume is bounded by the
 * rules above V9X_GLIDE_CAPTURE_FIRST below: whole frames now and then, and
 * each distinct argument set once. Every export is counted, and the counts
 * are written every 15 s and at shutdown.
 *
 * Floats are logged as their IEEE bit patterns: converting one to an
 * integer lowers to Watcom's __CHP helper, which a DLL linked without the C
 * runtime cannot resolve (gl_state.c), and the bits lose nothing.
 *
 * Glide API values below cite the Glide 2.4 Reference Manual (3Dfx
 * Interactive, 1997) as facts. No 3dfx source is copied (licence rule in
 * docs\plans\3dfx-voodoo3-prior-work.md). Where a value is a guess at what
 * the game needs rather than what the manual says, the comment says so;
 * the census decides those.
 *
 * Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "glide_api.h"
#include "glide_vertex.h"
#include "glide_state.h"
#include "glide_texmem.h"
#include "glide_texfmt.h"
#include "glide_surface.h"

static void v9x_glide_stub_called(unsigned int ix);
#define V9X_GLIDE_STUB_HOOK(ix) v9x_glide_stub_called(ix)
#define V9X_GLIDE_DEFINE_STUBS
#include "glide_exports_gen.h"

static const char v9x_glide_build_id[] = "V9XGLIDE build=" V9X_BUILD_ID;
static const char *const v9x_glide_names[V9X_GLIDE_EXPORT_COUNT] =
    V9X_GLIDE_EXPORT_NAMES;

/* FXTRUE / FXFALSE (Reference Manual, "Data types"). */
#define V9X_GLIDE_TRUE  1ul
#define V9X_GLIDE_FALSE 0ul

/*
 * grGlideGetVersion's string. The retail Voodoo Graphics GLIDE2X.DLL on the
 * NFS II SE disc carries the ident "@#%2.4"; 2.4 is that release's version.
 */
#define V9X_GLIDE_VERSION_STRING "2.4"

/*
 * What grSstQueryHardware reports: one Voodoo Graphics (GR_SSTTYPE_VOODOO,
 * 0) with 2 MiB of frame buffer and one TMU of 2 MiB, the common 4 MiB
 * board. GrHwConfiguration is num_sst, then per board the type and
 * GrVoodooConfig_t: fbRam (MiB), fbiRev, nTexelfx, sliDetect, then per TMU
 * tmuRev and tmuRam (MiB). The revisions are guesses; the census records
 * whether the game reads them.
 */
#define V9X_GLIDE_SSTTYPE_VOODOO 0ul
#define V9X_GLIDE_FB_MIB         2ul
#define V9X_GLIDE_FBI_REV        2ul
#define V9X_GLIDE_TMU_COUNT      1ul
#define V9X_GLIDE_TMU_REV        1ul
#define V9X_GLIDE_TMU_MIB        2ul

/* The TMU address range grTexMinAddress / grTexMaxAddress report: the
 * whole 2 MiB. Whether retail Glide holds back the top few bytes is not
 * known; the census shows how the game uses the range. */
#define V9X_GLIDE_TEX_MIN_ADDRESS 0ul
#define V9X_GLIDE_TEX_MAX_ADDRESS (V9X_GLIDE_TMU_MIB << 20)

/* grTexDownloadTable: a palette is 256 FxU32 (glide_api.h); an NCC table
 * (GuNccTable) 112 bytes. */
#define V9X_GLIDE_NCC_TABLE_DWORDS  28u

/* grTexCombineFunction's GR_TEXTURECOMBINE_DECAL: the texel passes through,
 * as grTexCombine with LOCAL for colour and alpha. */
#define V9X_GLIDE_TEXTURECOMBINE_DECAL 1ul

/* A function the state mapping does not know, so a TMU set to it is
 * reported unrecognised. */
#define V9X_GLIDE_COMBINE_FUNCTION_OTHER 0xFFul

/*
 * grLfbLock. GrLfbInfo_t is size, lfbPtr, strideInBytes, writeMode,
 * origin (20 bytes); the caller fills size. Voodoo Graphics' frame buffer
 * is 1024 pixels a line, so its stride is 2048 bytes in the 16-bit write
 * modes and 4096 in GR_LFBWRITEMODE_888 (4) and _8888 (5). The census
 * build hands out a shadow buffer of that geometry, so a game that assumes
 * the stride writes inside it. GR_LFBWRITEMODE_ANY (0xFF) reports 565 (0).
 */
#define V9X_GLIDE_LFB_INFO_BYTES   20ul
/* GrLock_t: GR_LFB_READ_ONLY 0, GR_LFB_WRITE_ONLY 1, GR_LFB_NOIDLE 0x10. */
#define V9X_GLIDE_LFB_WRITE_ONLY   1ul
#define V9X_GLIDE_LFB_WRITE_565    0ul
#define V9X_GLIDE_LFB_WRITE_888    4ul
#define V9X_GLIDE_LFB_WRITE_8888   5ul
#define V9X_GLIDE_LFB_WRITE_ANY    0xfful
#define V9X_GLIDE_LFB_STRIDE_16    2048ul
#define V9X_GLIDE_LFB_STRIDE_32    4096ul
#define V9X_GLIDE_LFB_LINES        1024ul
#define V9X_GLIDE_LFB_BYTES        (V9X_GLIDE_LFB_STRIDE_32 * V9X_GLIDE_LFB_LINES)

/*
 * grSstStatus. The Voodoo Graphics status register (SST-1 spec, "status"):
 * bits 0-5 PCI FIFO free space, bit 6 vertical retrace, bits 7-9 busy. The
 * census build reports an idle board with a full FIFO and flips bit 6 each
 * call, so a game polling for either retrace polarity gets out of its loop.
 */
#define V9X_GLIDE_STATUS_FIFO_FREE 0x3ful
#define V9X_GLIDE_STATUS_VRETRACE  0x40ul

/*
 * Log volume. Two rules, because the first census run (NFS II SE on the
 * netbook, 2026-10-08) made 80 million draw calls and 107,767 swaps in 40
 * seconds, and a menu re-downloads the same two textures every frame:
 *
 * - Whole frames. The first V9X_GLIDE_CAPTURE_FIRST frames and every
 *   V9X_GLIDE_CAPTURE_EVERY-th log every call in order, up to
 *   V9X_GLIDE_CAPTURE_LINES lines, so a frame's state sequence can be read.
 * - Distinct arguments. Outside a captured frame a state or texture call
 *   logs only an argument set it has not logged before (a fixed hash set;
 *   once full, nothing new is logged and the counts still run).
 *
 * Draw and poll calls also log their first V9X_GLIDE_SAMPLE_FIRST calls.
 * The counts are written every V9X_GLIDE_SUMMARY_MS.
 */
#define V9X_GLIDE_CAPTURE_FIRST    3ul
#define V9X_GLIDE_CAPTURE_EVERY    1800ul
#define V9X_GLIDE_CAPTURE_LINES    3000ul
#define V9X_GLIDE_SEEN_SLOTS       8192u
#define V9X_GLIDE_SAMPLE_FIRST     24ul
#define V9X_GLIDE_SUMMARY_MS       15000ul

/*
 * The census build draws nothing, so without a wait a swap returns at
 * once and the game spins. A swap with interval n waits n frames of a
 * 60 Hz retrace, as the board would; the game's timing then resembles a
 * real run.
 */
#define V9X_GLIDE_FRAME_MS         16ul

static v9x_u32 v9x_glide_sequence;
static v9x_u32 v9x_glide_calls[V9X_GLIDE_EXPORT_COUNT];
static v9x_u32 v9x_glide_lines[V9X_GLIDE_EXPORT_COUNT];
static v9x_u32 v9x_glide_seen[V9X_GLIDE_SEEN_SLOTS];
static v9x_u32 v9x_glide_seen_full;
/* The frame before the first swap is captured: it holds the setup. */
static v9x_u32 v9x_glide_capturing = 1ul;
static v9x_u32 v9x_glide_capture_lines;
static v9x_u32 v9x_glide_summary_tick;
static v9x_u32 v9x_glide_swaps;
static v9x_u32 v9x_glide_status_toggle;
static v9x_u8 *v9x_glide_lfb;

/* The Glide state as the game set it, and what grSstWinOpen chose. */
static V9X_GLIDE_STATE v9x_glide_state;
static v9x_u32 v9x_glide_color_format;
static v9x_u32 v9x_glide_render_buffer = V9X_GLIDE_BUFFER_BACK;
static v9x_u8 v9x_glide_fog[V9X_GLIDE_FOG_TABLE_SIZE];
/* The buffer grLfbLock locked on the device, plus one; 0 for the shadow. */
static v9x_u32 v9x_glide_lfb_locked;

/* Draw outcomes, written with the census counts. */
static v9x_u32 v9x_glide_drawn;
static v9x_u32 v9x_glide_refused;
static v9x_u32 v9x_glide_culled;
static v9x_u32 v9x_glide_skipped_textured;
static v9x_u32 v9x_glide_unrecognized_logged;
static v9x_u32 v9x_glide_fog_dropped;
static v9x_u32 v9x_glide_uploads;
static v9x_u32 v9x_glide_refusals_logged;
static v9x_u32 v9x_glide_surface_hits;
static v9x_u32 v9x_glide_surface_evictions;

/*
 * Where a frame's time goes, in CPU cycles, as the ICD measures (gl_icd.c):
 * low-dword TSC deltas, each under a call or a frame, added into 64-bit
 * buckets and written with every summary, then cleared. NFS II SE ran at
 * about 5 frames a second with uploads already cut (2026-10-09); these say
 * whether the time is the game's, the DLL's or the engine's. Every
 * machine the DLL has run on has a TSC (Atom N280, Pentium III).
 */
static DWORD v9x_glide_rdtsc_low(void);
#pragma aux v9x_glide_rdtsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];

#define V9X_GLIDE_PROF_FRAME   0u   /* swap to swap: everything */
#define V9X_GLIDE_PROF_ENTRY   1u   /* inside grDrawTriangle and grDrawLine */
#define V9X_GLIDE_PROF_PREPARE 2u   /* state mapping and texture binding */
#define V9X_GLIDE_PROF_UPLOAD  3u   /* texture surface creation and fill */
#define V9X_GLIDE_PROF_SUBMIT  4u   /* the render interface's draw */
#define V9X_GLIDE_PROF_SWAP    5u   /* the flip */
#define V9X_GLIDE_PROF_CLEAR   6u   /* the render interface's clear */
#define V9X_GLIDE_PROF_COUNT   7u

static v9x_u32 v9x_glide_prof[V9X_GLIDE_PROF_COUNT * 2u];
static v9x_u32 v9x_glide_breaks[3];
static v9x_u32 v9x_glide_prof_batches;
static v9x_u32 v9x_glide_prof_swap_mark;

static void v9x_glide_prof_add(unsigned int bucket, v9x_u32 delta)
{
    v9x_u32 *pair = &v9x_glide_prof[bucket * 2u];

    pair[0] += delta;
    if (pair[0] < delta) {
        ++pair[1];
    }
}

/* ---- the log ------------------------------------------------------- */

/* One line, appended: sequence, process, milliseconds, text. Shared with
 * glide_surface.c (glide_surface.h), as gl_icd.c's log is with
 * gl_surface.c; not exported from the DLL. */
void v9x_glide_log(const char *text)
{
    HANDLE file;
    /* The longest text (a vertex line, under 200) plus the prefix. */
    char line[400];
    DWORD written;
    int length;

    file = CreateFileA(V9X_DIAG_GLIDE_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    length = wsprintfA(line, "%lu pid=%08lX t=%lu %s\r\n", ++v9x_glide_sequence,
                       GetCurrentProcessId(), GetTickCount(), text);
    WriteFile(file, line, (DWORD)length, &written, 0);
    CloseHandle(file);
}

void v9x_glide_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    char text[256];

    wsprintfA(text, format, a, b, c);
    v9x_glide_log(text);
}

/* The export's name and call number, then the formatted arguments. */
static void v9x_glide_logf(unsigned int ix, v9x_u32 call, const char *format, ...)
{
    char text[320];
    char args[256];
    va_list list;

    va_start(list, format);
    wvsprintfA(args, format, list);
    va_end(list);
    wsprintfA(text, "%s#%lu %s", v9x_glide_names[ix], call, args);
    v9x_glide_log(text);
    ++v9x_glide_lines[ix];
    if (v9x_glide_capturing &&
        ++v9x_glide_capture_lines >= V9X_GLIDE_CAPTURE_LINES) {
        v9x_glide_capturing = 0ul;
        v9x_glide_log("capture budget spent");
    }
}

static v9x_u32 v9x_glide_count(unsigned int ix)
{
    return ++v9x_glide_calls[ix];
}

/* Whether a draw or poll call's nth call is logged. */
static int v9x_glide_sampled(v9x_u32 call)
{
    return call <= V9X_GLIDE_SAMPLE_FIRST || v9x_glide_capturing != 0ul;
}

static v9x_u32 v9x_glide_rotl(v9x_u32 value, unsigned int bits)
{
    return (value << bits) | (value >> (32u - bits));
}

/*
 * Whether ix's argument set, folded into key, is new to the run. Open
 * addressing on the (export, key) hash; zero marks an empty slot, so a
 * hash of zero is stored as one (a collision costs one unlogged set).
 */
static int v9x_glide_first_seen(unsigned int ix, v9x_u32 key)
{
    v9x_u32 hash = v9x_glide_rotl(key, 11u) ^ ((v9x_u32)ix * 0x9E3779B1ul);
    unsigned int slot;
    unsigned int probe;

    if (hash == 0ul) {
        hash = 1ul;
    }
    slot = (unsigned int)(hash % V9X_GLIDE_SEEN_SLOTS);
    for (probe = 0u; probe < V9X_GLIDE_SEEN_SLOTS; ++probe) {
        if (v9x_glide_seen[slot] == hash) {
            return 0;
        }
        if (v9x_glide_seen[slot] == 0ul) {
            v9x_glide_seen[slot] = hash;
            return 1;
        }
        slot = (slot + 1u) % V9X_GLIDE_SEEN_SLOTS;
    }
    if (!v9x_glide_seen_full) {
        v9x_glide_seen_full = 1ul;
        v9x_glide_log("distinct-argument set full; only captures log now");
    }
    return 0;
}

/* Whether a state or texture call is logged: inside a captured frame,
 * always; otherwise when its argument set is new. */
static int v9x_glide_noted(unsigned int ix, v9x_u32 key)
{
    if (v9x_glide_capturing) {
        return 1;
    }
    return v9x_glide_first_seen(ix, key);
}

static v9x_u32 v9x_glide_key(v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 d)
{
    return a ^ v9x_glide_rotl(b, 7u) ^ v9x_glide_rotl(c, 14u) ^
           v9x_glide_rotl(d, 21u);
}

static v9x_u32 v9x_glide_checksum(const v9x_u32 *words, unsigned int count)
{
    v9x_u32 sum = 0ul;
    unsigned int index;

    for (index = 0u; index < count; ++index) {
        sum = v9x_glide_rotl(sum, 5u) ^ words[index];
    }
    return sum;
}

/* Every export called so far: its calls and how many lines it logged. */
static void v9x_glide_summary(const char *why)
{
    char text[160];
    unsigned int ix;

    wsprintfA(text, "census %s swaps=%lu", why, v9x_glide_swaps);
    v9x_glide_log(text);
    wsprintfA(text, "census   draws: drawn=%lu refused=%lu culled=%lu skipped-textured=%lu",
              v9x_glide_drawn, v9x_glide_refused, v9x_glide_culled,
              v9x_glide_skipped_textured);
    v9x_glide_log(text);
    wsprintfA(text, "census   draws: fog-dropped=%lu uploads=%lu surface-hits=%lu evictions=%lu",
              v9x_glide_fog_dropped, v9x_glide_uploads, v9x_glide_surface_hits,
              v9x_glide_surface_evictions);
    v9x_glide_log(text);
    {
        /* Megacycles (2^20) since the last summary, then cleared. */
        v9x_u32 mc[V9X_GLIDE_PROF_COUNT];
        unsigned int b;

        for (b = 0u; b < V9X_GLIDE_PROF_COUNT; ++b) {
            mc[b] = (v9x_glide_prof[b * 2u + 1u] << 12) |
                    (v9x_glide_prof[b * 2u] >> 20);
            v9x_glide_prof[b * 2u] = 0ul;
            v9x_glide_prof[b * 2u + 1u] = 0ul;
        }
        wsprintfA(text, "census   cycles(Mc): frame=%lu entry=%lu prepare=%lu upload=%lu submit=%lu swap=%lu clear=%lu batches=%lu",
                  mc[0], mc[1], mc[2], mc[3], mc[4], mc[5], mc[6],
                  v9x_glide_prof_batches);
        v9x_glide_prof_batches = 0ul;
        v9x_glide_log(text);
        wsprintfA(text, "census   batch ends: full=%lu texture=%lu state=%lu",
                  v9x_glide_breaks[0], v9x_glide_breaks[1], v9x_glide_breaks[2]);
        v9x_glide_breaks[0] = 0ul;
        v9x_glide_breaks[1] = 0ul;
        v9x_glide_breaks[2] = 0ul;
        v9x_glide_log(text);
    }
    for (ix = 0u; ix < V9X_GLIDE_EXPORT_COUNT; ++ix) {
        if (v9x_glide_calls[ix] == 0ul) {
            continue;
        }
        wsprintfA(text, "census   %s calls=%lu lines=%lu", v9x_glide_names[ix],
                  v9x_glide_calls[ix], v9x_glide_lines[ix]);
        v9x_glide_log(text);
    }
}

static void v9x_glide_stub_called(unsigned int ix)
{
    if (v9x_glide_count(ix) == 1ul) {
        char text[96];

        wsprintfA(text, "stub first-call %s", v9x_glide_names[ix]);
        v9x_glide_log(text);
    }
}

/*
 * A GrVertex: x, y, z, r, g, b, ooz, a, oow, then per TMU sow, tow, oow
 * (Reference Manual, GrVertex). The first twelve words are the same
 * whatever the TMU count the game was built for.
 */
static void v9x_glide_vertex(unsigned int ix, v9x_u32 call, char tag,
                             const v9x_u32 *v)
{
    if (v == 0) {
        v9x_glide_logf(ix, call, "  v%c null", tag);
        return;
    }
    v9x_glide_logf(ix, call,
                   "  v%c x=%08lX y=%08lX ooz=%08lX oow=%08lX rgba=%08lX,%08lX,%08lX,%08lX st0=%08lX,%08lX,%08lX",
                   tag, v[0], v[1], v[6], v[8], v[3], v[4], v[5], v[7], v[9],
                   v[10], v[11]);
}

/* ---- drawing on the device ------------------------------------------ */

static void v9x_glide_zero(void *block, unsigned int bytes)
{
    unsigned int i;

    for (i = 0u; i < bytes; ++i) {
        ((v9x_u8 *)block)[i] = 0u;
    }
}

/* Word by word: the setup, texture and level compared for every queued
 * triangle are all 32-bit fields. */
static int v9x_glide_same(const void *a, const void *b, unsigned int bytes)
{
    const v9x_u32 *x = (const v9x_u32 *)a;
    const v9x_u32 *y = (const v9x_u32 *)b;
    unsigned int i;

    for (i = 0u; i < bytes / 4u; ++i) {
        if (x[i] != y[i]) {
            return 0;
        }
    }
    return 1;
}

/* An unrecognised state is drawn with the closest mapping; the first few
 * are logged so the census of a new title names what to add. */
static void v9x_glide_note_unrecognized(const V9X_GLIDE_DRAW_SETUP *setup)
{
    const V9X_GLIDE_STATE *s = &v9x_glide_state;

    if (setup->recognized || v9x_glide_unrecognized_logged >= 16ul) {
        return;
    }
    ++v9x_glide_unrecognized_logged;
    v9x_glide_log3("unrecognized: color func=%lu factor=%lu local/other/invert=%06lX",
                   s->color.function, s->color.factor,
                   (s->color.local << 16) | (s->color.other << 8) | s->color.invert);
    v9x_glide_log3("unrecognized: alpha func=%lu factor=%lu local/other/invert=%06lX",
                   s->alpha.function, s->alpha.factor,
                   (s->alpha.local << 16) | (s->alpha.other << 8) | s->alpha.invert);
    v9x_glide_log3("unrecognized: tmu rgb/alpha=%lu/%lu blend=%04lX",
                   s->tex_rgb_function, s->tex_alpha_function,
                   (s->blend_src << 8) | s->blend_dst);
}

/*
 * The textures the game downloaded, one entry per glide_texmem.c record
 * (same index). An entry keeps the game's texels of the largest level - the
 * pointer it downloaded from is the game's to reuse - with a checksum of
 * them, and its 16-bit conversion. `variant` names a conversion: the
 * palette's contents for P_8 and the chroma key, which both change texels.
 * A surface holding a conversion lives in the surface cache below, found
 * through `surface_slot` (an index plus one) while `surface_stamp` still
 * matches the slot's.
 */
typedef struct v9x_glide_texture {
    v9x_u32 serial;
    v9x_u8 *raw;
    v9x_u32 sum;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 format;
    v9x_u16 *texels;
    v9x_u32 abi_format;
    v9x_u32 variant;
    v9x_u32 surface_slot;
    v9x_u32 surface_stamp;
} V9X_GLIDE_TEXTURE;

/*
 * Texture surfaces by content. NFS II SE downloads about 300 textures a
 * second in a race, mostly ones it downloaded before (census: 29,218
 * downloads of far fewer distinct textures), and the first race on the
 * netbook re-filled a surface for every one and ran at 5 frames a second
 * (2026-10-09). A surface is keyed by its texels' checksum, size, format
 * and variant, so a texture downloaded again finds the surface it already
 * has. When the cache is full the least recently bound surface goes.
 */
#define V9X_GLIDE_SURFACES 1024u

typedef struct v9x_glide_cached_surface {
    v9x_u32 in_use;
    v9x_u32 sum;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 format;
    v9x_u32 variant;
    v9x_u32 abi_format;
    void *surface;
    v9x_u32 stamp;
    v9x_u32 last_used;
} V9X_GLIDE_CACHED_SURFACE;

static V9X_GLIDE_TEXMEM v9x_glide_texmem;
static V9X_GLIDE_TEXTURE v9x_glide_textures[V9X_GLIDE_TEXMEM_RECORDS];
static V9X_GLIDE_CACHED_SURFACE v9x_glide_surfaces[V9X_GLIDE_SURFACES];
static v9x_u32 v9x_glide_surface_stamp;
static v9x_u32 v9x_glide_use_clock;
static int v9x_glide_source = -1;
static v9x_u32 v9x_glide_source_address;
static v9x_u32 v9x_glide_source_aspect = V9X_GLIDE_ASPECT_1X1;
static v9x_u32 v9x_glide_palette[V9X_GLIDE_PALETTE_ENTRIES];
static v9x_u32 v9x_glide_palette_sum;

/* Triangles held while nothing they are drawn with changes, as the ICD
 * holds them (gl_icd.c): every batch through the interface costs a lock,
 * a validation and a submission whatever its size. NFS II SE sends about
 * 2,300 triangles a frame one grDrawTriangle at a time (census). The
 * texture is kept with `levels` null and pointed at `level` when drawn. */
static struct {
    V9X_GLIDE_DRAW_SETUP setup;
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_LEVEL level;
    V9X_R3D_ABI_VERTEX vertices[3u * V9X_R3D_ABI_BATCH_MAX];
    v9x_u32 triangles;
} v9x_glide_pending;

static void *v9x_glide_alloc(v9x_u32 bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void v9x_glide_free(void *block)
{
    if (block != 0) {
        HeapFree(GetProcessHeap(), 0, block);
    }
}

/* The surface cache owns the surfaces; an entry only names one. */
static void v9x_glide_texture_free(V9X_GLIDE_TEXTURE *texture)
{
    v9x_glide_free(texture->raw);
    v9x_glide_free(texture->texels);
    v9x_glide_zero(texture, sizeof(*texture));
}

/* Surfaces go before the device that made them; the texels stay, so a
 * reopened device uploads them again. */
static void v9x_glide_textures_drop_surfaces(void)
{
    unsigned int i;

    for (i = 0u; i < V9X_GLIDE_SURFACES; ++i) {
        v9x_glide_hwtex_release(v9x_glide_surfaces[i].surface);
        v9x_glide_zero(&v9x_glide_surfaces[i], sizeof(v9x_glide_surfaces[i]));
    }
}

static v9x_u32 v9x_glide_sum_bytes(const v9x_u8 *bytes, v9x_u32 count)
{
    v9x_u32 sum = 2166136261ul;
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        sum = (sum ^ bytes[i]) * 16777619ul;
    }
    return sum;
}

/* A refused batch, logged in full the first few times: the state is what
 * says which combination the engine does not take. */
static void v9x_glide_log_refusal(v9x_u32 result, const V9X_R3D_ABI_DRAW *draw)
{
    const V9X_R3D_ABI_STATE *s = &draw->state;
    const V9X_R3D_ABI_TEXTURE *t = &draw->texture;

    v9x_glide_log3("draw refused: result=%lu triangles=%lu storage/format=%04lX",
                   result, draw->triangle_count, (t->storage << 8) | t->format);
    v9x_glide_log3("  ops color/alpha=%lu/%lu address/filters=%06lX",
                   t->color_op, t->alpha_op,
                   (t->address << 16) | (t->min_filter << 8) | t->mag_filter);
    v9x_glide_log3("  blend=%lu %lu/%lu", s->blend_enable, s->src_blend,
                   s->dst_blend);
    v9x_glide_log3("  alpha test=%lu func=%lu ref=%lu", s->alpha_test_enable,
                   s->alpha_func, s->alpha_ref);
    v9x_glide_log3("  fog=%lu depth enable/write/func=%03lX scissor-right=%lu",
                   s->fog_enable,
                   (s->depth_enable << 8) | (s->depth_write << 4) | s->depth_func,
                   s->scissor_right);
}

/*
 * One batch through the render interface, into the buffer grRenderBuffer
 * chose. STALE means a mode change started a new generation: describe
 * again and retry once. A fogged batch the engine refuses is drawn again
 * without fog and counted: whether an engine fogs through this interface
 * is measured per engine, not assumed.
 */
static v9x_u32 v9x_glide_submit(V9X_GLIDE_DRAW_SETUP *setup,
                                 const V9X_R3D_ABI_TEXTURE *texture,
                                 const V9X_R3D_ABI_VERTEX *vertices,
                                 v9x_u32 triangles)
{
    const V9X_R3D_INTERFACE *iface = v9x_glide_device_interface();
    V9X_R3D_ABI_DRAW draw;
    V9X_R3D_ABI_OUTCOME outcome;
    v9x_u32 result = V9X_R3D_RESULT_INVALID;
    v9x_u32 start;
    unsigned int attempt;

    if (iface == 0 || triangles == 0ul) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    v9x_glide_zero(&draw, sizeof(draw));
    draw.struct_bytes = sizeof(draw);
    draw.target.surface = v9x_glide_device_buffer(v9x_glide_render_buffer);
    draw.depth.surface = v9x_glide_device_buffer(V9X_GLIDE_BUFFER_AUX);
    draw.texture = *texture;
    draw.texture1.storage = V9X_R3D_ABI_TEXTURE_NONE;
    draw.state = setup->state;
    draw.vertices = vertices;
    draw.triangle_count = triangles;
    v9x_glide_device_wait_flip();
    start = v9x_glide_rdtsc_low();
    ++v9x_glide_prof_batches;
    for (attempt = 0u; attempt < 3u; ++attempt) {
        draw.generation = v9x_glide_device_generation();
        v9x_glide_zero(&outcome, sizeof(outcome));
        result = iface->draw(&draw, &outcome);
        if (result == V9X_R3D_RESULT_STALE && v9x_glide_device_redescribe()) {
            continue;
        }
        if (result == V9X_R3D_RESULT_UNSUPPORTED && draw.state.fog_enable) {
            draw.state.fog_enable = 0ul;
            v9x_glide_fog_dropped += triangles;
            continue;
        }
        break;
    }
    v9x_glide_prof_add(V9X_GLIDE_PROF_SUBMIT, v9x_glide_rdtsc_low() - start);
    if (result == V9X_R3D_RESULT_OK || result == V9X_R3D_RESULT_PARTIAL) {
        v9x_glide_drawn += outcome.submitted;
        return result;
    }
    if (v9x_glide_refusals_logged < 24ul) {
        ++v9x_glide_refusals_logged;
        v9x_glide_log_refusal(result, &draw);
    }
    v9x_glide_refused += triangles;
    return result;
}

static void v9x_glide_flush(void)
{
    V9X_R3D_ABI_TEXTURE texture;

    if (v9x_glide_pending.triangles == 0ul) {
        return;
    }
    texture = v9x_glide_pending.texture;
    if (texture.storage == V9X_R3D_ABI_TEXTURE_CPU) {
        texture.levels = &v9x_glide_pending.level;
        texture.level_count = 1ul;
    }
    (void)v9x_glide_submit(&v9x_glide_pending.setup, &texture,
                           v9x_glide_pending.vertices,
                           v9x_glide_pending.triangles);
    v9x_glide_pending.triangles = 0ul;
}

static void v9x_glide_queue(const V9X_GLIDE_DRAW_SETUP *setup,
                            const V9X_R3D_ABI_TEXTURE *texture,
                            const V9X_R3D_ABI_LEVEL *level,
                            const V9X_R3D_ABI_VERTEX *vertices,
                            v9x_u32 triangles)
{
    v9x_u32 i;

    if (v9x_glide_pending.triangles != 0ul) {
        /* Why a batch ends, counted: full, a texture change, or a state
         * change (the measure of what larger batches would need). */
        unsigned int reason = 0u;

        if (v9x_glide_pending.triangles + triangles > V9X_R3D_ABI_BATCH_MAX) {
            reason = 1u;
        } else if (!v9x_glide_same(&v9x_glide_pending.texture, texture,
                                   sizeof(*texture)) ||
                   !v9x_glide_same(&v9x_glide_pending.level, level,
                                   sizeof(*level))) {
            reason = 2u;
        } else if (!v9x_glide_same(&v9x_glide_pending.setup, setup,
                                   sizeof(*setup))) {
            reason = 3u;
        }
        if (reason != 0u) {
            ++v9x_glide_breaks[reason - 1u];
            v9x_glide_flush();
        }
    }
    if (v9x_glide_pending.triangles == 0ul) {
        v9x_glide_pending.setup = *setup;
        v9x_glide_pending.texture = *texture;
        v9x_glide_pending.level = *level;
    }
    for (i = 0ul; i < triangles * 3ul; ++i) {
        v9x_glide_pending.vertices[v9x_glide_pending.triangles * 3ul + i] =
            vertices[i];
    }
    v9x_glide_pending.triangles += triangles;
}

/* Why a textured draw had nothing to draw with, the first few times:
 * 1 no source (grTexSource named no download), 2 the record was
 * overwritten since, 3 its texels were not kept (format or mask), 4 the
 * conversion failed. */
static v9x_u32 v9x_glide_skips_logged;

static int v9x_glide_skip(v9x_u32 reason, v9x_u32 detail)
{
    if (v9x_glide_skips_logged < 16ul) {
        ++v9x_glide_skips_logged;
        v9x_glide_log3("textured draw skipped: reason=%lu detail=%08lX swap=%lu",
                       reason, detail, v9x_glide_swaps);
    }
    return 0;
}

/* The conversion the current palette and chroma key call for: the
 * palette by its contents, since NFS II SE loads the same seven palettes
 * thousands of times (census: 7,768 loads). Never zero. */
static v9x_u32 v9x_glide_variant(const V9X_GLIDE_TEXTURE *texture,
                                 v9x_u32 key_enable, v9x_u32 key_rgb)
{
    v9x_u32 variant = 0x80000000ul;

    if (texture->format == V9X_GLIDE_TEXFMT_P_8) {
        variant ^= v9x_glide_palette_sum;
    }
    if (key_enable) {
        variant ^= (key_rgb << 7) ^ (key_rgb >> 25) ^ 0x5A5A5A5Aul;
    }
    return variant != 0ul ? variant : 1ul;
}

/* The texels converted to `variant`, when they are not already. */
static int v9x_glide_texture_convert(V9X_GLIDE_TEXTURE *texture, v9x_u32 variant,
                                     v9x_u32 key_enable, v9x_u32 key_rgb)
{
    if (texture->variant == variant) {
        return 1;
    }
    /* A held batch may sample these texels. */
    v9x_glide_flush();
    if (texture->texels == 0) {
        texture->texels = (v9x_u16 *)v9x_glide_alloc(texture->width *
                                                     texture->height * 2ul);
    }
    if (texture->texels == 0 ||
        !v9x_glide_texfmt_convert(texture->format, texture->raw,
                                  texture->width * texture->height,
                                  v9x_glide_palette, key_enable, key_rgb,
                                  texture->texels, &texture->abi_format)) {
        return 0;
    }
    texture->variant = variant;
    return 1;
}

/* The cached surface holding `texture` converted to `variant`, made and
 * filled if none does; null when none can be had (then the CPU texels are
 * drawn). */
static V9X_GLIDE_CACHED_SURFACE *v9x_glide_surface_for(V9X_GLIDE_TEXTURE *texture,
                                                       v9x_u32 variant,
                                                       v9x_u32 key_enable,
                                                       v9x_u32 key_rgb)
{
    const V9X_R3D_ABI_DESCRIBE *describe = v9x_glide_device_description();
    V9X_GLIDE_CACHED_SURFACE *entry;
    unsigned int i;
    unsigned int slot = V9X_GLIDE_SURFACES;
    unsigned int oldest = 0u;
    v9x_u32 start;
    v9x_u32 edge = texture->width > texture->height ? texture->width :
                                                      texture->height;

    /* The one this entry used last, while nothing has replaced it. */
    if (texture->surface_slot != 0ul) {
        entry = &v9x_glide_surfaces[texture->surface_slot - 1ul];
        if (entry->in_use && entry->stamp == texture->surface_stamp &&
            entry->variant == variant) {
            entry->last_used = ++v9x_glide_use_clock;
            return entry;
        }
        texture->surface_slot = 0ul;
    }
    if (describe == 0 || describe->hw_texture_size_max < edge ||
        texture->width < describe->hw_texture_size_min ||
        texture->height < describe->hw_texture_size_min) {
        return 0;
    }

    /* The same texels already in a surface, from an earlier download. */
    for (i = 0u; i < V9X_GLIDE_SURFACES; ++i) {
        entry = &v9x_glide_surfaces[i];
        if (!entry->in_use) {
            if (slot == V9X_GLIDE_SURFACES) {
                slot = i;
            }
            continue;
        }
        if (entry->sum == texture->sum && entry->width == texture->width &&
            entry->height == texture->height && entry->format == texture->format &&
            entry->variant == variant) {
            entry->last_used = ++v9x_glide_use_clock;
            texture->surface_slot = (v9x_u32)i + 1ul;
            texture->surface_stamp = entry->stamp;
            ++v9x_glide_surface_hits;
            return entry;
        }
        if (entry->last_used < v9x_glide_surfaces[oldest].last_used ||
            !v9x_glide_surfaces[oldest].in_use) {
            oldest = i;
        }
    }

    /* A new one: converted, made, filled. */
    if (!v9x_glide_texture_convert(texture, variant, key_enable, key_rgb) ||
        (describe->texture_formats & (1ul << texture->abi_format)) == 0ul) {
        return 0;
    }
    if (slot == V9X_GLIDE_SURFACES) {
        slot = oldest;
        /* A held batch may sample the surface about to go. */
        v9x_glide_flush();
        v9x_glide_hwtex_release(v9x_glide_surfaces[slot].surface);
    }
    entry = &v9x_glide_surfaces[slot];
    v9x_glide_zero(entry, sizeof(*entry));
    start = v9x_glide_rdtsc_low();
    entry->surface = v9x_glide_hwtex_create(texture->width, texture->height,
                                            texture->abi_format);
    /*
     * Video memory runs out long before the table does: the netbook refused
     * a 64x64 surface (DDERR_OUTOFVIDEOMEMORY) once the menu's animated
     * 256x256 textures had filled it with frames already shown
     * (2026-10-09). The least recently bound surface goes, until this one
     * fits or none is left to give.
     */
    while (entry->surface == 0) {
        unsigned int victim = V9X_GLIDE_SURFACES;

        for (i = 0u; i < V9X_GLIDE_SURFACES; ++i) {
            if (i != slot && v9x_glide_surfaces[i].in_use &&
                (victim == V9X_GLIDE_SURFACES ||
                 v9x_glide_surfaces[i].last_used <
                     v9x_glide_surfaces[victim].last_used)) {
                victim = i;
            }
        }
        if (victim == V9X_GLIDE_SURFACES) {
            break;
        }
        v9x_glide_flush();
        v9x_glide_hwtex_release(v9x_glide_surfaces[victim].surface);
        v9x_glide_zero(&v9x_glide_surfaces[victim], sizeof(v9x_glide_surfaces[victim]));
        ++v9x_glide_surface_evictions;
        entry->surface = v9x_glide_hwtex_create(texture->width, texture->height,
                                                texture->abi_format);
    }
    if (entry->surface == 0 ||
        !v9x_glide_hwtex_upload(entry->surface, texture->texels, texture->width,
                                texture->height)) {
        v9x_glide_hwtex_release(entry->surface);
        entry->surface = 0;
        v9x_glide_prof_add(V9X_GLIDE_PROF_UPLOAD, v9x_glide_rdtsc_low() - start);
        return 0;
    }
    v9x_glide_prof_add(V9X_GLIDE_PROF_UPLOAD, v9x_glide_rdtsc_low() - start);
    ++v9x_glide_uploads;
    entry->in_use = 1ul;
    entry->sum = texture->sum;
    entry->width = texture->width;
    entry->height = texture->height;
    entry->format = texture->format;
    entry->variant = variant;
    entry->abi_format = texture->abi_format;
    entry->stamp = ++v9x_glide_surface_stamp;
    entry->last_used = ++v9x_glide_use_clock;
    texture->surface_slot = (v9x_u32)slot + 1ul;
    texture->surface_stamp = entry->stamp;
    return entry;
}

/*
 * The current source as the draw's texture: a cached surface when the
 * engine samples video memory and takes the size and format, the CPU
 * texels otherwise. Zero when there is nothing to draw with (no source, a
 * record overwritten since, a format not converted).
 */
static int v9x_glide_texture_bind(const V9X_GLIDE_DRAW_SETUP *setup,
                                  V9X_R3D_ABI_TEXTURE *out,
                                  V9X_R3D_ABI_LEVEL *level)
{
    const V9X_GLIDE_TEXREC *record;
    V9X_GLIDE_TEXTURE *texture;
    V9X_GLIDE_CACHED_SURFACE *entry;
    v9x_u32 key_rgb = v9x_glide_color_to_argb(v9x_glide_state.chroma_value,
                                              v9x_glide_color_format) &
                      0x00FFFFFFul;
    v9x_u32 variant;

    if (v9x_glide_source < 0 || v9x_glide_device_description() == 0) {
        return v9x_glide_skip(1ul, v9x_glide_source_address);
    }
    record = &v9x_glide_texmem.records[v9x_glide_source];
    texture = &v9x_glide_textures[v9x_glide_source];
    if (!record->in_use || texture->serial != record->serial) {
        return v9x_glide_skip(2ul, record->start);
    }
    if (texture->raw == 0) {
        return v9x_glide_skip(3ul, record->info.format);
    }
    variant = v9x_glide_variant(texture, setup->key_texture, key_rgb);

    v9x_glide_zero(out, sizeof(*out));
    v9x_glide_zero(level, sizeof(*level));
    out->min_filter = setup->min_filter;
    out->mag_filter = setup->mag_filter;
    out->mip = V9X_R3D_ABI_MIP_NONE;
    out->address = setup->address;
    out->color_op = setup->color_op;
    out->alpha_op = setup->alpha_op;

    entry = v9x_glide_surface_for(texture, variant, setup->key_texture, key_rgb);
    if (entry != 0) {
        out->storage = V9X_R3D_ABI_TEXTURE_HW;
        out->format = entry->abi_format;
        out->surface.surface = entry->surface;
        return 1;
    }
    if (!v9x_glide_texture_convert(texture, variant, setup->key_texture, key_rgb)) {
        return v9x_glide_skip(4ul, texture->format);
    }
    out->storage = V9X_R3D_ABI_TEXTURE_CPU;
    out->format = texture->abi_format;
    level->pixels = texture->texels;
    level->bytes = texture->width * texture->height * 2ul;
    level->pitch = texture->width * 2ul;
    level->width = texture->width;
    level->height = texture->height;
    return 1;
}

/* grTexDownloadMipMap's texels into the record's entry: the largest
 * level, which is the first in the game's data when the even/odd mask
 * includes it. NFS II SE downloads single levels with both (census). */
static void v9x_glide_texture_download(v9x_u32 address, v9x_u32 even_odd,
                                       const v9x_u32 *info)
{
    V9X_GLIDE_TEXINFO texinfo;
    V9X_GLIDE_TEXTURE *texture;
    const v9x_u8 *data;
    v9x_u32 parity;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 bytes;
    v9x_u32 i;
    int index;

    v9x_glide_flush();
    texinfo.small_lod = info[0];
    texinfo.large_lod = info[1];
    texinfo.aspect = info[2];
    texinfo.format = info[3];
    data = (const v9x_u8 *)info[4];
    index = v9x_glide_texmem_download(&v9x_glide_texmem, address, even_odd,
                                      &texinfo);

    /* Entries whose records the download overwrote, or that it reused. */
    for (i = 0ul; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        if (v9x_glide_textures[i].serial != 0ul &&
            (!v9x_glide_texmem.records[i].in_use ||
             v9x_glide_texmem.records[i].serial != v9x_glide_textures[i].serial)) {
            v9x_glide_texture_free(&v9x_glide_textures[i]);
        }
    }
    if (index < 0) {
        return;
    }
    texture = &v9x_glide_textures[index];
    texture->serial = v9x_glide_texmem.records[index].serial;
    parity = (texinfo.large_lod & 1ul) ? V9X_GLIDE_MIPMAPLEVELMASK_ODD :
                                         V9X_GLIDE_MIPMAPLEVELMASK_EVEN;
    if (data == 0 || (even_odd & parity) == 0ul ||
        !v9x_glide_texfmt_supported(texinfo.format) ||
        !v9x_glide_level_size(texinfo.large_lod, texinfo.aspect, &width, &height)) {
        return;
    }
    bytes = width * height * (texinfo.format >= V9X_GLIDE_TEXFMT_16BIT ? 2ul : 1ul);
    texture->raw = (v9x_u8 *)v9x_glide_alloc(bytes);
    if (texture->raw == 0) {
        return;
    }
    for (i = 0ul; i < bytes; ++i) {
        texture->raw[i] = data[i];
    }
    texture->sum = v9x_glide_sum_bytes(texture->raw, bytes);
    texture->width = width;
    texture->height = height;
    texture->format = texinfo.format;
}

static void v9x_glide_vertex_setup(const V9X_GLIDE_DRAW_SETUP *draw,
                                   V9X_GLIDE_VERTEX_SETUP *vs)
{
    vs->origin = v9x_glide_state.origin;
    vs->height = (float)v9x_glide_state.height;
    vs->depth_mode = v9x_glide_state.depth_mode;
    if (!draw->textured ||
        !v9x_glide_texture_scales(v9x_glide_source_aspect, &vs->s_scale,
                                  &vs->t_scale)) {
        vs->s_scale = 1.0f / (float)V9X_GLIDE_LOD_EDGE;
        vs->t_scale = 1.0f / (float)V9X_GLIDE_LOD_EDGE;
    }
    vs->color_source = draw->color_source;
    vs->alpha_source = draw->alpha_source;
    vs->constant_argb = v9x_glide_color_to_argb(v9x_glide_state.constant_color,
                                                v9x_glide_color_format);
    vs->fog_mode = v9x_glide_state.fog_mode & V9X_GLIDE_FOG_SOURCE_MASK;
    vs->fog_table = v9x_glide_fog;
}

/*
 * One triangle cut to the clip window and queued. The hardware engines
 * refuse a draw whose scissor is not the whole target - Gen3 refused
 * NFS II SE's HUD panes outright (netbook, 2026-10-09) - so the window is
 * applied to the geometry here, as the ICD applies GL's scissor box, and
 * the draw carries the whole target. Clipping to it also keeps vertices
 * inside the surface, which some engines require.
 */
static void v9x_glide_queue_clipped(const V9X_GLIDE_DRAW_SETUP *setup,
                                    const V9X_R3D_ABI_TEXTURE *texture,
                                    const V9X_R3D_ABI_LEVEL *level,
                                    const V9X_R3D_ABI_VERTEX *triangle)
{
    V9X_GLIDE_DRAW_SETUP whole = *setup;
    V9X_R3D_ABI_VERTEX pieces[3u * V9X_GLIDE_CLIP_TRIANGLES_MAX];
    unsigned int count;

    /* The caller's setup keeps its window: a line queues two triangles
     * with it, and the first run of this cut only the first (the track
     * map's route spilled out of its pane, netbook, 2026-10-09). */
    count = v9x_glide_clip_rect(triangle, (float)setup->state.scissor_left,
                                (float)setup->state.scissor_top,
                                (float)setup->state.scissor_right,
                                (float)setup->state.scissor_bottom, pieces);
    whole.state.scissor_left = 0ul;
    whole.state.scissor_top = 0ul;
    whole.state.scissor_right = v9x_glide_state.width;
    whole.state.scissor_bottom = v9x_glide_state.height;
    if (count == 0u) {
        ++v9x_glide_culled;
        return;
    }
    v9x_glide_queue(&whole, texture, level, pieces, (v9x_u32)count);
}

/* The draw's setup and texture, or zero when it cannot be drawn. */
static int v9x_glide_prepare(V9X_GLIDE_DRAW_SETUP *setup,
                             V9X_R3D_ABI_TEXTURE *texture,
                             V9X_R3D_ABI_LEVEL *level)
{
    v9x_u32 start = v9x_glide_rdtsc_low();
    int ok = 1;

    v9x_glide_state_map(&v9x_glide_state, setup);
    v9x_glide_note_unrecognized(setup);
    if (!setup->textured) {
        v9x_glide_zero(texture, sizeof(*texture));
        v9x_glide_zero(level, sizeof(*level));
        texture->storage = V9X_R3D_ABI_TEXTURE_NONE;
    } else if (!v9x_glide_texture_bind(setup, texture, level)) {
        ++v9x_glide_skipped_textured;
        ok = 0;
    }
    v9x_glide_prof_add(V9X_GLIDE_PROF_PREPARE, v9x_glide_rdtsc_low() - start);
    return ok;
}

static void v9x_glide_draw_triangle(const float *a, const float *b,
                                    const float *c)
{
    V9X_GLIDE_DRAW_SETUP setup;
    V9X_GLIDE_VERTEX_SETUP vs;
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_LEVEL level;
    V9X_R3D_ABI_VERTEX v[3];

    if (a == 0 || b == 0 || c == 0 || !v9x_glide_prepare(&setup, &texture, &level)) {
        return;
    }
    v9x_glide_vertex_setup(&setup, &vs);
    v9x_glide_vertex_convert(&vs, a, &v[0]);
    v9x_glide_vertex_convert(&vs, b, &v[1]);
    v9x_glide_vertex_convert(&vs, c, &v[2]);
    if (!v9x_glide_cull_keep(v9x_glide_state.cull_mode, v9x_glide_state.origin,
                             &v[0], &v[1], &v[2])) {
        ++v9x_glide_culled;
        return;
    }
    v9x_glide_queue_clipped(&setup, &texture, &level, v);
}

static void v9x_glide_draw_line(const float *a, const float *b)
{
    V9X_GLIDE_DRAW_SETUP setup;
    V9X_GLIDE_VERTEX_SETUP vs;
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_LEVEL level;
    V9X_R3D_ABI_VERTEX ends[2];
    V9X_R3D_ABI_VERTEX v[6];

    if (a == 0 || b == 0 || !v9x_glide_prepare(&setup, &texture, &level)) {
        return;
    }
    v9x_glide_vertex_setup(&setup, &vs);
    v9x_glide_vertex_convert(&vs, a, &ends[0]);
    v9x_glide_vertex_convert(&vs, b, &ends[1]);
    v9x_glide_line_triangles(&ends[0], &ends[1], v);
    v9x_glide_queue_clipped(&setup, &texture, &level, &v[0]);
    v9x_glide_queue_clipped(&setup, &texture, &level, &v[3]);
}

/*
 * grBufferClear: the colour buffer always, the depth buffer when depth
 * buffering is on and writable (Reference Manual), inside the clip window.
 *
 * Drawn, not cleared. The render interface's clear waits for the engine
 * and writes both buffers with the CPU: on the netbook, through the
 * uncached aperture, that was 53-64% of every NFS II SE frame (2026-10-09).
 * DirectDraw's blitter fills (DDBLT_COLORFILL) were cheap but flickered:
 * with the flip waited for, with everything drained before and with the
 * fill waited for after, Michael still saw it; the colour fill alone did
 * it, and the CPU clear did not. So the clear is one rectangle through the
 * engine that draws everything else, in order with it by construction:
 * the clear colour, depth test ALWAYS, depth writes on at the clear depth.
 * The interface's clear stays as the fallback when that draw is refused.
 */
static void v9x_glide_clear(v9x_u32 color, v9x_u32 depth)
{
    const V9X_R3D_INTERFACE *iface = v9x_glide_device_interface();
    V9X_GLIDE_DRAW_SETUP setup;
    V9X_R3D_ABI_TEXTURE none;
    V9X_R3D_ABI_VERTEX quad[6];
    V9X_R3D_ABI_CLEAR clear;
    V9X_R3D_ABI_RECT rect;
    v9x_u32 result = V9X_R3D_RESULT_INVALID;
    v9x_u32 start;
    float left;
    float top;
    float right;
    float bottom;
    unsigned int attempt;
    unsigned int i;

    if (iface == 0) {
        return;
    }
    v9x_glide_flush();
    v9x_glide_state_map(&v9x_glide_state, &setup);
    rect.left = setup.state.scissor_left;
    rect.top = setup.state.scissor_top;
    rect.right = setup.state.scissor_right;
    rect.bottom = setup.state.scissor_bottom;
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    v9x_glide_zero(&clear, sizeof(clear));
    clear.struct_bytes = sizeof(clear);
    clear.target.surface = v9x_glide_device_buffer(v9x_glide_render_buffer);
    clear.depth.surface = v9x_glide_device_buffer(V9X_GLIDE_BUFFER_AUX);
    clear.clear_color = 1ul;
    clear.color_value = v9x_glide_color_to_argb(color, v9x_glide_color_format) &
                        0x00FFFFFFul;
    clear.write_mask = V9X_R3D_ABI_WRITE_RGB;
    if (clear.depth.surface != 0 &&
        v9x_glide_state.depth_mode != V9X_GLIDE_DEPTH_DISABLE &&
        v9x_glide_state.depth_mask) {
        clear.clear_depth = 1ul;
        clear.write_depth = 1ul;
        clear.depth_value = depth & 0xFFFFul;
    }
    clear.rects = &rect;
    clear.rect_count = 1ul;
    start = v9x_glide_rdtsc_low();

    /* The rectangle as two triangles, at the clear depth (0xFFFF is 1.0,
     * the far plane, in both buffer modes: glide_vertex.c). */
    v9x_glide_zero(&setup.state, sizeof(setup.state));
    setup.state.depth_enable = clear.clear_depth;
    setup.state.depth_write = clear.clear_depth;
    setup.state.depth_func = V9X_GLIDE_CMP_ALWAYS + 1ul;
    setup.state.src_blend = 2ul;            /* D3DBLEND_ONE */
    setup.state.dst_blend = 1ul;            /* D3DBLEND_ZERO */
    setup.state.write_mask = V9X_R3D_ABI_WRITE_RGB;
    setup.state.scissor_right = v9x_glide_state.width;
    setup.state.scissor_bottom = v9x_glide_state.height;
    v9x_glide_zero(&none, sizeof(none));
    none.storage = V9X_R3D_ABI_TEXTURE_NONE;
    left = (float)rect.left;
    top = (float)rect.top;
    right = (float)rect.right;
    bottom = (float)rect.bottom;
    for (i = 0u; i < 6u; ++i) {
        quad[i].sz = (float)clear.depth_value / 65535.0f;
        quad[i].rhw = 1.0f;
        quad[i].color = 0xFF000000ul | clear.color_value;
        quad[i].specular = 0xFF000000ul;
        quad[i].tu = 0.0f;
        quad[i].tv = 0.0f;
    }
    quad[0].sx = left;  quad[0].sy = top;
    quad[1].sx = right; quad[1].sy = top;
    quad[2].sx = left;  quad[2].sy = bottom;
    quad[3].sx = right; quad[3].sy = top;
    quad[4].sx = right; quad[4].sy = bottom;
    quad[5].sx = left;  quad[5].sy = bottom;
    result = v9x_glide_submit(&setup, &none, quad, 2ul);
    if (result == V9X_R3D_RESULT_OK || result == V9X_R3D_RESULT_PARTIAL) {
        v9x_glide_prof_add(V9X_GLIDE_PROF_CLEAR, v9x_glide_rdtsc_low() - start);
        return;
    }

    v9x_glide_device_wait_flip();
    for (attempt = 0u; attempt < 2u; ++attempt) {
        clear.generation = v9x_glide_device_generation();
        result = iface->clear(&clear);
        if (result != V9X_R3D_RESULT_STALE || !v9x_glide_device_redescribe()) {
            break;
        }
    }
    v9x_glide_prof_add(V9X_GLIDE_PROF_CLEAR, v9x_glide_rdtsc_low() - start);
    if (result != V9X_R3D_RESULT_OK && v9x_glide_refusals_logged < 24ul) {
        ++v9x_glide_refusals_logged;
        v9x_glide_log3("clear refused: result=%lu", result, 0ul, 0ul);
    }
}

/* Before the device goes: what is held, then the surfaces it made. */
static void v9x_glide_close(void)
{
    v9x_glide_flush();
    v9x_glide_textures_drop_surfaces();
    v9x_glide_device_close();
}

/* ---- initialisation and the board ---------------------------------- */

void __stdcall grGlideInit(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grGlideInit);

    v9x_glide_logf(V9X_GLIDE_IX_grGlideInit, call, "");
}

void __stdcall grGlideShutdown(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grGlideShutdown);

    v9x_glide_logf(V9X_GLIDE_IX_grGlideShutdown, call, "");
    v9x_glide_summary("shutdown");
    /* A title may shut down without grSstWinClose (plan, hazards): the
     * display mode is given back either way. */
    v9x_glide_close();
}

void __stdcall grGlideGetVersion(char *version)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grGlideGetVersion);
    const char *source = V9X_GLIDE_VERSION_STRING;
    unsigned int index = 0u;

    if (version != 0) {
        while (source[index] != '\0') {
            version[index] = source[index];
            ++index;
        }
        version[index] = '\0';
    }
    v9x_glide_logf(V9X_GLIDE_IX_grGlideGetVersion, call, "buffer=%08lX -> %s",
                   (v9x_u32)version, (const char *)V9X_GLIDE_VERSION_STRING);
}

v9x_u32 __stdcall grSstQueryHardware(v9x_u32 *config)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstQueryHardware);

    if (config != 0) {
        config[0] = 1ul;
        config[1] = V9X_GLIDE_SSTTYPE_VOODOO;
        config[2] = V9X_GLIDE_FB_MIB;
        config[3] = V9X_GLIDE_FBI_REV;
        config[4] = V9X_GLIDE_TMU_COUNT;
        config[5] = 0ul;
        config[6] = V9X_GLIDE_TMU_REV;
        config[7] = V9X_GLIDE_TMU_MIB;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grSstQueryHardware, call,
                   "config=%08lX -> voodoo fb=%luMiB tmus=%lu tmu0=%luMiB",
                   (v9x_u32)config, V9X_GLIDE_FB_MIB, V9X_GLIDE_TMU_COUNT,
                   V9X_GLIDE_TMU_MIB);
    return V9X_GLIDE_TRUE;
}

void __stdcall grSstSelect(v9x_u32 which)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstSelect);

    v9x_glide_logf(V9X_GLIDE_IX_grSstSelect, call, "sst=%lu", which);
}

v9x_u32 __stdcall grSstWinOpen(v9x_u32 window, v9x_u32 resolution,
                               v9x_u32 refresh, v9x_u32 color_format,
                               v9x_u32 origin, v9x_u32 color_buffers,
                               v9x_u32 aux_buffers)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstWinOpen);
    v9x_u32 width = 0ul;
    v9x_u32 height = 0ul;
    v9x_u32 opened = V9X_GLIDE_FALSE;

    if (v9x_glide_resolution_size(resolution, &width, &height)) {
        v9x_glide_color_format = color_format;
        v9x_glide_render_buffer = V9X_GLIDE_BUFFER_BACK;
        v9x_glide_state_init(&v9x_glide_state, width, height, origin);
        v9x_glide_close();
        opened = v9x_glide_device_open((void *)window, width, height,
                                       color_buffers, aux_buffers) ?
                 V9X_GLIDE_TRUE : V9X_GLIDE_FALSE;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grSstWinOpen, call,
                   "hwnd=%08lX res=%lu refresh=%lu cformat=%lu origin=%lu colbuf=%lu auxbuf=%lu -> %lu",
                   window, resolution, refresh, color_format, origin,
                   color_buffers, aux_buffers, opened);
    return opened;
}

void __stdcall grSstWinClose(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstWinClose);

    v9x_glide_logf(V9X_GLIDE_IX_grSstWinClose, call, "");
    v9x_glide_summary("winclose");
    v9x_glide_close();
}

void __stdcall grSstIdle(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstIdle);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grSstIdle, call, "");
    }
}

v9x_u32 __stdcall grSstIsBusy(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstIsBusy);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grSstIsBusy, call, "-> 0");
    }
    return V9X_GLIDE_FALSE;
}

v9x_u32 __stdcall grSstStatus(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstStatus);
    v9x_u32 status;

    v9x_glide_status_toggle ^= V9X_GLIDE_STATUS_VRETRACE;
    status = V9X_GLIDE_STATUS_FIFO_FREE | v9x_glide_status_toggle;
    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grSstStatus, call, "-> %08lX", status);
    }
    return status;
}

/* ---- buffers ------------------------------------------------------- */

void __stdcall grBufferClear(v9x_u32 color, v9x_u32 alpha, v9x_u32 depth)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grBufferClear);

    if (v9x_glide_noted(V9X_GLIDE_IX_grBufferClear,
                        v9x_glide_key(color, alpha, depth, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grBufferClear, call,
                       "color=%08lX alpha=%02lX depth=%04lX", color,
                       alpha & 0xfful, depth & 0xfffful);
    }
    v9x_glide_clear(color, depth);
}

void __stdcall grBufferSwap(v9x_u32 interval)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grBufferSwap);
    v9x_u32 now;

    ++v9x_glide_swaps;
    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grBufferSwap, call, "interval=%lu",
                       interval);
    }
    if (v9x_glide_device_is_open()) {
        v9x_u32 start;

        v9x_glide_flush();
        start = v9x_glide_rdtsc_low();
        (void)v9x_glide_device_swap(interval);
        v9x_glide_prof_add(V9X_GLIDE_PROF_SWAP, v9x_glide_rdtsc_low() - start);
    } else if (interval != 0ul) {
        Sleep(interval * V9X_GLIDE_FRAME_MS);
    }
    {
        v9x_u32 mark = v9x_glide_rdtsc_low();

        if (v9x_glide_prof_swap_mark != 0ul) {
            v9x_glide_prof_add(V9X_GLIDE_PROF_FRAME, mark - v9x_glide_prof_swap_mark);
        }
        v9x_glide_prof_swap_mark = mark;
    }
    now = GetTickCount();
    if (now - v9x_glide_summary_tick >= V9X_GLIDE_SUMMARY_MS) {
        v9x_glide_summary_tick = now;
        v9x_glide_summary("periodic");
    }

    /* The frame that starts now is captured whole, or not at all. */
    v9x_glide_capture_lines = 0ul;
    v9x_glide_capturing = (v9x_glide_swaps < V9X_GLIDE_CAPTURE_FIRST ||
                           (v9x_glide_swaps % V9X_GLIDE_CAPTURE_EVERY) == 0ul) ?
                          1ul : 0ul;
    if (v9x_glide_capturing) {
        char text[64];

        wsprintfA(text, "capture frame after swap %lu", v9x_glide_swaps);
        v9x_glide_log(text);
    }
}

void __stdcall grRenderBuffer(v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grRenderBuffer);

    if (v9x_glide_noted(V9X_GLIDE_IX_grRenderBuffer, buffer)) {
        v9x_glide_logf(V9X_GLIDE_IX_grRenderBuffer, call, "buffer=%lu", buffer);
    }
    if (buffer == V9X_GLIDE_BUFFER_FRONT || buffer == V9X_GLIDE_BUFFER_BACK) {
        v9x_glide_flush();
        v9x_glide_render_buffer = buffer;
    }
}

void __stdcall grClipWindow(v9x_u32 min_x, v9x_u32 min_y, v9x_u32 max_x,
                            v9x_u32 max_y)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grClipWindow);

    if (v9x_glide_noted(V9X_GLIDE_IX_grClipWindow,
                        v9x_glide_key(min_x, min_y, max_x, max_y))) {
        v9x_glide_logf(V9X_GLIDE_IX_grClipWindow, call, "min=%lu,%lu max=%lu,%lu",
                       min_x, min_y, max_x, max_y);
    }
    v9x_glide_state.clip_min_x = min_x;
    v9x_glide_state.clip_min_y = min_y;
    v9x_glide_state.clip_max_x = max_x;
    v9x_glide_state.clip_max_y = max_y;
}

/* ---- render state -------------------------------------------------- */

/*
 * The one-argument state calls share a shape: log the value under the
 * rules above, then store it where the state mapping reads it (`store`,
 * a statement on `value`). A macro rather than a helper so each export
 * keeps its own index and its own name in the log.
 */
#define V9X_GLIDE_STATE1(name, label, store)                                 \
    void __stdcall name(v9x_u32 value)                                       \
    {                                                                        \
        v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_##name);                 \
                                                                             \
        if (v9x_glide_noted(V9X_GLIDE_IX_##name, value)) {                   \
            v9x_glide_logf(V9X_GLIDE_IX_##name, call, label "=%08lX", value); \
        }                                                                    \
        store;                                                               \
    }

V9X_GLIDE_STATE1(grCullMode, "mode", v9x_glide_state.cull_mode = value)
/* Not used by NFS II SE (census); no render-interface field. */
V9X_GLIDE_STATE1(grDepthBiasLevel, "bias", (void)value)
V9X_GLIDE_STATE1(grDepthBufferFunction, "func",
                 v9x_glide_state.depth_func = value)
V9X_GLIDE_STATE1(grDepthBufferMode, "mode", v9x_glide_state.depth_mode = value)
V9X_GLIDE_STATE1(grDepthMask, "enable", v9x_glide_state.depth_mask = value)
/* The engines dither 16-bit targets as they choose. */
V9X_GLIDE_STATE1(grDitherMode, "mode", (void)value)
V9X_GLIDE_STATE1(grAlphaTestFunction, "func", v9x_glide_state.alpha_func = value)
V9X_GLIDE_STATE1(grAlphaTestReferenceValue, "ref",
                 v9x_glide_state.alpha_ref = value)
V9X_GLIDE_STATE1(grChromakeyMode, "mode", v9x_glide_state.chroma_mode = value)
V9X_GLIDE_STATE1(grChromakeyValue, "color",
                 v9x_glide_state.chroma_value = value)
V9X_GLIDE_STATE1(grFogColorValue, "color", v9x_glide_state.fog_color = value)
V9X_GLIDE_STATE1(grFogMode, "mode", v9x_glide_state.fog_mode = value)
/* Gamma has no render-interface field yet (census, open). */
V9X_GLIDE_STATE1(grGammaCorrectionValue, "gamma-bits", (void)value)

void __stdcall grAlphaBlendFunction(v9x_u32 rgb_src, v9x_u32 rgb_dst,
                                  v9x_u32 alpha_src, v9x_u32 alpha_dst)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grAlphaBlendFunction);

    if (v9x_glide_noted(V9X_GLIDE_IX_grAlphaBlendFunction,
                        v9x_glide_key(rgb_src, rgb_dst, alpha_src, alpha_dst))) {
        v9x_glide_logf(V9X_GLIDE_IX_grAlphaBlendFunction, call,
                       "rgb=%lu,%lu alpha=%lu,%lu", rgb_src, rgb_dst,
                       alpha_src, alpha_dst);
    }
    /* The alpha factors blend a destination alpha the 16-bit targets do
     * not have. */
    v9x_glide_state.blend_src = rgb_src;
    v9x_glide_state.blend_dst = rgb_dst;
}

static void v9x_glide_store_combine(V9X_GLIDE_COMBINE *combine,
                                    v9x_u32 function, v9x_u32 factor,
                                    v9x_u32 local, v9x_u32 other,
                                    v9x_u32 invert)
{
    combine->function = function;
    combine->factor = factor;
    combine->local = local;
    combine->other = other;
    combine->invert = invert;
}

/* grColorCombine and grAlphaCombine: function, factor, local, other,
 * invert (Reference Manual). */
void __stdcall grColorCombine(v9x_u32 function, v9x_u32 factor, v9x_u32 local,
                              v9x_u32 other, v9x_u32 invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grColorCombine);

    if (v9x_glide_noted(V9X_GLIDE_IX_grColorCombine,
                        v9x_glide_key(function, factor, local,
                                      other ^ (invert << 16)))) {
        v9x_glide_logf(V9X_GLIDE_IX_grColorCombine, call,
                       "func=%lu factor=%lu local=%lu other=%lu invert=%lu",
                       function, factor, local, other, invert);
    }
    v9x_glide_store_combine(&v9x_glide_state.color, function, factor, local,
                            other, invert);
}

void __stdcall grAlphaCombine(v9x_u32 function, v9x_u32 factor, v9x_u32 local,
                              v9x_u32 other, v9x_u32 invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grAlphaCombine);

    if (v9x_glide_noted(V9X_GLIDE_IX_grAlphaCombine,
                        v9x_glide_key(function, factor, local,
                                      other ^ (invert << 16)))) {
        v9x_glide_logf(V9X_GLIDE_IX_grAlphaCombine, call,
                       "func=%lu factor=%lu local=%lu other=%lu invert=%lu",
                       function, factor, local, other, invert);
    }
    v9x_glide_store_combine(&v9x_glide_state.alpha, function, factor, local,
                            other, invert);
}

void __stdcall grFogTable(const v9x_u32 *table)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grFogTable);
    v9x_u32 key;
    unsigned int index;

    if (table == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grFogTable, call, "table=null");
        return;
    }
    for (index = 0u; index < V9X_GLIDE_FOG_TABLE_SIZE; ++index) {
        v9x_glide_fog[index] = ((const v9x_u8 *)table)[index];
    }
    key = v9x_glide_checksum(table, V9X_GLIDE_FOG_TABLE_SIZE / 4u);
    if (!v9x_glide_noted(V9X_GLIDE_IX_grFogTable, key)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grFogTable, call,
                   "0-31 %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
                   table[0], table[1], table[2], table[3], table[4], table[5],
                   table[6], table[7]);
    v9x_glide_logf(V9X_GLIDE_IX_grFogTable, call,
                   "32-63 %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
                   table[8], table[9], table[10], table[11], table[12],
                   table[13], table[14], table[15]);
}

/* guFogGenerateExp fills a 64-entry table. The census build leaves it
 * zero (no fog) rather than compute exp without a C runtime; the density
 * it was asked for is what the log needs. */
void __stdcall guFogGenerateExp(v9x_u8 *table, v9x_u32 density)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_guFogGenerateExp);
    unsigned int index;

    if (table != 0) {
        for (index = 0u; index < V9X_GLIDE_FOG_TABLE_SIZE; ++index) {
            table[index] = 0u;
        }
    }
    v9x_glide_logf(V9X_GLIDE_IX_guFogGenerateExp, call,
                   "table=%08lX density-bits=%08lX", (v9x_u32)table, density);
}

/* ---- textures ------------------------------------------------------ */

void __stdcall grTexClampMode(v9x_u32 tmu, v9x_u32 s_mode, v9x_u32 t_mode)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexClampMode);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexClampMode,
                        v9x_glide_key(tmu, s_mode, t_mode, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexClampMode, call, "tmu=%lu s=%lu t=%lu",
                       tmu, s_mode, t_mode);
    }
    v9x_glide_state.clamp_s = s_mode;
    v9x_glide_state.clamp_t = t_mode;
}

void __stdcall grTexFilterMode(v9x_u32 tmu, v9x_u32 min_filter,
                               v9x_u32 mag_filter)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexFilterMode);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexFilterMode,
                        v9x_glide_key(tmu, min_filter, mag_filter, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexFilterMode, call,
                       "tmu=%lu min=%lu mag=%lu", tmu, min_filter, mag_filter);
    }
    v9x_glide_state.min_filter = min_filter;
    v9x_glide_state.mag_filter = mag_filter;
}

void __stdcall grTexMipMapMode(v9x_u32 tmu, v9x_u32 mode, v9x_u32 lod_blend)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexMipMapMode);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexMipMapMode,
                        v9x_glide_key(tmu, mode, lod_blend, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexMipMapMode, call,
                       "tmu=%lu mode=%lu lodblend=%lu", tmu, mode, lod_blend);
    }
}

/* grTexCombine: tmu, rgb function, rgb factor, alpha function, alpha
 * factor, rgb invert, alpha invert (Reference Manual). */
void __stdcall grTexCombine(v9x_u32 tmu, v9x_u32 rgb_function,
                            v9x_u32 rgb_factor, v9x_u32 alpha_function,
                            v9x_u32 alpha_factor, v9x_u32 rgb_invert,
                            v9x_u32 alpha_invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexCombine);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexCombine,
                        v9x_glide_key(tmu ^ (rgb_invert << 8) ^ (alpha_invert << 9),
                                      rgb_function, rgb_factor,
                                      alpha_function ^ (alpha_factor << 16)))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexCombine, call,
                       "tmu=%lu rgb=%lu,%lu alpha=%lu,%lu invert=%lu,%lu", tmu,
                       rgb_function, rgb_factor, alpha_function, alpha_factor,
                       rgb_invert, alpha_invert);
    }
    /* With one TMU there is no "other" texture: the unit's output is the
     * texel exactly when its function is LOCAL and nothing is inverted. */
    v9x_glide_state.tex_rgb_function = rgb_invert ?
        V9X_GLIDE_COMBINE_FUNCTION_OTHER : rgb_function;
    v9x_glide_state.tex_alpha_function = alpha_invert ?
        V9X_GLIDE_COMBINE_FUNCTION_OTHER : alpha_function;
}

void __stdcall grTexCombineFunction(v9x_u32 tmu, v9x_u32 function)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexCombineFunction);
    v9x_u32 unit;

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexCombineFunction,
                        v9x_glide_key(tmu, function, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexCombineFunction, call,
                       "tmu=%lu func=%lu", tmu, function);
    }
    unit = function == V9X_GLIDE_TEXTURECOMBINE_DECAL ?
           V9X_GLIDE_COMBINE_FUNCTION_LOCAL : V9X_GLIDE_COMBINE_FUNCTION_OTHER;
    v9x_glide_state.tex_rgb_function = unit;
    v9x_glide_state.tex_alpha_function = unit;
}

v9x_u32 __stdcall grTexMinAddress(v9x_u32 tmu)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexMinAddress);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexMinAddress, tmu)) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexMinAddress, call, "tmu=%lu -> %08lX",
                       tmu, V9X_GLIDE_TEX_MIN_ADDRESS);
    }
    return V9X_GLIDE_TEX_MIN_ADDRESS;
}

v9x_u32 __stdcall grTexMaxAddress(v9x_u32 tmu)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexMaxAddress);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexMaxAddress, tmu)) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexMaxAddress, call, "tmu=%lu -> %08lX",
                       tmu, V9X_GLIDE_TEX_MAX_ADDRESS);
    }
    return V9X_GLIDE_TEX_MAX_ADDRESS;
}

/* lodmin is the smallest level (the larger GrLOD_t number), lodmax the
 * largest; glide_texmem.c takes the order from the values. */
v9x_u32 __stdcall grTexCalcMemRequired(v9x_u32 lodmin, v9x_u32 lodmax,
                                       v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexCalcMemRequired);
    V9X_GLIDE_TEXINFO texinfo;
    v9x_u32 total;

    texinfo.small_lod = lodmin;
    texinfo.large_lod = lodmax;
    texinfo.aspect = aspect;
    texinfo.format = format;
    total = v9x_glide_texmem_required(&texinfo, V9X_GLIDE_MIPMAPLEVELMASK_BOTH);
    if (v9x_glide_noted(V9X_GLIDE_IX_grTexCalcMemRequired,
                        v9x_glide_key(lodmin, lodmax, aspect, format))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexCalcMemRequired, call,
                       "lodmin=%lu lodmax=%lu aspect=%lu format=%lu -> %lu",
                       lodmin, lodmax, aspect, format, total);
    }
    return total;
}

/* GrTexInfo: smallLod, largeLod, aspectRatio, format, data. */
static v9x_u32 v9x_glide_info_key(v9x_u32 address, const v9x_u32 *info)
{
    if (info == 0) {
        return address;
    }
    return v9x_glide_key(address, info[0] ^ (info[1] << 8), info[2], info[3]);
}

void __stdcall grTexSource(v9x_u32 tmu, v9x_u32 address, v9x_u32 even_odd,
                           const v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexSource);
    V9X_GLIDE_TEXINFO texinfo;

    /* The record the draws sample until the next grTexSource; one TMU. */
    v9x_glide_source = -1;
    v9x_glide_source_address = address;
    if (info != 0) {
        texinfo.small_lod = info[0];
        texinfo.large_lod = info[1];
        texinfo.aspect = info[2];
        texinfo.format = info[3];
        v9x_glide_source = v9x_glide_texmem_find(&v9x_glide_texmem, address,
                                                 even_odd, &texinfo);
        v9x_glide_source_aspect = texinfo.aspect;
    }
    if (!v9x_glide_noted(V9X_GLIDE_IX_grTexSource,
                         v9x_glide_info_key(address ^ (even_odd << 28), info))) {
        return;
    }
    if (info == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexSource, call,
                       "tmu=%lu addr=%08lX evenodd=%lu info=null", tmu, address,
                       even_odd);
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grTexSource, call,
                   "tmu=%lu addr=%08lX evenodd=%lu lod=%lu..%lu aspect=%lu format=%lu",
                   tmu, address, even_odd, info[1], info[0], info[2], info[3]);
}

void __stdcall grTexDownloadMipMap(v9x_u32 tmu, v9x_u32 address,
                                   v9x_u32 even_odd, const v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexDownloadMipMap);

    if (info != 0) {
        v9x_glide_texture_download(address, even_odd, info);
    }
    if (!v9x_glide_noted(V9X_GLIDE_IX_grTexDownloadMipMap,
                         v9x_glide_info_key(address ^ (even_odd << 28), info))) {
        return;
    }
    if (info == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexDownloadMipMap, call,
                       "tmu=%lu addr=%08lX evenodd=%lu info=null", tmu, address,
                       even_odd);
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grTexDownloadMipMap, call,
                   "tmu=%lu addr=%08lX evenodd=%lu lod=%lu..%lu aspect=%lu format=%lu data=%08lX",
                   tmu, address, even_odd, info[1], info[0], info[2], info[3],
                   info[4]);
}

void __stdcall grTexDownloadTable(v9x_u32 tmu, v9x_u32 type,
                                  const v9x_u32 *data)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexDownloadTable);
    unsigned int words = type == V9X_GLIDE_TEXTABLE_PALETTE ?
                         V9X_GLIDE_PALETTE_ENTRIES : V9X_GLIDE_NCC_TABLE_DWORDS;
    v9x_u32 sum;

    if (data == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexDownloadTable, call,
                       "tmu=%lu type=%lu data=null", tmu, type);
        return;
    }
    /* A new palette changes every P_8 conversion; its checksum is part of
     * a P_8 variant, so the next draw of each converts again, and a held
     * batch keeps the conversion it was made with. NCC tables are for the
     * YIQ formats, which are not converted. */
    if (type == V9X_GLIDE_TEXTABLE_PALETTE) {
        unsigned int entry;

        for (entry = 0u; entry < V9X_GLIDE_PALETTE_ENTRIES; ++entry) {
            v9x_glide_palette[entry] = data[entry];
        }
        v9x_glide_palette_sum = v9x_glide_sum_bytes((const v9x_u8 *)data,
                                                    V9X_GLIDE_PALETTE_ENTRIES * 4u);
    }
    sum = v9x_glide_checksum(data, words);
    if (!v9x_glide_noted(V9X_GLIDE_IX_grTexDownloadTable,
                         v9x_glide_key(tmu, type, sum, 0ul))) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grTexDownloadTable, call,
                   "tmu=%lu type=%lu sum=%08lX first=%08lX %08lX %08lX %08lX",
                   tmu, type, sum, data[0], data[1], data[2], data[3]);
}

/* ---- linear frame buffer ------------------------------------------- */

static v9x_u8 *v9x_glide_lfb_buffer(void)
{
    if (v9x_glide_lfb == 0) {
        v9x_glide_lfb = (v9x_u8 *)VirtualAlloc(0, V9X_GLIDE_LFB_BYTES,
                                               MEM_COMMIT, PAGE_READWRITE);
    }
    return v9x_glide_lfb;
}

/* grLfbLock: type, buffer, writeMode, origin, pixelPipeline, info. */
v9x_u32 __stdcall grLfbLock(v9x_u32 type, v9x_u32 buffer, v9x_u32 write_mode,
                            v9x_u32 origin, v9x_u32 pipeline, v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbLock);
    v9x_u8 *shadow = v9x_glide_lfb_buffer();
    v9x_u32 mode = write_mode == V9X_GLIDE_LFB_WRITE_ANY ?
                   V9X_GLIDE_LFB_WRITE_565 : write_mode;
    v9x_u32 stride = (mode == V9X_GLIDE_LFB_WRITE_888 ||
                      mode == V9X_GLIDE_LFB_WRITE_8888) ?
                     V9X_GLIDE_LFB_STRIDE_32 : V9X_GLIDE_LFB_STRIDE_16;
    v9x_u32 size = info != 0 ? info[0] : 0ul;

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grLfbLock, call,
                       "type=%02lX buffer=%lu wmode=%02lX origin=%lu pipeline=%lu info.size=%lu -> %08lX stride=%lu",
                       type, buffer, write_mode, origin, pipeline, size,
                       (v9x_u32)shadow, stride);
    }
    if (info == 0 || size < V9X_GLIDE_LFB_INFO_BYTES) {
        return V9X_GLIDE_FALSE;
    }

    /*
     * On the device, the buffer itself, when its 16-bit 565 layout is what
     * the lock asks for: a read, or a write in 565 or "any". Other write
     * modes (888, 1555, the depth forms) keep the shadow until a title
     * needs them; NFS II SE wrote 565 only (census). The pixel pipeline
     * flag is not honoured: writes go straight to memory.
     */
    if (v9x_glide_device_is_open() && v9x_glide_lfb_locked == 0ul &&
        ((type & V9X_GLIDE_LFB_WRITE_ONLY) == 0ul ||
         mode == V9X_GLIDE_LFB_WRITE_565)) {
        void *pixels;
        v9x_u32 pitch;

        /* Held triangles land first: the lock is to see or to follow them. */
        v9x_glide_flush();
        if (v9x_glide_device_lock(buffer, (type & V9X_GLIDE_LFB_WRITE_ONLY) == 0ul,
                                  &pixels, &pitch)) {
            v9x_glide_lfb_locked = buffer + 1ul;
            info[1] = (v9x_u32)pixels;
            info[2] = pitch;
            info[3] = V9X_GLIDE_LFB_WRITE_565;
            info[4] = origin;
            return V9X_GLIDE_TRUE;
        }
    }
    if (shadow == 0) {
        return V9X_GLIDE_FALSE;
    }
    info[1] = (v9x_u32)shadow;
    info[2] = stride;
    info[3] = mode;
    info[4] = origin;
    return V9X_GLIDE_TRUE;
}

v9x_u32 __stdcall grLfbUnlock(v9x_u32 type, v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbUnlock);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grLfbUnlock, call, "type=%02lX buffer=%lu",
                       type, buffer);
    }
    if (v9x_glide_lfb_locked != 0ul) {
        v9x_glide_device_unlock(v9x_glide_lfb_locked - 1ul);
        v9x_glide_lfb_locked = 0ul;
    }
    return V9X_GLIDE_TRUE;
}

/* grLfbReadRegion reads 16-bit pixels; off the device it returns black. */
v9x_u32 __stdcall grLfbReadRegion(v9x_u32 buffer, v9x_u32 x, v9x_u32 y,
                                  v9x_u32 width, v9x_u32 height,
                                  v9x_u32 dst_stride, v9x_u8 *dst)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbReadRegion);
    v9x_u32 row;
    v9x_u32 column;

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grLfbReadRegion, call,
                       "buffer=%lu at=%lu,%lu size=%lux%lu stride=%lu dst=%08lX",
                       buffer, x, y, width, height, dst_stride, (v9x_u32)dst);
    }
    if (dst == 0 || width > V9X_GLIDE_LFB_STRIDE_16 / 2ul ||
        height > V9X_GLIDE_LFB_LINES || dst_stride < width * 2ul) {
        return V9X_GLIDE_FALSE;
    }
    if (v9x_glide_device_is_open()) {
        const v9x_u8 *pixels;
        void *base;
        v9x_u32 pitch;

        v9x_glide_flush();
        if (x + width > v9x_glide_state.width ||
            y + height > v9x_glide_state.height ||
            !v9x_glide_device_lock(buffer, 1, &base, &pitch)) {
            return V9X_GLIDE_FALSE;
        }
        pixels = (const v9x_u8 *)base;
        for (row = 0ul; row < height; ++row) {
            for (column = 0ul; column < width * 2ul; ++column) {
                dst[row * dst_stride + column] =
                    pixels[(y + row) * pitch + x * 2ul + column];
            }
        }
        v9x_glide_device_unlock(buffer);
        return V9X_GLIDE_TRUE;
    }
    for (row = 0ul; row < height; ++row) {
        for (column = 0ul; column < width * 2ul; ++column) {
            dst[row * dst_stride + column] = 0u;
        }
    }
    return V9X_GLIDE_TRUE;
}

/* grLfbWriteRegion: dst buffer, x, y, source format, width, height,
 * source stride, data. */
v9x_u32 __stdcall grLfbWriteRegion(v9x_u32 buffer, v9x_u32 x, v9x_u32 y,
                                   v9x_u32 src_format, v9x_u32 width,
                                   v9x_u32 height, v9x_u32 src_stride,
                                   const void *data)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbWriteRegion);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grLfbWriteRegion, call,
                       "buffer=%lu at=%lu,%lu format=%lu size=%lux%lu stride=%ld data=%08lX",
                       buffer, x, y, src_format, width, height,
                       (long)src_stride, (v9x_u32)data);
    }
    return V9X_GLIDE_TRUE;
}

/* ---- drawing ------------------------------------------------------- */

void __stdcall grDrawTriangle(const v9x_u32 *a, const v9x_u32 *b,
                              const v9x_u32 *c)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grDrawTriangle);

    if (v9x_glide_device_is_open()) {
        v9x_u32 start = v9x_glide_rdtsc_low();

        v9x_glide_draw_triangle((const float *)a, (const float *)b,
                                (const float *)c);
        v9x_glide_prof_add(V9X_GLIDE_PROF_ENTRY, v9x_glide_rdtsc_low() - start);
    }
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawTriangle, call, "swap=%lu", v9x_glide_swaps);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'a', a);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'b', b);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'c', c);
}

void __stdcall grDrawLine(const v9x_u32 *a, const v9x_u32 *b)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grDrawLine);

    if (v9x_glide_device_is_open()) {
        v9x_u32 start = v9x_glide_rdtsc_low();

        v9x_glide_draw_line((const float *)a, (const float *)b);
        v9x_glide_prof_add(V9X_GLIDE_PROF_ENTRY, v9x_glide_rdtsc_low() - start);
    }
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawLine, call, "swap=%lu", v9x_glide_swaps);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawLine, call, 'a', a);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawLine, call, 'b', b);
}

void __stdcall grDrawPoint(const v9x_u32 *a)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grDrawPoint);

    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawPoint, call, "swap=%lu", v9x_glide_swaps);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawPoint, call, 'a', a);
}

/* ---- the DLL ------------------------------------------------------- */

BOOL __stdcall V9xGlideEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* The program and the build every later line is read against. */
        char path[MAX_PATH];
        char text[MAX_PATH + 96];

        CreateDirectoryA(V9X_DIAG_DIR, 0);
        v9x_glide_summary_tick = GetTickCount();
        /* The 640x480 a Voodoo opens most often, until grSstWinOpen says. */
        v9x_glide_state_init(&v9x_glide_state, 640ul, 480ul,
                             V9X_GLIDE_ORIGIN_UPPER_LEFT);
        v9x_glide_texmem_init(&v9x_glide_texmem);
        if (GetModuleFileNameA(0, path, sizeof(path)) == 0ul) {
            path[0] = '\0';
        }
        path[sizeof(path) - 1u] = '\0';
        wsprintfA(text, "attach instance=%08lX version=%s %s exe=%s",
                  (DWORD)instance, V9X_VERSION_STRING, v9x_glide_build_id, path);
        v9x_glide_log(text);
    } else if (reason == DLL_PROCESS_DETACH) {
        v9x_glide_summary("detach");
        if (v9x_glide_lfb != 0) {
            VirtualFree(v9x_glide_lfb, 0, MEM_RELEASE);
            v9x_glide_lfb = 0;
        }
    }
    return TRUE;
}
