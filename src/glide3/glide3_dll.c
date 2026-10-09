/*
 * GLIDE3X.DLL, Glide 3.x over the render interface
 * (docs\plans\glide-3x-wrapper.md). The Glide 3 front end: it translates
 * Glide 3's calls into the engine GLIDE2X.DLL draws with
 * (src\glide\glide_core.c), so both DLLs share one texture engine, one
 * draw path and one census log format. What is Glide 3's own:
 *
 * - Vertices come through the layout grVertexLayout declares, read into
 *   Glide 2 GrVertex floats by glide3_layout.c, and primitives arrive as
 *   vertex arrays, polygons, strips and fans, expanded there too.
 * - GrTexInfo counts LOD and aspect in log2; glide3_layout.c turns them
 *   into Glide 2's numbers before the engine sees them.
 * - grGet, grGetString and grQueryResolutions describe the board.
 * - grSstWinOpen returns a context; one context is kept.
 *
 * All 99 exports of 3dfx's GLIDE3X.DLL exist
 * (src\glide3\glide3_entrypoints.psd1). The ones Diablo II and Rollcage
 * import, and a few state calls beside them, are written here; the rest
 * are generated stubs that log their first call and return zero.
 *
 * The log is C:\V9XDIAG\V9XGLD3.LOG, under the engine's rules (glide_core.c).
 * Glide API values are facts from 3dfx's Glide 3 glide.h and sst1vid.h;
 * nothing of either is copied (licence rule in the plan).
 *
 * Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "../glide/glide_api.h"
#include "../glide/glide_state.h"
#include "../glide/glide_texmem.h"
#include "../glide/glide_core.h"
#include "glide3_layout.h"

static void v9x_glide3_stub_called(unsigned int ix, const v9x_u32 *args,
                                   unsigned int count);
#define V9X_GLIDE3_STUB_HOOK(ix, args, count) v9x_glide3_stub_called(ix, args, count)
#define V9X_GLIDE3_DEFINE_STUBS
#include "glide3_exports_gen.h"

static const char v9x_glide3_build_id[] = "V9XGLD3 build=" V9X_BUILD_ID;
static const char *const v9x_glide3_names[V9X_GLIDE3_EXPORT_COUNT] =
    V9X_GLIDE3_EXPORT_NAMES;

/* The census in glide_core.c counts by export index. */
typedef char v9x_glide3_exports_fit_core[
    V9X_GLIDE3_EXPORT_COUNT <= V9X_GLIDE_CORE_EXPORTS_MAX ? 1 : -1];

/* FXTRUE / FXFALSE. */
#define V9X_GLIDE3_TRUE  1ul
#define V9X_GLIDE3_FALSE 0ul

/* ---- glide.h facts ------------------------------------------------- */

/* grGet pnames. */
#define V9X_GR_BITS_DEPTH                 0x01ul
#define V9X_GR_BITS_RGBA                  0x02ul
#define V9X_GR_FIFO_FULLNESS              0x03ul
#define V9X_GR_FOG_TABLE_ENTRIES          0x04ul
#define V9X_GR_GAMMA_TABLE_ENTRIES        0x05ul
#define V9X_GR_IS_BUSY                    0x08ul
#define V9X_GR_LFB_PIXEL_PIPE             0x09ul
#define V9X_GR_MAX_TEXTURE_SIZE           0x0aul
#define V9X_GR_MAX_TEXTURE_ASPECT_RATIO   0x0bul
#define V9X_GR_MEMORY_FB                  0x0cul
#define V9X_GR_MEMORY_TMU                 0x0dul
#define V9X_GR_MEMORY_UMA                 0x0eul
#define V9X_GR_NUM_BOARDS                 0x0ful
#define V9X_GR_NUM_FB                     0x11ul
#define V9X_GR_NUM_SWAP_HISTORY_BUFFER    0x12ul
#define V9X_GR_NUM_TMU                    0x13ul
#define V9X_GR_PENDING_BUFFERSWAPS        0x14ul
#define V9X_GR_REVISION_FB                0x15ul
#define V9X_GR_REVISION_TMU               0x16ul
#define V9X_GR_SUPPORTS_PASSTHRU          0x23ul
#define V9X_GR_TEXTURE_ALIGN              0x24ul
#define V9X_GR_VIDEO_POSITION             0x25ul
#define V9X_GR_WDEPTH_MIN_MAX             0x27ul
#define V9X_GR_ZDEPTH_MIN_MAX             0x28ul
#define V9X_GR_BITS_GAMMA                 0x2aul

/* grGetString names. */
#define V9X_GR_HARDWARE                   0xa1ul
#define V9X_GR_RENDERER                   0xa2ul
#define V9X_GR_VENDOR                     0xa3ul
#define V9X_GR_VERSION                    0xa4ul

/* grQueryResolutions: GR_QUERY_ANY and GR_REFRESH_60Hz. */
#define V9X_GR_QUERY_ANY                  0xfffffffful
#define V9X_GR_REFRESH_60Hz               0x00ul
#define V9X_GR_RESOLUTION_ENTRY_BYTES     16ul

/* grCoordinateSpace: GR_WINDOW_COORDS; GR_CLIP_COORDS is not drawn. */
#define V9X_GR_WINDOW_COORDS              0x00ul

/* grFogMode's source (low byte): GR_FOG_WITH_TABLE_ON_Q and
 * GR_FOG_WITH_ITERATED_ALPHA_EXT. The fog-coordinate and iterated-Z
 * sources have no engine counterpart. */
#define V9X_GR_FOG_SOURCE_MASK            0xfful
#define V9X_GR_FOG_WITH_TABLE_ON_Q        0x02ul
#define V9X_GR_FOG_WITH_ITERATED_ALPHA    0x04ul

/* grTexDownloadTable: GR_TEXTABLE_PALETTE. */
#define V9X_GR_TEXTABLE_PALETTE           0x02ul

/* A texture-unit function the state mapping does not know, as
 * glide_dll.c reports an inverted one. */
#define V9X_GLIDE3_COMBINE_FUNCTION_OTHER 0xFFul

/* ---- the board ------------------------------------------------------ */

/*
 * One Voodoo3-class board, one TMU, 4 MiB of frame buffer and 4 MiB of
 * texture memory, maximum texture 256 and aspect 8:1. Diablo II and
 * Rollcage accepted it in the census
 * (docs\decisions\2026-10-10-rollcage-glide3-census.md).
 */
#define V9X_GLIDE3_MEMORY_FB      (4ul * 1024ul * 1024ul)
#define V9X_GLIDE3_MEMORY_TMU     (4ul * 1024ul * 1024ul)
#define V9X_GLIDE3_MAX_TEXTURE    256ul
#define V9X_GLIDE3_MAX_ASPECT     3ul
#define V9X_GLIDE3_TEXTURE_ALIGN  8ul
#define V9X_GLIDE3_GAMMA_ENTRIES  256ul
#define V9X_GLIDE3_REVISION       1ul
/* The 64 KiB of frame buffer 3dfx's own mode check holds back for the
 * command FIFO. */
#define V9X_GLIDE3_FIFO_RESERVE   0x10000ul
#define V9X_GLIDE3_COLOR_BUFFERS_MAX 3ul
#define V9X_GLIDE3_AUX_BUFFERS_MAX   1ul

static const char v9x_glide3_version[] = "3.01 Velocity9x " V9X_BUILD_ID;
static const char v9x_glide3_hardware[] = "Voodoo3";
static const char v9x_glide3_renderer[] = "Glide";
static const char v9x_glide3_vendor[] = "3Dfx Interactive";
static const char v9x_glide3_extension[] = "";

/* The modes grQueryResolutions offers: GrScreenResolution_t, width,
 * height (sst1vid.h). The flip chain has opened each on the test cards. */
static const v9x_u32 v9x_glide3_modes[3][3] = {
    { 0x07ul, 640ul, 480ul },
    { 0x08ul, 800ul, 600ul },
    { 0x0cul, 1024ul, 768ul }
};

/* ---- front-end state ---------------------------------------------- */

static V9X_GLIDE3_LAYOUT v9x_glide3_layout;
static v9x_u32 v9x_glide3_coordinates = V9X_GR_WINDOW_COORDS;
static v9x_u32 v9x_glide3_clip_space_logged;
static v9x_u32 v9x_glide3_context_open;

static void v9x_glide3_stub_called(unsigned int ix, const v9x_u32 *args,
                                   unsigned int count)
{
    (void)args;
    (void)count;
    v9x_glide_stub_called(ix);
}

/* Up to the first four 32-bit arguments of a call as the log key. */
static v9x_u32 v9x_glide3_key4(v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 d)
{
    return v9x_glide_key(a, b, c, d);
}

/* ---- queries -------------------------------------------------------- */

/* grGet: the answer is written to params (FxI32 each) and its size in
 * bytes returned; an unknown pname or too small a buffer returns 0. */
v9x_u32 __stdcall grGet(v9x_u32 pname, v9x_u32 plength, v9x_u32 *params)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grGet);
    v9x_u32 values[4];
    v9x_u32 count = 1ul;
    v9x_u32 index;

    values[0] = 0ul;
    switch (pname) {
    case V9X_GR_BITS_DEPTH:               values[0] = 16ul; break;
    case V9X_GR_BITS_RGBA:
        values[0] = 5ul; values[1] = 6ul; values[2] = 5ul; values[3] = 0ul;
        count = 4ul;
        break;
    case V9X_GR_FIFO_FULLNESS:            values[0] = 0ul; break;
    case V9X_GR_FOG_TABLE_ENTRIES:        values[0] = V9X_GLIDE_FOG_TABLE_SIZE; break;
    case V9X_GR_GAMMA_TABLE_ENTRIES:      values[0] = V9X_GLIDE3_GAMMA_ENTRIES; break;
    case V9X_GR_BITS_GAMMA:               values[0] = 8ul; break;
    case V9X_GR_IS_BUSY:                  values[0] = 0ul; break;
    case V9X_GR_LFB_PIXEL_PIPE:           values[0] = 0ul; break;
    case V9X_GR_MAX_TEXTURE_SIZE:         values[0] = V9X_GLIDE3_MAX_TEXTURE; break;
    case V9X_GR_MAX_TEXTURE_ASPECT_RATIO: values[0] = V9X_GLIDE3_MAX_ASPECT; break;
    case V9X_GR_MEMORY_FB:                values[0] = V9X_GLIDE3_MEMORY_FB; break;
    case V9X_GR_MEMORY_TMU:               values[0] = V9X_GLIDE3_MEMORY_TMU; break;
    case V9X_GR_MEMORY_UMA:               values[0] = 0ul; break;
    case V9X_GR_NUM_BOARDS:               values[0] = 1ul; break;
    case V9X_GR_NUM_FB:                   values[0] = 1ul; break;
    case V9X_GR_NUM_SWAP_HISTORY_BUFFER:  values[0] = 0ul; break;
    case V9X_GR_NUM_TMU:                  values[0] = 1ul; break;
    case V9X_GR_PENDING_BUFFERSWAPS:      values[0] = 0ul; break;
    case V9X_GR_REVISION_FB:              values[0] = V9X_GLIDE3_REVISION; break;
    case V9X_GR_REVISION_TMU:             values[0] = V9X_GLIDE3_REVISION; break;
    case V9X_GR_SUPPORTS_PASSTHRU:        values[0] = 0ul; break;
    case V9X_GR_TEXTURE_ALIGN:            values[0] = V9X_GLIDE3_TEXTURE_ALIGN; break;
    case V9X_GR_VIDEO_POSITION:           values[0] = 0ul; break;
    case V9X_GR_ZDEPTH_MIN_MAX:
        values[0] = 0xfffful; values[1] = 0ul; count = 2ul;
        break;
    case V9X_GR_WDEPTH_MIN_MAX:
        values[0] = 1ul; values[1] = 0xfffful; count = 2ul;
        break;
    default:
        count = 0ul;
        break;
    }
    if (count == 0ul || params == 0 || plength < count * 4ul) {
        if (v9x_glide_noted(V9X_GLIDE3_IX_grGet, v9x_glide3_key4(pname, plength, 1ul, 0ul))) {
            v9x_glide_logf(V9X_GLIDE3_IX_grGet, call, "pname=%02lX plength=%lu unanswered",
                           pname, plength);
        }
        return 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        params[index] = values[index];
    }
    if (v9x_glide_noted(V9X_GLIDE3_IX_grGet, v9x_glide3_key4(pname, plength, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grGet, call, "pname=%02lX -> %lu (%lu bytes)",
                       pname, values[0], count * 4ul);
    }
    return count * 4ul;
}

const char *__stdcall grGetString(v9x_u32 pname)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grGetString);
    const char *answer = v9x_glide3_extension;

    switch (pname) {
    case V9X_GR_HARDWARE: answer = v9x_glide3_hardware; break;
    case V9X_GR_RENDERER: answer = v9x_glide3_renderer; break;
    case V9X_GR_VENDOR:   answer = v9x_glide3_vendor; break;
    case V9X_GR_VERSION:  answer = v9x_glide3_version; break;
    default:              break;
    }
    if (v9x_glide_noted(V9X_GLIDE3_IX_grGetString, pname)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grGetString, call, "pname=%02lX -> %s",
                       pname, answer);
    }
    return answer;
}

static int v9x_glide3_matches(v9x_u32 wanted, v9x_u32 value)
{
    return wanted == V9X_GR_QUERY_ANY || wanted == value;
}

/*
 * grQueryResolutions(template, output): the GrResolution entries
 * (resolution, refresh, numColorBuffers, numAuxBuffers; 16 bytes) that
 * match the template, GR_QUERY_ANY matching anything. Returns their size
 * in bytes and writes them when output is not null. Rollcage cannot start
 * without an answer (census). Offered: the three modes above at 60 Hz,
 * one to three colour buffers and up to one aux buffer, where 16-bit
 * buffers fit the frame buffer less the FIFO reserve.
 */
v9x_u32 __stdcall grQueryResolutions(const v9x_u32 *want, v9x_u32 *output)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grQueryResolutions);
    v9x_u32 size = 0ul;
    v9x_u32 mode;
    v9x_u32 color;
    v9x_u32 aux;

    if (want == 0) {
        v9x_glide_logf(V9X_GLIDE3_IX_grQueryResolutions, call, "template=null -> 0");
        return 0ul;
    }
    for (mode = 0ul; mode < 3ul; ++mode) {
        if (!v9x_glide3_matches(want[0], v9x_glide3_modes[mode][0]) ||
            !v9x_glide3_matches(want[1], V9X_GR_REFRESH_60Hz)) {
            continue;
        }
        for (color = 1ul; color <= V9X_GLIDE3_COLOR_BUFFERS_MAX; ++color) {
            for (aux = 0ul; aux <= V9X_GLIDE3_AUX_BUFFERS_MAX; ++aux) {
                if (!v9x_glide3_matches(want[2], color) ||
                    !v9x_glide3_matches(want[3], aux) ||
                    v9x_glide3_modes[mode][1] * v9x_glide3_modes[mode][2] * 2ul *
                        (color + aux) >=
                        V9X_GLIDE3_MEMORY_FB - V9X_GLIDE3_FIFO_RESERVE) {
                    continue;
                }
                size += V9X_GR_RESOLUTION_ENTRY_BYTES;
                if (output != 0) {
                    output[0] = v9x_glide3_modes[mode][0];
                    output[1] = V9X_GR_REFRESH_60Hz;
                    output[2] = color;
                    output[3] = aux;
                    output += 4;
                }
            }
        }
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grQueryResolutions, call,
                   "template=%08lX,%08lX,%08lX,%08lX output=%08lX -> %lu bytes",
                   want[0], want[1], want[2], want[3], (v9x_u32)output, size);
    return size;
}

/* ---- initialisation, the window and contexts ------------------------ */

void __stdcall grGlideInit(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grGlideInit);

    v9x_glide_logf(V9X_GLIDE3_IX_grGlideInit, call, "");
    v9x_glide3_layout_init(&v9x_glide3_layout);
}

void __stdcall grGlideShutdown(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grGlideShutdown);

    v9x_glide_logf(V9X_GLIDE3_IX_grGlideShutdown, call, "");
    v9x_glide_summary("shutdown");
    v9x_glide_close();
    v9x_glide3_context_open = 0ul;
}

void __stdcall grSstSelect(v9x_u32 which)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grSstSelect);

    v9x_glide_logf(V9X_GLIDE3_IX_grSstSelect, call, "sst=%lu", which);
}

/*
 * grSstWinOpen(hWnd, resolution, refresh, colorFormat, origin, nColBuf,
 * nAuxBuf): a GrContext_t, zero on failure. One context is kept; an open
 * while one is open replaces it, which is how Diablo II moves from
 * 640x480 to 800x600 (census).
 */
v9x_u32 __stdcall grSstWinOpen(v9x_u32 window, v9x_u32 resolution,
                               v9x_u32 refresh, v9x_u32 color_format,
                               v9x_u32 origin, v9x_u32 color_buffers,
                               v9x_u32 aux_buffers)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grSstWinOpen);
    v9x_u32 context;

    context = v9x_glide_open((void *)window, resolution, color_format, origin,
                             color_buffers, aux_buffers) ? 1ul : 0ul;
    v9x_glide3_context_open = context;
    v9x_glide_logf(V9X_GLIDE3_IX_grSstWinOpen, call,
                   "hwnd=%08lX res=%lu refresh=%lu cformat=%lu origin=%lu colbuf=%lu auxbuf=%lu -> %lu",
                   window, resolution, refresh, color_format, origin,
                   color_buffers, aux_buffers, context);
    return context;
}

v9x_u32 __stdcall grSstWinClose(v9x_u32 context)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grSstWinClose);

    v9x_glide_logf(V9X_GLIDE3_IX_grSstWinClose, call, "context=%lu", context);
    v9x_glide_summary("winclose");
    v9x_glide_close();
    v9x_glide3_context_open = 0ul;
    return V9X_GLIDE3_TRUE;
}

v9x_u32 __stdcall grSelectContext(v9x_u32 context)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grSelectContext);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grSelectContext, call, "context=%lu -> %lu",
                       context, v9x_glide3_context_open);
    }
    return v9x_glide3_context_open != 0ul ? V9X_GLIDE3_TRUE : V9X_GLIDE3_FALSE;
}

void __stdcall grFinish(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grFinish);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grFinish, call, "");
    }
    v9x_glide_flush();
}

void __stdcall grFlush(void)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grFlush);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grFlush, call, "");
    }
    v9x_glide_flush();
}

/* ---- buffers ------------------------------------------------------- */

void __stdcall grBufferClear(v9x_u32 color, v9x_u32 alpha, v9x_u32 depth)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grBufferClear);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grBufferClear,
                        v9x_glide_key(color, alpha, depth, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grBufferClear, call,
                       "color=%08lX alpha=%02lX depth=%04lX", color,
                       alpha & 0xfful, depth & 0xfffful);
    }
    v9x_glide_clear(color, depth);
}

void __stdcall grBufferSwap(v9x_u32 interval)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grBufferSwap);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grBufferSwap, call, "interval=%lu",
                       interval);
    }
    v9x_glide_swap(interval);
}

void __stdcall grRenderBuffer(v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grRenderBuffer);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grRenderBuffer, buffer)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grRenderBuffer, call, "buffer=%lu", buffer);
    }
    v9x_glide_set_render_buffer(buffer);
}

/* A clip edge as Glide 3 passes it, FxU32 but negative in practice:
 * Rollcage animates a panel whose bottom edge starts at -7 (census). Read
 * as unsigned it would open the window to the whole screen; clamped to
 * zero it leaves the window empty, which is what the game asks for. */
static v9x_u32 v9x_glide3_clip_edge(v9x_u32 value)
{
    return (value & 0x80000000ul) != 0ul ? 0ul : value;
}

void __stdcall grClipWindow(v9x_u32 min_x, v9x_u32 min_y, v9x_u32 max_x,
                            v9x_u32 max_y)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grClipWindow);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grClipWindow,
                        v9x_glide_key(min_x, min_y, max_x, max_y))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grClipWindow, call,
                       "min=%ld,%ld max=%ld,%ld", (long)min_x, (long)min_y,
                       (long)max_x, (long)max_y);
    }
    v9x_glide_state.clip_min_x = v9x_glide3_clip_edge(min_x);
    v9x_glide_state.clip_min_y = v9x_glide3_clip_edge(min_y);
    v9x_glide_state.clip_max_x = v9x_glide3_clip_edge(max_x);
    v9x_glide_state.clip_max_y = v9x_glide3_clip_edge(max_y);
}

/* No write mask reaches the engine yet; neither test title calls it. */
void __stdcall grColorMask(v9x_u32 rgb, v9x_u32 alpha)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grColorMask);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grColorMask, v9x_glide_key(rgb, alpha, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grColorMask, call, "rgb=%lu alpha=%lu (ignored)",
                       rgb, alpha);
    }
}

void __stdcall grCoordinateSpace(v9x_u32 mode)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grCoordinateSpace);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grCoordinateSpace, mode)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grCoordinateSpace, call, "mode=%lu", mode);
    }
    v9x_glide3_coordinates = mode;
}

/* ---- render state -------------------------------------------------- */

/* The one-argument state calls: log the value, then store it (`store`, a
 * statement on `value`). A macro so each keeps its own index and name. */
#define V9X_GLIDE3_STATE1(name, label, store)                                 \
    void __stdcall name(v9x_u32 value)                                        \
    {                                                                         \
        v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_##name);                 \
                                                                              \
        if (v9x_glide_noted(V9X_GLIDE3_IX_##name, value)) {                   \
            v9x_glide_logf(V9X_GLIDE3_IX_##name, call, label "=%08lX", value); \
        }                                                                     \
        store;                                                                \
    }

V9X_GLIDE3_STATE1(grCullMode, "mode", v9x_glide_state.cull_mode = value)
V9X_GLIDE3_STATE1(grDepthBufferMode, "mode", v9x_glide_state.depth_mode = value)
V9X_GLIDE3_STATE1(grDepthBufferFunction, "func",
                  v9x_glide_state.depth_func = value)
V9X_GLIDE3_STATE1(grDepthMask, "enable", v9x_glide_state.depth_mask = value)
/* No render-interface field. */
V9X_GLIDE3_STATE1(grDepthBiasLevel, "bias", (void)value)
/* The engines dither 16-bit targets as they choose. */
V9X_GLIDE3_STATE1(grDitherMode, "mode", (void)value)
V9X_GLIDE3_STATE1(grAlphaTestFunction, "func", v9x_glide_state.alpha_func = value)
V9X_GLIDE3_STATE1(grAlphaTestReferenceValue, "ref",
                  v9x_glide_state.alpha_ref = value & 0xfful)
V9X_GLIDE3_STATE1(grChromakeyMode, "mode", v9x_glide_state.chroma_mode = value)
V9X_GLIDE3_STATE1(grChromakeyValue, "color", v9x_glide_state.chroma_value = value)
V9X_GLIDE3_STATE1(grFogColorValue, "color", v9x_glide_state.fog_color = value)
V9X_GLIDE3_STATE1(grConstantColorValue, "color",
                  v9x_glide_state.constant_color = value)

/* grFogMode: Glide 3's sources as the engine's. Table fog on Q (the
 * W-based table) and iterated alpha carry over; the fog-coordinate and
 * iterated-Z sources draw unfogged and are logged as such. */
void __stdcall grFogMode(v9x_u32 mode)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grFogMode);
    v9x_u32 source = mode & V9X_GR_FOG_SOURCE_MASK;
    v9x_u32 engine = V9X_GLIDE_FOG_DISABLE;

    if (source == V9X_GR_FOG_WITH_TABLE_ON_Q) {
        engine = V9X_GLIDE_FOG_TABLE;
    } else if (source == V9X_GR_FOG_WITH_ITERATED_ALPHA) {
        engine = V9X_GLIDE_FOG_ITERATED_ALPHA;
    }
    if (v9x_glide_noted(V9X_GLIDE3_IX_grFogMode, mode)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grFogMode, call, "mode=%08lX -> %lu", mode,
                       engine);
    }
    v9x_glide_state.fog_mode = (mode & ~V9X_GR_FOG_SOURCE_MASK) | engine;
}

void __stdcall grFogTable(const v9x_u8 *table)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grFogTable);

    if (table == 0) {
        v9x_glide_logf(V9X_GLIDE3_IX_grFogTable, call, "table=null");
        return;
    }
    v9x_glide_fog_load(table);
    if (v9x_glide_noted(V9X_GLIDE3_IX_grFogTable,
                        v9x_glide_checksum((const v9x_u32 *)table,
                                           V9X_GLIDE_FOG_TABLE_SIZE / 4u))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grFogTable, call, "first=%02X last=%02X",
                       (unsigned int)table[0],
                       (unsigned int)table[V9X_GLIDE_FOG_TABLE_SIZE - 1u]);
    }
}

void __stdcall grAlphaBlendFunction(v9x_u32 rgb_src, v9x_u32 rgb_dst,
                                    v9x_u32 alpha_src, v9x_u32 alpha_dst)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grAlphaBlendFunction);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grAlphaBlendFunction,
                        v9x_glide_key(rgb_src, rgb_dst, alpha_src, alpha_dst))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grAlphaBlendFunction, call,
                       "rgb=%lu,%lu alpha=%lu,%lu", rgb_src, rgb_dst,
                       alpha_src, alpha_dst);
    }
    /* The alpha factors blend a destination alpha the 16-bit targets do
     * not have. */
    v9x_glide_state.blend_src = rgb_src;
    v9x_glide_state.blend_dst = rgb_dst;
}

static void v9x_glide3_store_combine(V9X_GLIDE_COMBINE *combine,
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

void __stdcall grColorCombine(v9x_u32 function, v9x_u32 factor, v9x_u32 local,
                              v9x_u32 other, v9x_u32 invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grColorCombine);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grColorCombine,
                        v9x_glide_key(function, factor, local,
                                      other ^ (invert << 16)))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grColorCombine, call,
                       "func=%lu factor=%lu local=%lu other=%lu invert=%lu",
                       function, factor, local, other, invert);
    }
    v9x_glide3_store_combine(&v9x_glide_state.color, function, factor, local,
                             other, invert);
}

void __stdcall grAlphaCombine(v9x_u32 function, v9x_u32 factor, v9x_u32 local,
                              v9x_u32 other, v9x_u32 invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grAlphaCombine);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grAlphaCombine,
                        v9x_glide_key(function, factor, local,
                                      other ^ (invert << 16)))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grAlphaCombine, call,
                       "func=%lu factor=%lu local=%lu other=%lu invert=%lu",
                       function, factor, local, other, invert);
    }
    v9x_glide3_store_combine(&v9x_glide_state.alpha, function, factor, local,
                             other, invert);
}

/* Gamma has no render-interface field (Glide 2 ignores it too). */
void __stdcall grLoadGammaTable(v9x_u32 entries, const v9x_u32 *red,
                                const v9x_u32 *green, const v9x_u32 *blue)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grLoadGammaTable);

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grLoadGammaTable, call,
                       "entries=%lu r=%08lX g=%08lX b=%08lX (ignored)", entries,
                       (v9x_u32)red, (v9x_u32)green, (v9x_u32)blue);
    }
}

/* Three floats, logged as their bits (glide_core.c on why). */
void __stdcall guGammaCorrectionRGB(v9x_u32 red, v9x_u32 green, v9x_u32 blue)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_guGammaCorrectionRGB);

    if (v9x_glide_noted(V9X_GLIDE3_IX_guGammaCorrectionRGB,
                        v9x_glide_key(red, green, blue, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_guGammaCorrectionRGB, call,
                       "bits=%08lX,%08lX,%08lX (ignored)", red, green, blue);
    }
}

/* ---- textures ------------------------------------------------------ */

void __stdcall grTexClampMode(v9x_u32 tmu, v9x_u32 s_mode, v9x_u32 t_mode)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexClampMode);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexClampMode,
                        v9x_glide_key(tmu, s_mode, t_mode, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexClampMode, call, "tmu=%lu s=%lu t=%lu",
                       tmu, s_mode, t_mode);
    }
    v9x_glide_state.clamp_s = s_mode;
    v9x_glide_state.clamp_t = t_mode;
}

void __stdcall grTexFilterMode(v9x_u32 tmu, v9x_u32 min_filter,
                               v9x_u32 mag_filter)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexFilterMode);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexFilterMode,
                        v9x_glide_key(tmu, min_filter, mag_filter, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexFilterMode, call,
                       "tmu=%lu min=%lu mag=%lu", tmu, min_filter, mag_filter);
    }
    v9x_glide_state.min_filter = min_filter;
    v9x_glide_state.mag_filter = mag_filter;
}

/* The engine samples the largest level only; both test titles disable
 * mipmapping (census). */
void __stdcall grTexMipMapMode(v9x_u32 tmu, v9x_u32 mode, v9x_u32 lod_blend)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexMipMapMode);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexMipMapMode,
                        v9x_glide_key(tmu, mode, lod_blend, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexMipMapMode, call,
                       "tmu=%lu mode=%lu lodblend=%lu", tmu, mode, lod_blend);
    }
}

/* grTexCombine: tmu, rgb function, rgb factor, alpha function, alpha
 * factor, rgb invert, alpha invert. One TMU, as in glide_dll.c: the
 * unit's output is the texel when its function is LOCAL, uninverted. */
void __stdcall grTexCombine(v9x_u32 tmu, v9x_u32 rgb_function,
                            v9x_u32 rgb_factor, v9x_u32 alpha_function,
                            v9x_u32 alpha_factor, v9x_u32 rgb_invert,
                            v9x_u32 alpha_invert)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexCombine);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexCombine,
                        v9x_glide_key(tmu ^ (rgb_invert << 8) ^ (alpha_invert << 9),
                                      rgb_function, rgb_factor,
                                      alpha_function ^ (alpha_factor << 16)))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexCombine, call,
                       "tmu=%lu rgb=%lu,%lu alpha=%lu,%lu invert=%lu,%lu", tmu,
                       rgb_function, rgb_factor, alpha_function, alpha_factor,
                       rgb_invert, alpha_invert);
    }
    v9x_glide_state.tex_rgb_function = rgb_invert ?
        V9X_GLIDE3_COMBINE_FUNCTION_OTHER : rgb_function;
    v9x_glide_state.tex_alpha_function = alpha_invert ?
        V9X_GLIDE3_COMBINE_FUNCTION_OTHER : alpha_function;
}

v9x_u32 __stdcall grTexMinAddress(v9x_u32 tmu)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexMinAddress);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexMinAddress, tmu)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexMinAddress, call, "tmu=%lu -> 0", tmu);
    }
    return 0ul;
}

/* The highest start address that still fits the largest texture. */
v9x_u32 __stdcall grTexMaxAddress(v9x_u32 tmu)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexMaxAddress);
    v9x_u32 top = V9X_GLIDE3_MEMORY_TMU -
                  V9X_GLIDE3_MAX_TEXTURE * V9X_GLIDE3_MAX_TEXTURE * 2ul;

    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexMaxAddress, tmu)) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexMaxAddress, call, "tmu=%lu -> %08lX",
                       tmu, top);
    }
    return top;
}

/* A Glide 3 GrTexInfo as the engine's Glide 2 one (five words, data
 * last); V9X_FALSE, logged by the caller, when its numbers are out of
 * range. */
static int v9x_glide3_texinfo(const v9x_u32 *info, v9x_u32 *glide2)
{
    return info != 0 && v9x_glide3_texinfo_to_glide2(info, glide2);
}

/* The bytes a texture takes, by the engine's own arithmetic, so the game
 * lays textures out where the engine looks for them. */
v9x_u32 __stdcall grTexTextureMemRequired(v9x_u32 even_odd, const v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexTextureMemRequired);
    v9x_u32 glide2[5];
    V9X_GLIDE_TEXINFO texinfo;
    v9x_u32 total = 0ul;

    if (v9x_glide3_texinfo(info, glide2)) {
        texinfo.small_lod = glide2[0];
        texinfo.large_lod = glide2[1];
        texinfo.aspect = glide2[2];
        texinfo.format = glide2[3];
        total = v9x_glide_texmem_required(&texinfo, even_odd);
    }
    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexTextureMemRequired,
                        info == 0 ? even_odd :
                        v9x_glide_key(even_odd, info[0] ^ (info[1] << 8), info[2],
                                      info[3]))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexTextureMemRequired, call,
                       "evenodd=%lu info=%08lX -> %lu", even_odd, (v9x_u32)info,
                       total);
    }
    return total;
}

v9x_u32 __stdcall grTexCalcMemRequired(v9x_u32 small_lod, v9x_u32 large_lod,
                                       v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexCalcMemRequired);
    V9X_GLIDE_TEXINFO texinfo;
    v9x_u32 total = 0ul;

    texinfo.format = format;
    if (v9x_glide3_lod_to_glide2(small_lod, &texinfo.small_lod) &&
        v9x_glide3_lod_to_glide2(large_lod, &texinfo.large_lod) &&
        v9x_glide3_aspect_to_glide2(aspect, &texinfo.aspect)) {
        total = v9x_glide_texmem_required(&texinfo, V9X_GLIDE_MIPMAPLEVELMASK_BOTH);
    }
    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexCalcMemRequired,
                        v9x_glide_key(small_lod, large_lod, aspect, format))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexCalcMemRequired, call,
                       "lod=%ld..%ld aspect=%ld format=%lu -> %lu", (long)small_lod,
                       (long)large_lod, (long)aspect, format, total);
    }
    return total;
}

static v9x_u32 v9x_glide3_info_key(v9x_u32 address, const v9x_u32 *info)
{
    if (info == 0) {
        return address;
    }
    return v9x_glide_key(address, info[0] ^ (info[1] << 8), info[2], info[3]);
}

void __stdcall grTexSource(v9x_u32 tmu, v9x_u32 address, v9x_u32 even_odd,
                           const v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexSource);
    v9x_u32 glide2[5];
    int valid = v9x_glide3_texinfo(info, glide2);

    v9x_glide_texture_source(address, even_odd, valid ? glide2 : 0);
    if (!v9x_glide_noted(V9X_GLIDE3_IX_grTexSource,
                         v9x_glide3_info_key(address ^ (even_odd << 28), info))) {
        return;
    }
    if (info == 0) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexSource, call,
                       "tmu=%lu addr=%08lX evenodd=%lu info=null", tmu, address,
                       even_odd);
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grTexSource, call,
                   "tmu=%lu addr=%08lX evenodd=%lu lod=%ld..%ld aspect=%ld format=%lu%s",
                   tmu, address, even_odd, (long)info[0], (long)info[1],
                   (long)info[2], info[3], valid ? "" : " (out of range)");
}

void __stdcall grTexDownloadMipMap(v9x_u32 tmu, v9x_u32 address,
                                   v9x_u32 even_odd, const v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexDownloadMipMap);
    v9x_u32 glide2[5];
    int valid = v9x_glide3_texinfo(info, glide2);

    if (valid) {
        v9x_glide_texture_download(address, even_odd, glide2);
    }
    if (!v9x_glide_noted(V9X_GLIDE3_IX_grTexDownloadMipMap,
                         v9x_glide3_info_key(address ^ (even_odd << 28), info))) {
        return;
    }
    if (info == 0) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexDownloadMipMap, call,
                       "tmu=%lu addr=%08lX evenodd=%lu info=null", tmu, address,
                       even_odd);
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grTexDownloadMipMap, call,
                   "tmu=%lu addr=%08lX evenodd=%lu lod=%ld..%ld aspect=%ld format=%lu data=%08lX%s",
                   tmu, address, even_odd, (long)info[0], (long)info[1],
                   (long)info[2], info[3], info[4],
                   valid ? "" : " (out of range)");
}

/* grTexDownloadTable(type, data): Glide 3 drops Glide 2's tmu argument.
 * Palettes are 256 FxU32; the 6666 palette and NCC tables are not
 * converted. */
void __stdcall grTexDownloadTable(v9x_u32 type, const v9x_u32 *data)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grTexDownloadTable);
    v9x_u32 sum;

    if (data == 0) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexDownloadTable, call,
                       "type=%lu data=null", type);
        return;
    }
    if (type != V9X_GR_TEXTABLE_PALETTE) {
        if (v9x_glide_noted(V9X_GLIDE3_IX_grTexDownloadTable, type)) {
            v9x_glide_logf(V9X_GLIDE3_IX_grTexDownloadTable, call,
                           "type=%lu (not converted)", type);
        }
        return;
    }
    v9x_glide_palette_load(data);
    sum = v9x_glide_checksum(data, V9X_GLIDE_PALETTE_ENTRIES);
    if (v9x_glide_noted(V9X_GLIDE3_IX_grTexDownloadTable,
                        v9x_glide_key(type, sum, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grTexDownloadTable, call,
                       "type=%lu sum=%08lX first=%08lX %08lX %08lX %08lX",
                       type, sum, data[0], data[1], data[2], data[3]);
    }
}

/* ---- vertices and drawing ------------------------------------------- */

void __stdcall grVertexLayout(v9x_u32 param, v9x_u32 offset, v9x_u32 mode)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grVertexLayout);
    v9x_u16 known = v9x_glide3_layout_set(&v9x_glide3_layout, param, offset, mode);

    if (v9x_glide_noted(V9X_GLIDE3_IX_grVertexLayout,
                        v9x_glide_key(param, offset, mode, 0ul))) {
        v9x_glide_logf(V9X_GLIDE3_IX_grVertexLayout, call,
                       "param=%02lX offset=%lu mode=%lu%s", param, offset, mode,
                       known ? "" : " (unknown param)");
    }
}

/* Whether this draw can be taken: window coordinates only. Clip
 * coordinates need a projection no census title has asked for. */
static int v9x_glide3_drawable(void)
{
    if (v9x_glide3_coordinates == V9X_GR_WINDOW_COORDS) {
        return 1;
    }
    if (!v9x_glide3_clip_space_logged) {
        v9x_glide3_clip_space_logged = 1ul;
        v9x_glide_log("clip-coordinate draws are not drawn");
    }
    return 0;
}

/* The ith vertex of a draw: from an array of pointers, or `stride` bytes
 * apart from `base`. */
static const v9x_u8 *v9x_glide3_vertex_at(const v9x_u8 *const *pointers,
                                          const v9x_u8 *base, v9x_u32 stride,
                                          v9x_u32 i)
{
    if (pointers != 0) {
        return pointers[i];
    }
    return base + i * stride;
}

/* A primitive's triangles or lines through the engine. */
static void v9x_glide3_primitive(v9x_u32 mode, v9x_u32 count,
                                 const v9x_u8 *const *pointers,
                                 const v9x_u8 *base, v9x_u32 stride)
{
    float corners[3][V9X_GLIDE_VERTEX_FLOATS];
    v9x_u32 indices[3];
    v9x_u32 total;
    v9x_u32 n;
    unsigned int k;

    if (!v9x_glide3_drawable() || (pointers == 0 && base == 0)) {
        return;
    }
    total = v9x_glide3_triangle_count(mode, count);
    for (n = 0ul; n < total; ++n) {
        v9x_glide3_triangle_at(mode, n, indices);
        for (k = 0u; k < 3u; ++k) {
            const v9x_u8 *vertex = v9x_glide3_vertex_at(pointers, base, stride,
                                                        indices[k]);

            if (vertex == 0) {
                return;
            }
            v9x_glide3_vertex_read(&v9x_glide3_layout, vertex, corners[k]);
        }
        v9x_glide_triangle(corners[0], corners[1], corners[2]);
    }
    total = v9x_glide3_line_count(mode, count);
    for (n = 0ul; n < total; ++n) {
        v9x_glide3_line_at(mode, n, indices);
        for (k = 0u; k < 2u; ++k) {
            const v9x_u8 *vertex = v9x_glide3_vertex_at(pointers, base, stride,
                                                        indices[k]);

            if (vertex == 0) {
                return;
            }
            v9x_glide3_vertex_read(&v9x_glide3_layout, vertex, corners[k]);
        }
        v9x_glide_line(corners[0], corners[1]);
    }
}

/* The first draws log their first vertex's words, as the census did. */
static void v9x_glide3_log_vertex(unsigned int ix, v9x_u32 call,
                                  const v9x_u8 *vertex)
{
    const v9x_u32 *w = (const v9x_u32 *)vertex;

    if (vertex == 0) {
        return;
    }
    v9x_glide_logf(ix, call, "  v0 %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
                   w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
}

void __stdcall grDrawVertexArray(v9x_u32 mode, v9x_u32 count,
                                 const v9x_u8 *const *pointers)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grDrawVertexArray);

    v9x_glide3_primitive(mode, count, pointers, 0, 0ul);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grDrawVertexArray, call, "mode=%lu count=%lu swap=%lu",
                   mode, count, v9x_glide_swap_count());
    if (pointers != 0 && count != 0ul) {
        v9x_glide3_log_vertex(V9X_GLIDE3_IX_grDrawVertexArray, call, pointers[0]);
    }
}

void __stdcall grDrawVertexArrayContiguous(v9x_u32 mode, v9x_u32 count,
                                           const v9x_u8 *vertices,
                                           v9x_u32 stride)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grDrawVertexArrayContiguous);

    v9x_glide3_primitive(mode, count, 0, vertices, stride);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grDrawVertexArrayContiguous, call,
                   "mode=%lu count=%lu stride=%lu swap=%lu", mode, count, stride,
                   v9x_glide_swap_count());
    if (count != 0ul) {
        v9x_glide3_log_vertex(V9X_GLIDE3_IX_grDrawVertexArrayContiguous, call,
                              vertices);
    }
}

void __stdcall grDrawTriangle(const v9x_u8 *a, const v9x_u8 *b,
                              const v9x_u8 *c)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grDrawTriangle);
    const v9x_u8 *corners[3];

    corners[0] = a;
    corners[1] = b;
    corners[2] = c;
    v9x_glide3_primitive(V9X_GLIDE3_TRIANGLES, 3ul, corners, 0, 0ul);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grDrawTriangle, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide3_log_vertex(V9X_GLIDE3_IX_grDrawTriangle, call, a);
}

void __stdcall grDrawLine(const v9x_u8 *a, const v9x_u8 *b)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grDrawLine);
    const v9x_u8 *ends[2];

    ends[0] = a;
    ends[1] = b;
    v9x_glide3_primitive(V9X_GLIDE3_LINES, 2ul, ends, 0, 0ul);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grDrawLine, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide3_log_vertex(V9X_GLIDE3_IX_grDrawLine, call, a);
}

/* Points are counted and logged, not drawn, as in GLIDE2X.DLL. */
void __stdcall grDrawPoint(const v9x_u8 *a)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grDrawPoint);

    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE3_IX_grDrawPoint, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide3_log_vertex(V9X_GLIDE3_IX_grDrawPoint, call, a);
}

/* ---- linear frame buffer ------------------------------------------- */

/* grLfbLock(type, buffer, writeMode, origin, pixelPipeline, info):
 * GrLfbInfo_t has Glide 2's layout, so the engine's lock serves both. */
v9x_u32 __stdcall grLfbLock(v9x_u32 type, v9x_u32 buffer, v9x_u32 write_mode,
                            v9x_u32 origin, v9x_u32 pipeline, v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grLfbLock);

    return v9x_glide_lfb_lock(V9X_GLIDE3_IX_grLfbLock, call, type, buffer,
                              write_mode, origin, pipeline, info);
}

v9x_u32 __stdcall grLfbUnlock(v9x_u32 type, v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE3_IX_grLfbUnlock);

    return v9x_glide_lfb_unlock(V9X_GLIDE3_IX_grLfbUnlock, call, type, buffer);
}

/* ---- the DLL ------------------------------------------------------- */

BOOL __stdcall V9xGlide3Entry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* The program and the build every later line is read against. */
        char path[MAX_PATH];
        char text[MAX_PATH + 96];

        if (!v9x_glide_core_attach(V9X_DIAG_GLIDE3_LOG, v9x_glide3_names,
                                   V9X_GLIDE3_EXPORT_COUNT)) {
            return FALSE;
        }
        v9x_glide3_layout_init(&v9x_glide3_layout);
        if (GetModuleFileNameA(0, path, sizeof(path)) == 0ul) {
            path[0] = '\0';
        }
        path[sizeof(path) - 1u] = '\0';
        wsprintfA(text, "attach instance=%08lX version=%s %s exe=%s",
                  (DWORD)instance, V9X_VERSION_STRING, v9x_glide3_build_id, path);
        v9x_glide_log(text);
    } else if (reason == DLL_PROCESS_DETACH) {
        v9x_glide_core_detach();
    }
    return TRUE;
}
