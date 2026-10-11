/*
 * GLIDE2X.DLL, Glide 2.x over the render interface
 * (docs\plans\glide-2x-wrapper.md). Phase 0 made it a census of the calls
 * Need for Speed II SE makes; Phase 2 gives it a device. grSstWinOpen takes
 * the screen (glide_surface.c); clears, swaps, frame-buffer locks and
 * untextured triangles and lines reach it through the render interface,
 * with the state mapped by glide_state.c and the vertices by
 * glide_vertex.c. Textured draws are counted and skipped until Phase 3.
 *
 * This file is the Glide 2 front end: the exports and the DLL entry. The
 * engine behind them, shared with GLIDE3X.DLL, is glide_core.c
 * (glide_core.h); it keeps the log, the census counts and everything that
 * draws.
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
 * rules above V9X_GLIDE_CAPTURE_FIRST in glide_core.c: whole frames now and
 * then, and each distinct argument set once. Every export is counted, and the counts
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
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "glide_api.h"
#include "glide_state.h"
#include "glide_texmem.h"
#include "glide_surface.h"
#include "glide_core.h"
#include "glide_vertex.h"

#define V9X_GLIDE_STUB_HOOK(ix) v9x_glide_stub_called(ix)
#define V9X_GLIDE_DEFINE_STUBS
#include "glide_exports_gen.h"

static const char v9x_glide_build_id[] = "V9XGLIDE build=" V9X_BUILD_ID;
static const char *const v9x_glide_names[V9X_GLIDE_EXPORT_COUNT] =
    V9X_GLIDE_EXPORT_NAMES;

/* The census in glide_core.c counts by export index. */
typedef char v9x_glide_exports_fit_core[
    V9X_GLIDE_EXPORT_COUNT <= V9X_GLIDE_CORE_EXPORTS_MAX ? 1 : -1];

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
 * grSstStatus. The Voodoo Graphics status register (SST-1 spec, "status"):
 * bits 0-5 PCI FIFO free space, bit 6 vertical retrace, bits 7-9 busy. The
 * census build reports an idle board with a full FIFO and flips bit 6 each
 * call, so a game polling for either retrace polarity gets out of its loop.
 */
#define V9X_GLIDE_STATUS_FIFO_FREE 0x3ful
#define V9X_GLIDE_STATUS_VRETRACE  0x40ul

static v9x_u32 v9x_glide_status_toggle;

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
    v9x_u32 opened;

    opened = v9x_glide_open((void *)window, resolution, color_format, origin,
                            color_buffers, aux_buffers) ?
             V9X_GLIDE_TRUE : V9X_GLIDE_FALSE;
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

    if (v9x_glide_sampled(call)) {
        v9x_glide_logf(V9X_GLIDE_IX_grBufferSwap, call, "interval=%lu",
                       interval);
    }
    v9x_glide_swap(interval);
}

void __stdcall grRenderBuffer(v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grRenderBuffer);

    if (v9x_glide_noted(V9X_GLIDE_IX_grRenderBuffer, buffer)) {
        v9x_glide_logf(V9X_GLIDE_IX_grRenderBuffer, call, "buffer=%lu", buffer);
    }
    v9x_glide_set_render_buffer(buffer);
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
/* The constant colour a combine's CONSTANT reads. Carmageddon II's menu
 * text takes its alpha from it; as stubs these left it zero and every glyph
 * drew transparent (netbook, 2026-10-11). */
V9X_GLIDE_STATE1(grConstantColorValue, "color",
                 v9x_glide_state.constant_color = value)

void __stdcall grConstantColorValue4(float a, float r, float g, float b)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grConstantColorValue4);
    v9x_u32 argb = v9x_glide_argb_from_floats(a, r, g, b);

    if (v9x_glide_noted(V9X_GLIDE_IX_grConstantColorValue4, argb)) {
        v9x_glide_logf(V9X_GLIDE_IX_grConstantColorValue4, call,
                       "argb=%08lX", argb);
    }
    v9x_glide_set_constant_argb(argb);
}
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

/* The utility library's colour presets (glide_state.c). Carmageddon II sets
 * its colour combine only this way; as a stub it left the vertex colour in
 * force and the menu drew black (netbook, 2026-10-11). An unknown preset is
 * logged and leaves the combine as it was. */
void __stdcall guColorCombineFunction(v9x_u32 preset)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_guColorCombineFunction);
    v9x_u16 known = v9x_glide_gu_color_combine(preset, &v9x_glide_state.color);

    if (v9x_glide_noted(V9X_GLIDE_IX_guColorCombineFunction, preset)) {
        v9x_glide_logf(V9X_GLIDE_IX_guColorCombineFunction, call,
                       "preset=%lu known=%lu", preset, (v9x_u32)known);
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
    v9x_glide_store_combine(&v9x_glide_state.alpha, function, factor, local,
                            other, invert);
}

void __stdcall grFogTable(const v9x_u32 *table)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grFogTable);
    v9x_u32 key;

    if (table == 0) {
        v9x_glide_logf(V9X_GLIDE_IX_grFogTable, call, "table=null");
        return;
    }
    v9x_glide_fog_load((const v9x_u8 *)table);
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

/*
 * Only GR_HINT_STWHINT is acted on: its W_DIFF_TMU0 bit gives texturing
 * TMU 0's own W. Carmageddon II calls this around 180,000 times a run and,
 * as a stub, nothing said what it asked; its menu text then drew with the
 * vertex oow it never wrote (netbook, 2026-10-11). Each new value is logged.
 */
void __stdcall grHints(v9x_u32 type, v9x_u32 mask)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grHints);

    if (v9x_glide_noted(V9X_GLIDE_IX_grHints,
                        v9x_glide_key(type, mask, 0ul, 0ul))) {
        v9x_glide_logf(V9X_GLIDE_IX_grHints, call, "type=%lu mask=%08lX",
                       type, mask);
    }
    if (type == V9X_GLIDE_HINT_STWHINT) {
        v9x_glide_state.stw_hint = mask;
    }
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

    v9x_glide_texture_source(address, even_odd, info);
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
    /* NCC tables are for the YIQ formats, which are not converted. */
    if (type == V9X_GLIDE_TEXTABLE_PALETTE) {
        v9x_glide_palette_load(data);
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

/* grLfbLock: type, buffer, writeMode, origin, pixelPipeline, info. */
v9x_u32 __stdcall grLfbLock(v9x_u32 type, v9x_u32 buffer, v9x_u32 write_mode,
                            v9x_u32 origin, v9x_u32 pipeline, v9x_u32 *info)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbLock);

    return v9x_glide_lfb_lock(V9X_GLIDE_IX_grLfbLock, call, type, buffer,
                              write_mode, origin, pipeline, info);
}

v9x_u32 __stdcall grLfbUnlock(v9x_u32 type, v9x_u32 buffer)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbUnlock);

    return v9x_glide_lfb_unlock(V9X_GLIDE_IX_grLfbUnlock, call, type, buffer);
}

/* grLfbReadRegion reads 16-bit pixels; off the device it returns black. */
v9x_u32 __stdcall grLfbReadRegion(v9x_u32 buffer, v9x_u32 x, v9x_u32 y,
                                  v9x_u32 width, v9x_u32 height,
                                  v9x_u32 dst_stride, v9x_u8 *dst)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grLfbReadRegion);

    return v9x_glide_lfb_read(V9X_GLIDE_IX_grLfbReadRegion, call, buffer, x, y,
                              width, height, dst_stride, dst);
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

    v9x_glide_triangle((const float *)a, (const float *)b, (const float *)c);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawTriangle, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'a', a);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'b', b);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawTriangle, call, 'c', c);
}

void __stdcall grDrawLine(const v9x_u32 *a, const v9x_u32 *b)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grDrawLine);

    v9x_glide_line((const float *)a, (const float *)b);
    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawLine, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawLine, call, 'a', a);
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawLine, call, 'b', b);
}

void __stdcall grDrawPoint(const v9x_u32 *a)
{
    v9x_u32 call = v9x_glide_count(V9X_GLIDE_IX_grDrawPoint);

    if (!v9x_glide_sampled(call)) {
        return;
    }
    v9x_glide_logf(V9X_GLIDE_IX_grDrawPoint, call, "swap=%lu",
                   v9x_glide_swap_count());
    v9x_glide_vertex(V9X_GLIDE_IX_grDrawPoint, call, 'a', a);
}

/* ---- the DLL ------------------------------------------------------- */

BOOL __stdcall V9xGlideEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* The program and the build every later line is read against. */
        char path[MAX_PATH];
        char text[MAX_PATH + 160];
        SYSTEMTIME now;

        if (!v9x_glide_core_attach(V9X_DIAG_GLIDE_LOG, v9x_glide_names,
                                   V9X_GLIDE_EXPORT_COUNT)) {
            return FALSE;
        }
        if (GetModuleFileNameA(0, path, sizeof(path)) == 0ul) {
            path[0] = '\0';
        }
        path[sizeof(path) - 1u] = '\0';
        /* When, as in V9XGL.LOG: the clock and uptime place the session
         * against a snapshot's DumpTime and DumpUptimeMs. */
        GetLocalTime(&now);
        wsprintfA(text, "attach instance=%08lX version=%s %s "
                  "time=%04u-%02u-%02u %02u:%02u:%02u uptime-ms=%lu exe=%s",
                  (DWORD)instance, V9X_VERSION_STRING, v9x_glide_build_id,
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond, GetTickCount(), path);
        v9x_glide_log(text);
    } else if (reason == DLL_PROCESS_DETACH) {
        v9x_glide_core_detach();
    }
    return TRUE;
}
