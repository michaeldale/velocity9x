/*
 * GLIDE2X.DLL, Glide 2.x over the render interface
 * (docs\plans\glide-2x-wrapper.md). This is the Phase 0 census build: it
 * draws nothing. It answers which Glide calls Need for Speed II SE makes,
 * with which arguments and in what order, so the later phases implement
 * that list and no other.
 *
 * All 130 retail exports exist (src\glide\glide_entrypoints.psd1). The 50
 * that NFS2SEA.EXE imports are written here: each logs its arguments and
 * returns what a working Voodoo Graphics board would, closely enough that
 * the game carries on. The other 80 are generated stubs that log their
 * first call and return zero.
 *
 * The log is C:\V9XDIAG\V9XGLIDE.LOG, appended a line at a time with the
 * file opened and closed each time, so a game that dies mid-run leaves
 * everything it logged (the ICD's rule, gl_icd.c). Volume is bounded per
 * export: a state call logs when its arguments change, up to a cap; a draw
 * or poll call logs its first calls and then one in 4096. Every export is
 * counted, and the counts are written every 600 swaps and at shutdown.
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

/*
 * Texture memory arithmetic for grTexCalcMemRequired (Reference Manual,
 * grTexCalcMemRequired and "Texture formats"). LODs run GR_LOD_256 (0) to
 * GR_LOD_1 (8), the largest edge 256 >> lod; aspects run GR_ASPECT_8x1 (0)
 * to GR_ASPECT_1x8 (6) with GR_ASPECT_1x1 at 3; formats from
 * GR_TEXFMT_ARGB_8332 (8) up are 16 bits a texel, those below 8 bits. Each
 * level is rounded up to 8 bytes, the TMU's address granule - an
 * assumption the census tests against the addresses the game picks.
 */
#define V9X_GLIDE_LOD_256        0ul
#define V9X_GLIDE_LOD_1          8ul
#define V9X_GLIDE_ASPECT_1X1     3ul
#define V9X_GLIDE_ASPECT_1X8     6ul
#define V9X_GLIDE_TEXFMT_16BIT   8ul
#define V9X_GLIDE_TEX_GRANULE    8ul

/* grTexDownloadTable types (GrTexTable_t): GR_TEXTABLE_NCC0, NCC1 and
 * PALETTE. A palette is 256 FxU32; an NCC table (GuNccTable) 112 bytes. */
#define V9X_GLIDE_TEXTABLE_PALETTE  2ul
#define V9X_GLIDE_PALETTE_DWORDS    256u
#define V9X_GLIDE_NCC_TABLE_DWORDS  28u

/* GR_FOG_TABLE_SIZE: 64 FxU8 entries. */
#define V9X_GLIDE_FOG_TABLE_BYTES 64u

/*
 * grLfbLock. GrLfbInfo_t is size, lfbPtr, strideInBytes, writeMode,
 * origin (20 bytes); the caller fills size. Voodoo Graphics' frame buffer
 * is 1024 pixels a line, so its stride is 2048 bytes in the 16-bit write
 * modes and 4096 in GR_LFBWRITEMODE_888 (4) and _8888 (5). The census
 * build hands out a shadow buffer of that geometry, so a game that assumes
 * the stride writes inside it. GR_LFBWRITEMODE_ANY (0xFF) reports 565 (0).
 */
#define V9X_GLIDE_LFB_INFO_BYTES   20ul
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

/* ---- the log ------------------------------------------------------- */

/* One line, appended: sequence, process, milliseconds, text. */
static void v9x_glide_log(const char *text)
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

    v9x_glide_logf(V9X_GLIDE_IX_grSstWinOpen, call,
                   "hwnd=%08lX res=%lu refresh=%lu cformat=%lu origin=%lu colbuf=%lu auxbuf=%lu -> 1",
                   window, resolution, refresh, color_format, origin,
                   color_buffers, aux_buffers);
    return V9X_GLIDE_TRUE;
}

void __stdcall grSstWinClose(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grSstWinClose);

    v9x_glide_logf(V9X_GLIDE_IX_grSstWinClose, call, "");
    v9x_glide_summary("winclose");
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
    if (interval != 0ul) {
        Sleep(interval * V9X_GLIDE_FRAME_MS);
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
}

/* ---- render state -------------------------------------------------- */

/*
 * The one-argument state calls share a shape: log the value under the
 * rules above. A macro rather than a helper so each export keeps its own
 * index and its own name in the log.
 */
#define V9X_GLIDE_STATE1(name, label)                                        \
    void __stdcall name(v9x_u32 value)                                       \
    {                                                                        \
        v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_##name);                 \
                                                                             \
        if (v9x_glide_noted(V9X_GLIDE_IX_##name, value)) {                   \
            v9x_glide_logf(V9X_GLIDE_IX_##name, call, label "=%08lX", value); \
        }                                                                    \
    }

V9X_GLIDE_STATE1(grCullMode, "mode")
V9X_GLIDE_STATE1(grDepthBiasLevel, "bias")
V9X_GLIDE_STATE1(grDepthBufferFunction, "func")
V9X_GLIDE_STATE1(grDepthBufferMode, "mode")
V9X_GLIDE_STATE1(grDepthMask, "enable")
V9X_GLIDE_STATE1(grDitherMode, "mode")
V9X_GLIDE_STATE1(grAlphaTestFunction, "func")
V9X_GLIDE_STATE1(grAlphaTestReferenceValue, "ref")
V9X_GLIDE_STATE1(grChromakeyMode, "mode")
V9X_GLIDE_STATE1(grChromakeyValue, "color")
V9X_GLIDE_STATE1(grFogColorValue, "color")
V9X_GLIDE_STATE1(grFogMode, "mode")
V9X_GLIDE_STATE1(grGammaCorrectionValue, "gamma-bits")

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
}

void __stdcall grFogTable(const v9x_u32 *table)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grFogTable);
    v9x_u32 key;

    if (table == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grFogTable, call, "table=null");
        return;
    }
    key = v9x_glide_checksum(table, V9X_GLIDE_FOG_TABLE_BYTES / 4u);
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
        for (index = 0u; index < V9X_GLIDE_FOG_TABLE_BYTES; ++index) {
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
}

void __stdcall grTexCombineFunction(v9x_u32 tmu, v9x_u32 function)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexCombineFunction);

    if (v9x_glide_noted(V9X_GLIDE_IX_grTexCombineFunction,
                        v9x_glide_key(tmu, function, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexCombineFunction, call,
                       "tmu=%lu func=%lu", tmu, function);
    }
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

/* The bytes one LOD of a texture occupies, rounded to the granule. */
static v9x_u32 v9x_glide_level_bytes(v9x_u32 lod, v9x_u32 aspect,
                                     v9x_u32 format)
{
    v9x_u32 edge = 256ul >> lod;
    v9x_u32 width = edge;
    v9x_u32 height = edge;
    v9x_u32 bytes;

    if (aspect < V9X_GLIDE_ASPECT_1X1) {
        height = edge >> (V9X_GLIDE_ASPECT_1X1 - aspect);
    } else if (aspect > V9X_GLIDE_ASPECT_1X1) {
        width = edge >> (aspect - V9X_GLIDE_ASPECT_1X1);
    }
    if (width == 0ul) {
        width = 1ul;
    }
    if (height == 0ul) {
        height = 1ul;
    }
    bytes = width * height;
    if (format >= V9X_GLIDE_TEXFMT_16BIT) {
        bytes *= 2ul;
    }
    return (bytes + V9X_GLIDE_TEX_GRANULE - 1ul) & ~(V9X_GLIDE_TEX_GRANULE - 1ul);
}

/* lodmin is the smallest level (the larger GrLOD_t number), lodmax the
 * largest; the order is taken from the values, not the argument names. */
v9x_u32 __stdcall grTexCalcMemRequired(v9x_u32 lodmin, v9x_u32 lodmax,
                                       v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grTexCalcMemRequired);
    v9x_u32 first = lodmin < lodmax ? lodmin : lodmax;
    v9x_u32 last = lodmin < lodmax ? lodmax : lodmin;
    v9x_u32 total = 0ul;
    v9x_u32 lod;

    if (last <= V9X_GLIDE_LOD_1 && aspect <= V9X_GLIDE_ASPECT_1X8) {
        for (lod = first; lod <= last; ++lod) {
            total += v9x_glide_level_bytes(lod, aspect, format);
        }
    }
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
                         V9X_GLIDE_PALETTE_DWORDS : V9X_GLIDE_NCC_TABLE_DWORDS;
    v9x_u32 sum;

    if (data == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grTexDownloadTable, call,
                       "tmu=%lu type=%lu data=null", tmu, type);
        return;
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
    if (shadow == 0 || info == 0 || size < V9X_GLIDE_LFB_INFO_BYTES) {
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
    return V9X_GLIDE_TRUE;
}

/* grLfbReadRegion reads 16-bit pixels; the census build returns black. */
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
