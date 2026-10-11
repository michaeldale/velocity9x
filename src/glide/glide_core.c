/*
 * The Glide engine both Glide DLLs link (glide_core.h): moved out of
 * glide_dll.c unchanged when Glide 3 came (docs\plans\glide-3x-wrapper.md),
 * so GLIDE3X.DLL draws through the code NFS II SE measured. One of the
 * platform files (scripts\check-tree.ps1 lists it): the census log, the
 * texture engine and its surface cache, batching, drawing, clears, the
 * window and the frame buffer. glide_surface.c owns the DirectDraw device.
 *
 * The census counts by the calling DLL's export index; the names and the
 * log file are the front end's, registered by v9x_glide_core_attach.
 *
 * Floats are logged as their IEEE bit patterns: converting one to an
 * integer lowers to Watcom's __CHP helper, which a DLL linked without the C
 * runtime cannot resolve (gl_state.c), and the bits lose nothing.
 *
 * Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include "velocity9x/diagpaths.h"
#include "glide_api.h"
#include "glide_vertex.h"
#include "glide_state.h"
#include "glide_texmem.h"
#include "glide_texfmt.h"
#include "glide_surface.h"
#include "glide_core.h"

/*
 * grLfbLock. GrLfbInfo_t is size, lfbPtr, strideInBytes, writeMode,
 * origin (20 bytes); the caller fills size. Voodoo Graphics' frame buffer
 * is 1024 pixels a line, so its stride is 2048 bytes in the 16-bit write
 * modes and 4096 in GR_LFBWRITEMODE_888 (4) and _8888 (5). Off the device
 * the lock hands out a shadow buffer of that geometry, so a game that
 * assumes the stride writes inside it. GR_LFBWRITEMODE_ANY (0xFF) reports
 * 565 (0).
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

/* FXTRUE / FXFALSE (Reference Manual, "Data types"). */
#define V9X_GLIDE_TRUE  1ul
#define V9X_GLIDE_FALSE 0ul

/* The front end's log file and export names (v9x_glide_core_attach). The
 * Glide 2 log until then, so a line before attach still lands. */
static const char *v9x_glide_log_path = V9X_DIAG_GLIDE_LOG;
static const char *const *v9x_glide_names;
static unsigned int v9x_glide_name_count;

static v9x_u32 v9x_glide_sequence;
static v9x_u32 v9x_glide_calls[V9X_GLIDE_CORE_EXPORTS_MAX];
static v9x_u32 v9x_glide_lines[V9X_GLIDE_CORE_EXPORTS_MAX];
static v9x_u32 v9x_glide_seen[V9X_GLIDE_SEEN_SLOTS];
static v9x_u32 v9x_glide_seen_full;
/* The frame before the first swap is captured: it holds the setup. */
static v9x_u32 v9x_glide_capturing = 1ul;
static v9x_u32 v9x_glide_capture_lines;
static v9x_u32 v9x_glide_summary_tick;
static v9x_u32 v9x_glide_swaps;
static v9x_u8 *v9x_glide_lfb;

/* The Glide state as the game set it, and what grSstWinOpen chose. */
V9X_GLIDE_STATE v9x_glide_state;
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

    file = CreateFileA(v9x_glide_log_path, GENERIC_WRITE, FILE_SHARE_READ, 0,
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

void v9x_glide_logf(unsigned int ix, v9x_u32 call, const char *format, ...)
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

v9x_u32 v9x_glide_count(unsigned int ix)
{
    return ++v9x_glide_calls[ix];
}

int v9x_glide_sampled(v9x_u32 call)
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
int v9x_glide_noted(unsigned int ix, v9x_u32 key)
{
    if (v9x_glide_capturing) {
        return 1;
    }
    return v9x_glide_first_seen(ix, key);
}

v9x_u32 v9x_glide_key(v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 d)
{
    return a ^ v9x_glide_rotl(b, 7u) ^ v9x_glide_rotl(c, 14u) ^
           v9x_glide_rotl(d, 21u);
}

v9x_u32 v9x_glide_checksum(const v9x_u32 *words, unsigned int count)
{
    v9x_u32 sum = 0ul;
    unsigned int index;

    for (index = 0u; index < count; ++index) {
        sum = v9x_glide_rotl(sum, 5u) ^ words[index];
    }
    return sum;
}

/* Every export called so far: its calls and how many lines it logged. */
void v9x_glide_summary(const char *why)
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
    for (ix = 0u; ix < v9x_glide_name_count; ++ix) {
        if (v9x_glide_calls[ix] == 0ul) {
            continue;
        }
        wsprintfA(text, "census   %s calls=%lu lines=%lu", v9x_glide_names[ix],
                  v9x_glide_calls[ix], v9x_glide_lines[ix]);
        v9x_glide_log(text);
    }
}

void v9x_glide_stub_called(unsigned int ix)
{
    if (v9x_glide_count(ix) == 1ul) {
        char text[96];

        wsprintfA(text, "stub first-call %s", v9x_glide_names[ix]);
        v9x_glide_log(text);
    }
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

/*
 * The three tables, about 430 KB, are committed at attach rather than held
 * as statics: wlink puts _BSS in DGROUP and writes it into the image, which
 * made GLIDE2X.DLL 468 KB and overflowed a family's floppy. The names stay
 * those of the arrays they replace.
 */
typedef struct v9x_glide_tables {
    V9X_GLIDE_TEXMEM texmem;
    V9X_GLIDE_TEXTURE textures[V9X_GLIDE_TEXMEM_RECORDS];
    V9X_GLIDE_CACHED_SURFACE surfaces[V9X_GLIDE_SURFACES];
} V9X_GLIDE_TABLES;

static V9X_GLIDE_TABLES *v9x_glide_tables;
#define v9x_glide_texmem   (v9x_glide_tables->texmem)
#define v9x_glide_textures (v9x_glide_tables->textures)
#define v9x_glide_surfaces (v9x_glide_tables->surfaces)
static v9x_u32 v9x_glide_surface_stamp;
static v9x_u32 v9x_glide_use_clock;
static int v9x_glide_source = -1;
static v9x_u32 v9x_glide_source_misses_logged;
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

/* A float's bits, for the log: wsprintf has no float conversion. */
static v9x_u32 v9x_glide_float_bits(float value)
{
    return *(const v9x_u32 *)&value;
}

/* A refused batch, logged in full the first few times: the state is what
 * says which combination the engine does not take, and the first triangle's
 * corners say whether it was the geometry. Gen3 refuses a vertex off the
 * target, a z outside [0, 1] or an rhw that is not positive and finite
 * (i9xx_vertex.c); Carmageddon II's Glide draws were refused that way on the
 * netbook (2026-10-11) and nothing here said which. Bits, not values. */
static void v9x_glide_log_refusal(v9x_u32 result, const V9X_R3D_ABI_DRAW *draw)
{
    const V9X_R3D_ABI_STATE *s = &draw->state;
    const V9X_R3D_ABI_TEXTURE *t = &draw->texture;
    const V9X_R3D_ABI_VERTEX *v = (const V9X_R3D_ABI_VERTEX *)draw->vertices;
    v9x_u32 corner;

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
    if (v == 0 || draw->triangle_count == 0ul) {
        return;
    }
    for (corner = 0ul; corner < 3ul; ++corner) {
        v9x_glide_log3("  corner %lu x/y bits=%08lX/%08lX", corner,
                       v9x_glide_float_bits(v[corner].sx),
                       v9x_glide_float_bits(v[corner].sy));
        v9x_glide_log3("  corner %lu z/rhw bits=%08lX/%08lX", corner,
                       v9x_glide_float_bits(v[corner].sz),
                       v9x_glide_float_bits(v[corner].rhw));
    }
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

void v9x_glide_flush(void)
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

    if (v9x_glide_source < 0) {
        return v9x_glide_skip(1ul, v9x_glide_source_address);
    }
    /* Separate from 1: the two shared a reason, and Carmageddon II's menu
     * skips (netbook, 2026-10-11) could not say which they were. */
    if (v9x_glide_device_description() == 0) {
        return v9x_glide_skip(5ul, v9x_glide_source_address);
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
    /* The key's alpha stands in for the fragment's only where the
     * conversion's alpha is the key alone (glide_state.c); a format with
     * its own alpha keeps the fragment's, as before the key promotion.
     * NFS II SE's map pane needs the latter (netbook, 2026-10-10). */
    if (setup->key_alpha && !v9x_glide_texfmt_key_alpha_only(texture->format)) {
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    }

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

/*
 * A download's bytes into the copies of textures it lies partly over, which
 * texture memory keeps (glide_texmem.c): the overlap, within the largest
 * level of both, since that is all either copy holds. The copy's
 * conversion and hardware surface are dropped, so the next draw converts
 * and uploads the patched texels.
 */
static void v9x_glide_patch_overlaps(int index, v9x_u32 start, v9x_u32 end,
                                     const v9x_u8 *data, v9x_u32 data_bytes)
{
    unsigned int i;

    for (i = 0u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        const V9X_GLIDE_TEXREC *r = &v9x_glide_texmem.records[i];
        V9X_GLIDE_TEXTURE *texture = &v9x_glide_textures[i];
        v9x_u32 from;
        v9x_u32 to;
        v9x_u32 raw_bytes;
        v9x_u32 k;

        if ((int)i == index || !r->in_use || texture->raw == 0 ||
            texture->serial != r->serial ||
            !(start < r->end && r->start < end)) {
            continue;
        }
        from = start > r->start ? start : r->start;
        to = end < r->end ? end : r->end;
        raw_bytes = texture->width * texture->height *
                    (texture->format >= V9X_GLIDE_TEXFMT_16BIT ? 2ul : 1ul);
        for (k = from; k < to; ++k) {
            if (k - r->start >= raw_bytes || k - start >= data_bytes) {
                break;
            }
            texture->raw[k - r->start] = data[k - start];
        }
        texture->sum = v9x_glide_sum_bytes(texture->raw, raw_bytes);
        texture->variant = 0ul;
        texture->surface_slot = 0ul;
    }
}

/* grTexDownloadMipMap's texels into the record's entry: the largest
 * level, which is the first in the game's data when the even/odd mask
 * includes it. NFS II SE downloads single levels with both (census). */
void v9x_glide_texture_download(v9x_u32 address, v9x_u32 even_odd,
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
    v9x_glide_patch_overlaps(index, address,
                             address + v9x_glide_texmem_required(&texinfo,
                                                                 even_odd),
                             data, bytes);
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
    vs->textured = draw->textured ? 1ul : 0ul;
    vs->tmu0_w = (v9x_glide_state.stw_hint &
                  V9X_GLIDE_STWHINT_W_DIFF_TMU0) != 0ul ? 1ul : 0ul;
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

/* grDrawTriangle's and grDrawLine's work, timed as the entry bucket. */
void v9x_glide_triangle(const float *a, const float *b, const float *c)
{
    v9x_u32 start;

    if (!v9x_glide_device_is_open()) {
        return;
    }
    start = v9x_glide_rdtsc_low();
    v9x_glide_draw_triangle(a, b, c);
    v9x_glide_prof_add(V9X_GLIDE_PROF_ENTRY, v9x_glide_rdtsc_low() - start);
}

void v9x_glide_line(const float *a, const float *b)
{
    v9x_u32 start;

    if (!v9x_glide_device_is_open()) {
        return;
    }
    start = v9x_glide_rdtsc_low();
    v9x_glide_draw_line(a, b);
    v9x_glide_prof_add(V9X_GLIDE_PROF_ENTRY, v9x_glide_rdtsc_low() - start);
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
void v9x_glide_clear(v9x_u32 color, v9x_u32 depth)
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
void v9x_glide_close(void)
{
    v9x_glide_flush();
    v9x_glide_textures_drop_surfaces();
    v9x_glide_device_close();
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
v9x_u32 v9x_glide_lfb_lock(unsigned int ix, v9x_u32 call, v9x_u32 type,
                           v9x_u32 buffer, v9x_u32 write_mode, v9x_u32 origin,
                           v9x_u32 pipeline, v9x_u32 *info)
{
    v9x_u8 *shadow = v9x_glide_lfb_buffer();
    v9x_u32 mode = write_mode == V9X_GLIDE_LFB_WRITE_ANY ?
                   V9X_GLIDE_LFB_WRITE_565 : write_mode;
    v9x_u32 stride = (mode == V9X_GLIDE_LFB_WRITE_888 ||
                      mode == V9X_GLIDE_LFB_WRITE_8888) ?
                     V9X_GLIDE_LFB_STRIDE_32 : V9X_GLIDE_LFB_STRIDE_16;
    v9x_u32 size = info != 0 ? info[0] : 0ul;

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(ix, call,
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

v9x_u32 v9x_glide_lfb_unlock(unsigned int ix, v9x_u32 call, v9x_u32 type,
                             v9x_u32 buffer)
{
    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(ix, call, "type=%02lX buffer=%lu",
                       type, buffer);
    }
    if (v9x_glide_lfb_locked != 0ul) {
        v9x_glide_device_unlock(v9x_glide_lfb_locked - 1ul);
        v9x_glide_lfb_locked = 0ul;
    }
    return V9X_GLIDE_TRUE;
}

/* grLfbReadRegion reads 16-bit pixels; off the device it returns black. */
v9x_u32 v9x_glide_lfb_read(unsigned int ix, v9x_u32 call, v9x_u32 buffer,
                           v9x_u32 x, v9x_u32 y, v9x_u32 width,
                           v9x_u32 height, v9x_u32 dst_stride, v9x_u8 *dst)
{
    v9x_u32 row;
    v9x_u32 column;

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(ix, call,
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

/* ---- the window, the frame and texture state ---------------------- */

int v9x_glide_open(void *window, v9x_u32 resolution, v9x_u32 color_format,
                   v9x_u32 origin, v9x_u32 color_buffers, v9x_u32 aux_buffers)
{
    v9x_u32 width = 0ul;
    v9x_u32 height = 0ul;

    if (!v9x_glide_resolution_size(resolution, &width, &height)) {
        return 0;
    }
    v9x_glide_color_format = color_format;
    v9x_glide_render_buffer = V9X_GLIDE_BUFFER_BACK;
    v9x_glide_state_init(&v9x_glide_state, width, height, origin);
    v9x_glide_close();
    return v9x_glide_device_open(window, width, height, color_buffers,
                                 aux_buffers);
}

void v9x_glide_swap(v9x_u32 interval)
{
    v9x_u32 now;

    ++v9x_glide_swaps;
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

v9x_u32 v9x_glide_swap_count(void)
{
    return v9x_glide_swaps;
}

void v9x_glide_set_render_buffer(v9x_u32 buffer)
{
    if (buffer == V9X_GLIDE_BUFFER_FRONT || buffer == V9X_GLIDE_BUFFER_BACK) {
        v9x_glide_flush();
        v9x_glide_render_buffer = buffer;
    }
}

/* grConstantColorValue4's colour, ARGB, kept in the game's colour format as
 * grConstantColorValue's is, so every reader converts it one way. */
void v9x_glide_set_constant_argb(v9x_u32 argb)
{
    v9x_glide_state.constant_color =
        v9x_glide_argb_to_color(argb, v9x_glide_color_format);
}

/* The record the draws sample until the next source; one TMU. */
void v9x_glide_texture_source(v9x_u32 address, v9x_u32 even_odd,
                              const v9x_u32 *info)
{
    V9X_GLIDE_TEXINFO texinfo;

    v9x_glide_source = -1;
    v9x_glide_source_address = address;
    if (info == 0) {
        return;
    }
    texinfo.small_lod = info[0];
    texinfo.large_lod = info[1];
    texinfo.aspect = info[2];
    texinfo.format = info[3];
    v9x_glide_source = v9x_glide_texmem_find(&v9x_glide_texmem, address,
                                             even_odd, &texinfo);
    v9x_glide_source_aspect = texinfo.aspect;
    /* A source with no download behind it, the first few times: what was
     * asked, and what the memory holds at that address. */
    if (v9x_glide_source < 0 && v9x_glide_source_misses_logged < 16ul) {
        unsigned int i;
        v9x_u32 held = 0ul;
        v9x_u32 held_info = 0ul;

        ++v9x_glide_source_misses_logged;
        for (i = 0u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
            const V9X_GLIDE_TEXREC *r = &v9x_glide_texmem.records[i];

            if (r->in_use && r->start == address) {
                ++held;
                held_info = (r->even_odd << 24) | (r->info.small_lod << 16) |
                            (r->info.large_lod << 12) | (r->info.aspect << 8) |
                            r->info.format;
            }
        }
        v9x_glide_log3("source miss: addr=%08lX asked=%08lX held=%lu",
                       address,
                       (even_odd << 24) | (texinfo.small_lod << 16) |
                           (texinfo.large_lod << 12) | (texinfo.aspect << 8) |
                           texinfo.format,
                       held);
        v9x_glide_log3("  held record=%08lX (evenodd<<24 small<<16 large<<12 "
                       "aspect<<8 format) swap=%lu", held_info, v9x_glide_swaps,
                       0ul);
    }
}

/* A new palette changes every P_8 conversion; its checksum is part of a
 * P_8 variant, so the next draw of each converts again, and a held batch
 * keeps the conversion it was made with. */
void v9x_glide_palette_load(const v9x_u32 *palette)
{
    unsigned int entry;

    for (entry = 0u; entry < V9X_GLIDE_PALETTE_ENTRIES; ++entry) {
        v9x_glide_palette[entry] = palette[entry];
    }
    v9x_glide_palette_sum = v9x_glide_sum_bytes((const v9x_u8 *)palette,
                                                V9X_GLIDE_PALETTE_ENTRIES * 4u);
}

void v9x_glide_fog_load(const v9x_u8 *table)
{
    unsigned int index;

    for (index = 0u; index < V9X_GLIDE_FOG_TABLE_SIZE; ++index) {
        v9x_glide_fog[index] = table[index];
    }
}

/* ---- attach and detach ---------------------------------------------- */

int v9x_glide_core_attach(const char *log_path, const char *const *names,
                          unsigned int count)
{
    v9x_glide_log_path = log_path;
    v9x_glide_names = names;
    v9x_glide_name_count = count < V9X_GLIDE_CORE_EXPORTS_MAX ?
                           count : V9X_GLIDE_CORE_EXPORTS_MAX;
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    /* Committed pages are zero, as the statics were. */
    v9x_glide_tables = (V9X_GLIDE_TABLES *)VirtualAlloc(
        0, sizeof(V9X_GLIDE_TABLES), MEM_COMMIT, PAGE_READWRITE);
    if (v9x_glide_tables == 0) {
        v9x_glide_log("attach refused: no memory for the texture tables");
        return 0;
    }
    v9x_glide_summary_tick = GetTickCount();
    /* The 640x480 a Voodoo opens most often, until grSstWinOpen says. */
    v9x_glide_state_init(&v9x_glide_state, 640ul, 480ul,
                         V9X_GLIDE_ORIGIN_UPPER_LEFT);
    v9x_glide_texmem_init(&v9x_glide_texmem);
    return 1;
}

void v9x_glide_core_detach(void)
{
    v9x_glide_summary("detach");
    if (v9x_glide_lfb != 0) {
        VirtualFree(v9x_glide_lfb, 0, MEM_RELEASE);
        v9x_glide_lfb = 0;
    }
    if (v9x_glide_tables != 0) {
        VirtualFree(v9x_glide_tables, 0, MEM_RELEASE);
        v9x_glide_tables = 0;
    }
}
