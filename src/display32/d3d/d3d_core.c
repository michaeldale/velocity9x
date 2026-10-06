/*
 * Chip-neutral Direct3D core for V9XHAL.DLL.
 *
 * Everything about this driver's Direct3D that is not about a particular
 * chip: the context pool, the texture handle table, render-state
 * bookkeeping, the software clipper, and the DDHAL entry points DDRAW calls.
 * The chip half is behind V9X_D3D_ENGINE_OPS in d3d_internal.h. This file
 * writes no hardware register and names no chip's register vocabulary, which
 * is the property the split exists to create; check-tree.ps1 asserts it rather
 * than leaving it to this comment.
 *
 * Split out of d3d_virge.c on 2026-08-29. The bodies here are the ones that
 * passed the DirectDraw/Direct3D probe before the split, moved unchanged;
 * what changed is where the ViRGE's own numbers come from, which is now
 * ops->limits rather than a literal in the routine.
 *
 * Nothing outside this file reaches into it. DriverInit calls v9x_d3d_publish
 * to fill the shared block's D3D fields and callback tables, and DDRAW then
 * calls the V9xD3d* entry points directly. Two gates keep a chip without an
 * S3D core out: the 16-bit side nulls lpD3D* and GetDriverInfo for any engine
 * whose engine_caps lack D3D, and v9x_d3d_engine() below resolves no engine
 * for one this file has no implementation for.
 */
#include "d3d_internal.h"
/* offsetof only: a header of macros, nothing the nodefaultlibs link must
 * find, for the layout assertions against the neutral core's vertex. */
#include <stddef.h>
#include "r3d/r3d.h"
#include "r3d/r3d_cull.h"
#include "r3d/r3d_runs.h"
#include "r3d/r3d_records.h"
#include "r3d/r3d_validate.h"
#include "velocity9x/r3d_abi.h"
#include "d3d_state.h"
#include "d3d_select.h"
#include "d3d_dp2.h"
#include "velocity9x/diag_identity.h"


#if V9X_C3_SERVE_D3D_CALLBACKS2
static const BYTE v9x_guid_d3d_callbacks2[16] = {
    0xe1u, 0x84u, 0xa5u, 0x0bu, 0xb6u, 0x70u, 0xd0u, 0x11u,
    0x88u, 0x9du, 0x00u, 0xaau, 0x00u, 0xbbu, 0xb7u, 0x6au
};
#endif

/*
 * GUID_D3DExtendedCaps, transcribed from the Windows 98 DDK's DDRAWI.H:
 *   0x7de41f80, 0x9d93, 0x11d0, {0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29}
 *
 * Its Data1 is what the 2026-09-20 ViRGE run saw the runtime asking for and
 * being refused. Laid out little-endian, the way the guidInfo bytes arrive.
 */
static const BYTE v9x_guid_d3d_extended_caps[16] = {
    0x80u, 0x1fu, 0xe4u, 0x7du, 0x93u, 0x9du, 0xd0u, 0x11u,
    0x89u, 0xabu, 0x00u, 0xa0u, 0xc9u, 0x05u, 0x41u, 0x29u
};

/*
 * The two DDI 6 GUIDs, from the Windows SDK's ddrawi.h (the Windows 98 DDK
 * has neither):
 *   GUID_D3DCallbacks3  0xddf41230, 0xec0a, 0x11d0,
 *                       {0xa9,0xb6,0x00,0xaa,0x00,0xc0,0x99,0x3e}
 *   GUID_ZPixelFormats  0x93869880, 0x36cf, 0x11d1,
 *                       {0x9b,0x1b,0x00,0xaa,0x00,0xbb,0xb8,0xae}
 * Both Data1 values are in the GUID table of the Rage XL's DxDiag run
 * (2026-10-06), asked for and declined.
 */
static const BYTE v9x_guid_d3d_callbacks3[16] = {
    0x30u, 0x12u, 0xf4u, 0xddu, 0x0au, 0xecu, 0xd0u, 0x11u,
    0xa9u, 0xb6u, 0x00u, 0xaau, 0x00u, 0xc0u, 0x99u, 0x3eu
};

static const BYTE v9x_guid_z_pixel_formats[16] = {
    0x80u, 0x98u, 0x86u, 0x93u, 0xcfu, 0x36u, 0xd1u, 0x11u,
    0x9bu, 0x1bu, 0x00u, 0xaau, 0x00u, 0xbbu, 0xb8u, 0xaeu
};

/*
 * GUID_D3DParseUnknownCommandCallback, ddrawi.h:
 *   0x2e04ffa0, 0x98e4, 0x11d1, {0x8c,0xe1,0x00,0xa0,0xc9,0x06,0x29,0xa8}
 *
 * NOT optional on this runtime, whatever the documentation implies. Read
 * out of Windows 98's DDRAW.DLL (4.09.0000.0904, the DirectX 9.0c runtime)
 * on 2026-10-06: when the global data carries D3DDEVCAPS_DRAWPRIMITIVES2,
 * the code after the GUID_D3DCallbacks3 query requires that table to pass
 * its flag/pointer check, requires DrawPrimitives2 AND
 * ValidateTextureStageState non-null, and then offers its parser through
 * this GUID and requires DDHAL_DRIVER_HANDLED with DD_OK. Any failure skips
 * the rest of the driver's setup, and DirectDraw runs the device without
 * its HAL: "DDraw Status: Not Available" in DxDiag, which is what four
 * builds did before the code was read
 * (docs\probe\a8u4i5-rage-xl-pci-2026-10-06\README.md).
 */
static const BYTE v9x_guid_d3d_parse_unknown[16] = {
    0xa0u, 0xffu, 0x04u, 0x2eu, 0xe4u, 0x98u, 0xd1u, 0x11u,
    0x8cu, 0xe1u, 0x00u, 0xa0u, 0xc9u, 0x06u, 0x29u, 0xa8u
};

/* The runtime's parser, from the GUID above. One per process space is
 * enough: it is the same DDRAW.DLL entry for every driver object. */
static V9X_D3D_PARSE_UNKNOWN_FN v9x_d3d_parse_unknown;

static V9X_D3D_CONTEXT v9x_d3d_contexts[V9X_D3D_CONTEXT_COUNT];
static V9X_D3D_TEXTURE v9x_d3d_textures[V9X_D3D_TEXTURE_COUNT];
/* Where TextureCreate looks for a free slot first. */
static DWORD v9x_d3d_texture_next;

/*
 * The list builder's staging for v9x_d3d_draw_list, which is 6 KB and is
 * NOT on the stack on purpose.
 *
 * On the stack it was a 6,216-byte frame under a 6,868-byte DrawPrimitives
 * frame, and Final Reality's render thread had committed only 16 KB of its
 * 1 MB stack when it called in. The draw path's first push into the page
 * below the committed limit killed the process, with no fault dialog and no
 * V9XTRACE.INI, instead of growing the stack
 * (docs\issues\2026-09-30-final-reality-robots-faults-inside-drawprimitives-on-the-netbook.md).
 * The obvious alternative, probing the stack a page at a time, assumes that
 * a first touch of that page from inside a HAL callback grows the stack,
 * which is exactly what did not happen.
 *
 * One buffer serves every caller: DirectDraw holds the Win16 mutex around
 * every HAL callback (measured,
 * docs\decisions\2026-09-26-98se-directdraw-holds-the-win16-mutex-around-every-hal-callback-measured.md),
 * nothing in the HAL waits or yields inside one, and v9x_d3d_draw_list is
 * never re-entered: its sink goes to an engine, not back through here.
 */
static V9X_R3D_VERTEX v9x_d3d_list_staging[V9X_D3D_INDEXED_BATCH * 3u];

/*
 * The triangles a draw callback gathers before handing them to
 * v9x_d3d_draw_list: DrawPrimitives' record run, and DrawOneIndexedPrimitive's
 * copy out of the vertex pool. Off the stack for the reason above; as stack
 * arrays they made 6,868- and 6,292-byte frames, touched at the bottom on
 * entry.
 *
 * One buffer for both, because neither body calls the other and the Win16
 * mutex serializes the callbacks. It must stay distinct from the staging:
 * v9x_d3d_draw_list reads this as its input while it writes survivors there.
 */
static V9X_D3DTLVERTEX v9x_d3d_gather[V9X_D3D_INDEXED_BATCH * 3u];

/* Source colour keys by surface; see ddhal_internal.h. */
static V9X_D3D_COLOR_KEY v9x_d3d_color_keys[V9X_D3D_COLOR_KEY_COUNT];

V9X_D3D_COLOR_KEY *v9x_d3d_color_key_find(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD index;

    if (surface == 0) {
        return 0;
    }
    for (index = 0ul; index < V9X_D3D_COLOR_KEY_COUNT; ++index) {
        if (v9x_d3d_color_keys[index].surface == surface) {
            return &v9x_d3d_color_keys[index];
        }
    }
    return 0;
}

void v9x_d3d_color_key_set(const V9X_DD_SURFACE_LCL *surface, DWORD flags,
                           DWORD low, DWORD high)
{
    V9X_D3D_COLOR_KEY *entry;

    /* Only a source blit key means "these texels are transparent". A
     * destination key or an overlay key is something else, and a call that
     * clears the source key (no SRCBLT flag) is treated as removal below
     * only when it names that key. */
    if (surface == 0 || (flags & V9X_DDCKEY_SRCBLT) == 0ul) {
        return;
    }
    entry = v9x_d3d_color_key_find(surface);
    if (entry == 0) {
        /* First free slot. Not through v9x_d3d_color_key_find(0): that
         * refuses a null surface by design, and asking it for one found
         * nothing - measured as color_key_draws staying at zero on the
         * probe's keyed rung while the draw path had reached this table. */
        DWORD index;

        for (index = 0ul; index < V9X_D3D_COLOR_KEY_COUNT; ++index) {
            if (v9x_d3d_color_keys[index].surface == 0) {
                entry = &v9x_d3d_color_keys[index];
                break;
            }
        }
    }
    if (entry == 0) {
        return;                              /* table full: key not honoured */
    }
    entry->surface = surface;
    entry->low = low;
    entry->high = high;
    entry->dirty = 1ul;
}

void v9x_d3d_color_key_touch(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_COLOR_KEY *entry = v9x_d3d_color_key_find(surface);

    if (entry != 0) {
        entry->dirty = 1ul;
    }
}

void v9x_d3d_color_key_forget(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_COLOR_KEY *entry = v9x_d3d_color_key_find(surface);

    if (entry != 0) {
        entry->surface = 0;
        entry->dirty = 0ul;
    }
}

/* Rewritten 4444 alpha masks by surface; see ddhal_internal.h. */
static V9X_D3D_ALPHA_MASK v9x_d3d_alpha_masks[V9X_D3D_ALPHA_MASK_COUNT];

static V9X_D3D_ALPHA_MASK *v9x_d3d_alpha_mask_find(
    const V9X_DD_SURFACE_LCL *surface)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_ALPHA_MASK_COUNT; ++index) {
        if (v9x_d3d_alpha_masks[index].surface == surface) {
            return &v9x_d3d_alpha_masks[index];
        }
    }
    return 0;
}

V9X_D3D_ALPHA_MASK *v9x_d3d_alpha_mask_entry(
    const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_ALPHA_MASK *entry;

    if (surface == 0) {
        return 0;
    }
    entry = v9x_d3d_alpha_mask_find(surface);
    if (entry != 0) {
        return entry;
    }
    /* A free slot is one naming no surface. */
    entry = v9x_d3d_alpha_mask_find(0);
    if (entry != 0) {
        entry->surface = surface;
        entry->key = 0ul;
    }
    return entry;
}

void v9x_d3d_alpha_mask_touch(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_ALPHA_MASK *entry;

    if (surface == 0) {
        return;
    }
    entry = v9x_d3d_alpha_mask_find(surface);
    if (entry != 0) {
        entry->key = 0ul;
    }
}

void v9x_d3d_alpha_mask_forget(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_ALPHA_MASK *entry;

    if (surface == 0) {
        return;
    }
    entry = v9x_d3d_alpha_mask_find(surface);
    if (entry != 0) {
        entry->surface = 0;
        entry->key = 0ul;
    }
}
static V9X_D3DHAL_CALLBACKS2 v9x_d3d_callbacks2;
/* GUID_D3DCallbacks3's answer, served only at DDI 6. */
static V9X_D3DHAL_CALLBACKS3 v9x_d3d_callbacks3;
DWORD __stdcall V9xD3dClear2(V9X_D3DHAL_CLEAR2DATA *data);
static DWORD v9x_d3d_dp2_probe(void);

/*
 * Breadcrumbs for the DDI 6 bring-up, under Direct3DDdiProbe bit 16
 * (engine_abi.h). DxDiag at DDI 6 hung Windows with the Win16 lock held on
 * A8U4I5 (2026-10-06), and a hang loses every counter in the shared block,
 * so each line goes to disk write-through and the file is closed after it.
 * Fixed storage and KERNEL32 file I/O only, as the SiS timeout log. Bounded
 * at 4,000 lines a boot so a frame loop cannot fill the disk.
 */
#define V9X_D3D_DP2_LOG_PATH "C:\\V9XDIAG\\V9XDP2.LOG"
static DWORD v9x_d3d_dp2_log_lines;

void v9x_d3d_dp2_log(const char *event, DWORD a, DWORD b)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[96];
    int at = 0;
    int shift;
    HANDLE file;
    DWORD written;
    DWORD tick = GetTickCount();

    if ((v9x_d3d_dp2_probe() & V9X_DP2_PROBE_LOG) == 0ul ||
        v9x_d3d_dp2_log_lines >= 4000ul) {
        return;
    }
    ++v9x_d3d_dp2_log_lines;
    for (shift = 28; shift >= 0; shift -= 4) {
        line[at++] = digits[(tick >> shift) & 0xful];
    }
    line[at++] = ' ';
    while (*event != '\0' && at < 60) {
        line[at++] = *event++;
    }
    line[at++] = ' ';
    for (shift = 28; shift >= 0; shift -= 4) {
        line[at++] = digits[(a >> shift) & 0xful];
    }
    line[at++] = ' ';
    for (shift = 28; shift >= 0; shift -= 4) {
        line[at++] = digits[(b >> shift) & 0xful];
    }
    line[at++] = '\r';
    line[at++] = '\n';
    file = CreateFileA(V9X_D3D_DP2_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    WriteFile(file, line, (DWORD)at, &written, 0);
    CloseHandle(file);
}

static const V9X_D3D_ENGINE_OPS *v9x_d3d_selected_ops(v9x_u32 selection)
{
    switch (selection) {
    case V9X_D3D_SELECT_SOFTWARE:
        return &v9x_d3d_engine_soft;
    case V9X_D3D_SELECT_VIRGE:
        return &v9x_d3d_engine_virge;
    case V9X_D3D_SELECT_GEN3:
        return &v9x_d3d_engine_i9xx;
    case V9X_D3D_SELECT_MACH64:
        return &v9x_d3d_engine_mach64;
    case V9X_D3D_SELECT_RAGE2:
        return &v9x_d3d_engine_rage2;
    case V9X_D3D_SELECT_SIS6326:
        return &v9x_d3d_engine_sis6326;
    default:
        return 0;
    }
}

/*
 * Which engine draws for this chip.
 *
 * The same shape as ddhal_core.c's v9x_engine32(), and for the same reason:
 * one HAL binary carries every engine and every family links it, so the
 * engine_type the 16-bit side filled in is what decides whose code runs. A
 * chip with no D3D implementation resolves null here and every entry point
 * below declines, which is a second gate behind the 16-bit capability clamp
 * rather than a replacement for it.
 */
const V9X_D3D_ENGINE_OPS *v9x_d3d_engine(void)
{
    if (v9x_hal == 0 || (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul) {
        return 0;
    }
    /*
     * The software rasterizer by capability, then the chip by engine_type;
     * d3d_select.c holds the rule and its test. Gen3 resolves and then
     * reports itself not ready until its submission path is trusted.
     */
    return v9x_d3d_selected_ops(
        v9x_d3d_select_engine(1, v9x_hal->engine.engine_type,
                              v9x_hal->engine.engine_caps));
}

/*
 * How wide a depth pixel is on this chip, for the 2D side's depth fill.
 *
 * Resolved through v9x_d3d_engine() rather than v9x_d3d_publish_engine(),
 * because this is asked at blit time, when the descriptor is valid and the
 * answer must be the fitted chip's. Zero on a chip with no D3D engine, which
 * is what makes DDBLT_DEPTHFILL decline there: a card with no depth buffers
 * can be asked to clear one only by a caller that has gone wrong.
 */
DWORD v9x_d3d_depth_bytes_per_pixel(void)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (ops == 0 || ops->limits == 0) {
        return 0ul;
    }
    return ops->limits->depth_bits_per_pixel >> 3;
}

/* A DDBLT_DEPTHFILL value as the fitted engine's depth buffer holds it
 * (v9x_d3d_state_depth_fill); unchanged on a chip with no D3D engine. */
DWORD v9x_d3d_depth_fill_value(DWORD value)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (ops == 0 || ops->limits == 0) {
        return value;
    }
    return v9x_d3d_state_depth_fill(value,
                                    ops->limits->depth_bits_per_pixel,
                                    ops->limits->depth_fill_shift);
}

/*
 * Re-read the render target's address from DirectDraw before it is used.
 *
 * A flip does not move pixels: DDRAW swaps the fpVidMem of the front and
 * back surface objects, so the same back-buffer object the application (and
 * this context) holds points at a different page after every flip. The
 * address recorded at ContextCreate/SetRenderTarget is therefore right for
 * exactly one frame; from the second frame on, every other frame was being
 * rendered straight into the page on the monitor. Final Reality on the
 * emulated ViRGE flickered for that reason on 2026-09-03, after its flips
 * were correctly paced - the stock driver on the same emulator did not.
 *
 * The lookup is the one place every draw entry passes through, and both
 * engines read target_offset per draw, so this is the whole fix. Pitch,
 * size and format are the chain's, identical on both pages; an offset the
 * registers cannot express keeps the validated one rather than a sentinel.
 */
static void v9x_d3d_refresh_target(V9X_D3D_CONTEXT *context)
{
    DWORD offset;

    if (context->target == 0) {
        return;
    }
    offset = v9x_surface_offset(context->target);
    if (offset == 0xfffffffful) {
        return;
    }
    context->target_offset = offset;
    if (v9x_hal != 0) {
        v9x_hal->d3d_diagnostics.target_offset = offset;
        v9x_hal->d3d_diagnostics.target_pitch = context->pitch;
        v9x_hal->d3d_diagnostics.target_width = context->width;
        v9x_hal->d3d_diagnostics.target_height = context->height;
    }
}

static V9X_D3D_CONTEXT *v9x_d3d_context_from_handle(DWORD handle)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        if ((DWORD)&v9x_d3d_contexts[index] == handle &&
            v9x_d3d_contexts[index].active != 0ul) {
            v9x_d3d_refresh_target(&v9x_d3d_contexts[index]);
            return &v9x_d3d_contexts[index];
        }
    }
    return 0;
}

/*
 * Every draw batch, through one place, so the present trace records the
 * destination the engine was ACTUALLY given rather than whatever a later
 * context lookup happened to leave in the diagnostics. The context index
 * goes with it: a trace that cannot name the context cannot tell a second
 * context's target from the same context rebound.
 */
/*
 * The batch as the neutral core describes it, from the context: the three
 * surfaces resolved here, because they are DirectDraw's, and the render
 * state through d3d_state.c, whose identity mapping the compiler checks.
 * An engine on ops->draw reads this and nothing else of the context.
 *
 * The texture is resolved once per batch rather than once per triangle as
 * the engines did through v9x_d3d_context_texture_surface, so the
 * draws_no_handle and draws_handle_unresolved counters count batches from
 * here on (docs\plans\opengl-1.1-icd.md, Phase 1b).
 */
static void v9x_d3d_describe_draw(V9X_D3D_CONTEXT *context, V9X_R3D_DRAW *draw)
{
    V9X_D3D_STATE_RAW raw;

    draw->target.offset = context->target_offset;
    draw->target.pitch = context->pitch;
    draw->target.width = context->width;
    draw->target.height = context->height;
    draw->target.format = context->target_format;
    draw->target.object = context->target;
    draw->depth.offset = context->depth_offset;
    draw->depth.pitch = context->depth_pitch;
    draw->depth.width = context->width;
    draw->depth.height = context->height;
    draw->depth.format = 0ul;
    draw->depth.object = context->zbuffer;
    draw->texture.object = v9x_d3d_context_texture_surface(context);
    /* Direct3D never states the render interface's explicit fields; zero
     * them, since `draw` is the caller's uninitialised local. */
    draw->texture.levels = 0;
    draw->texture.level_count = 0ul;
    draw->explicit_state = 0ul;
    draw->vertex_alpha_opaque = 0ul;
    draw->texture1.object = 0;
    draw->texture1.levels = 0;
    draw->texture1.level_count = 0ul;
    draw->texcoords1 = 0;
    raw.z_enable = context->z_enable;
    raw.z_write = context->z_write;
    raw.z_func = context->z_func;
    raw.alpha_blend_enable = context->alpha_blend_enable;
    raw.src_blend = context->src_blend;
    raw.dest_blend = context->dest_blend;
    raw.texture_min = context->texture_min;
    raw.texture_mag = context->texture_mag;
    raw.texture_blend = context->texture_blend;
    raw.texture_address = context->texture_address;
    raw.texture_border = context->texture_border;
    raw.texture_wrap = context->texture_wrap;
    raw.wrap_u = context->wrap_u;
    raw.wrap_v = context->wrap_v;
    raw.shade_mode = context->shade_mode;
    raw.specular_enable = context->specular_enable;
    raw.fog_enable = context->fog_enable;
    raw.fog_color = context->fog_color;
    raw.alpha_test_enable = context->alpha_test_enable;
    raw.alpha_func = context->alpha_func;
    raw.alpha_ref = context->alpha_ref;
    raw.alpha_force = context->alpha_force;
    raw.color_key_enable = context->color_key_enable;
    v9x_d3d_state_fill(&raw, draw);
}

/* A byte of the census's packed state; a value past 255 reads as 255. */
static DWORD v9x_d3d_census_byte(DWORD value)
{
    return value > 0xfful ? 0xfful : value;
}

/*
 * What an untextured batch is. Diagnostics only: nothing here changes a
 * draw. The first triangle stands for the batch, which is what Direct3D's
 * render state already does - one state per batch.
 *
 * Varying coordinates are the tell. The Rage XL's lost lightmaps carried
 * tu/tv of 0.25 to 0.97 with no texture bound; geometry an application
 * means to draw untextured leaves them constant, usually zero.
 */
static void v9x_d3d_census_untextured(const V9X_D3D_CONTEXT *context,
                                      const V9X_D3DTLVERTEX *vertices)
{
    V9X_D3D_DIAGNOSTICS *diag;

    if (v9x_hal == 0 || context->texture_handle != 0ul || vertices == 0) {
        return;
    }
    diag = &v9x_hal->d3d_diagnostics;
    if (context->alpha_blend_enable == 0ul) {
        ++diag->no_handle_blend_off;
    } else if (context->src_blend == V9X_D3DBLEND_DESTCOLOR ||
               context->dest_blend == V9X_D3DBLEND_SRCCOLOR) {
        ++diag->no_handle_blend_modulate;
    } else {
        ++diag->no_handle_blend_other;
    }
    if (vertices[0].tu != vertices[1].tu || vertices[0].tu != vertices[2].tu ||
        vertices[0].tv != vertices[1].tv || vertices[0].tv != vertices[2].tv) {
        ++diag->no_handle_with_uv;
    }
    if ((vertices[0].color & 0x00fffffful) == 0x00fffffful) {
        ++diag->no_handle_white;
    }
    diag->no_handle_last_state =
        v9x_d3d_census_byte(context->src_blend) |
        (v9x_d3d_census_byte(context->dest_blend) << 8) |
        (v9x_d3d_census_byte(context->alpha_blend_enable) << 16) |
        (v9x_d3d_census_byte(context->texture_blend) << 24);
    diag->no_handle_last_color = vertices[0].color;
}

/* One batch to the engine, by whichever entry it serves. */
static int v9x_d3d_dispatch_draw(const V9X_D3D_ENGINE_OPS *ops,
                                 V9X_D3D_CONTEXT *context,
                                 const V9X_D3DTLVERTEX *vertices,
                                 DWORD triangle_count)
{
    V9X_R3D_DRAW draw;

    if (triangle_count != 0ul) {
        v9x_d3d_census_untextured(context, vertices);
    }
    if (ops->draw == 0) {
        return ops->draw_triangles(context, vertices, triangle_count);
    }
    v9x_d3d_describe_draw(context, &draw);
    return ops->draw(&draw, (const V9X_R3D_VERTEX *)vertices, triangle_count);
}

static int v9x_d3d_draw_batch(const V9X_D3D_ENGINE_OPS *ops,
                              V9X_D3D_CONTEXT *context,
                              const V9X_D3DTLVERTEX *vertices,
                              DWORD triangle_count)
{
    /* The retrace source's duty cycle, sampled where the batches are. A
     * real vertical blank is a few per cent of a frame; a ratio near 1 is
     * a source that always says yes, which would release every buffer
     * before its latch. Sampling is about when this code ran, so it is
     * taken whether or not the batch goes on to be submitted. */
    ++v9x_hal->d3d_diagnostics.vblank_samples;
    if (v9x_in_vblank()) {
        ++v9x_hal->d3d_diagnostics.vblank_in_blank;
    }
    /*
     * Everything that claims a draw HAPPENED is recorded after the backend
     * returns, and only if it returned success.
     *
     * Two reasons, both of which produced a wrong reading first. The
     * backends wait here - d3d_i9xx.c for a pending flip since intel78,
     * d3d_virge.c since 2026-09-19 - and that wait calls v9x_flip_done,
     * which emits the FLIP_DONE record; tracing the draw first wrote
     * ACCEPTED, DRAW, DONE for a batch that had correctly waited, the
     * exact pattern this ring exists to call premature reuse. And a
     * backend can REFUSE: the ViRGE path returns without submitting when
     * its flip wait runs out, so a record taken regardless would claim a
     * submission that never happened, and would consume the first-draw
     * marker so the real submission went untraced.
     *
     * And a backend's SUCCESS is not a submission either: the ViRGE path
     * returns success without launching a command for a degenerate
     * triangle, a thin one, and a blend its unit cannot express, and
     * returns failure from the middle of a batch whose earlier triangles
     * DID launch. So neither the return value nor the ordering decides
     * this. The submission count, incremented where commands actually
     * reach the hardware, does; the rule that consumes the marker is in
     * src\common\drawnote.c and is tested there.
     */
    {
        DWORD before = v9x_present_submissions();
        int ok = v9x_d3d_dispatch_draw(ops, context, vertices,
                                       triangle_count);
        int submitted = v9x_present_submissions() != before ? 1 : 0;

        /*
         * Into the buffer the panel was last told to show? Counted per
         * SUBMITTED batch, so it does not depend on the ring's length.
         * Zero across a run says the engine was never aimed at the
         * presented buffer; a large count is premature reuse or a stale
         * binding, and the trace says which by where it sits relative to
         * the flip.
         */
        if (submitted &&
            context->target_offset == v9x_present_flip_offset()) {
            ++v9x_hal->d3d_diagnostics.draws_into_presented;
        }
        if (v9x_present_draw_record(submitted)) {
            v9x_present_trace(V9X_PRESENT_TRACE_DRAW,
                              (DWORD)(context - v9x_d3d_contexts),
                              context->target_offset);
        }
        return ok;
    }
}

/* A handle is an entry's address, so it is checked, not searched for: a
 * scan of the table per draw grew with the table. */
static V9X_D3D_TEXTURE *v9x_d3d_texture_from_handle(DWORD handle,
                                                     DWORD context)
{
    DWORD base = (DWORD)&v9x_d3d_textures[0];
    DWORD offset;
    V9X_D3D_TEXTURE *texture;

    if (handle < base) {
        return 0;
    }
    offset = handle - base;
    if (offset % (DWORD)sizeof(V9X_D3D_TEXTURE) != 0ul ||
        offset / (DWORD)sizeof(V9X_D3D_TEXTURE) >= V9X_D3D_TEXTURE_COUNT) {
        return 0;
    }
    texture = &v9x_d3d_textures[offset / (DWORD)sizeof(V9X_D3D_TEXTURE)];
    if (texture->active == 0ul || texture->context != context) {
        return 0;
    }
    return texture;
}

/*
 * Where a surface pointer came from, recorded with it when it is refused.
 *
 * The fault address names the helper and not its caller, and the helper has
 * ten of them. Numbered rather than named because the value crosses into a
 * capture file; the mask below is one bit per site, so a boot reports every
 * site that ever handed over a bad pointer and not only the last.
 */
/* Sites 1 and 2 (the teardown scan and the bound-texture lookup) are retired:
 * both now read the value resolved at creation and dereference nothing. The
 * numbers are not reused so an older capture still reads. */
#define V9X_D3D_LCL_SITE_TARGET           3ul
#define V9X_D3D_LCL_SITE_ZBUFFER          4ul
/* Sites 5 and 6 (the colour-key touches in TextureSwap) are retired for the
 * same reason; the swap moves the resolved value with its wrapper. */
#define V9X_D3D_LCL_SITE_ONEPRIM_EXE      7ul
#define V9X_D3D_LCL_SITE_PRIMS_EXE        8ul
#define V9X_D3D_LCL_SITE_RENDERPRIM_EXE   9ul
#define V9X_D3D_LCL_SITE_RENDERPRIM_TL   10ul
#define V9X_D3D_LCL_SITE_TEXTURE_CREATE  11ul
/* The render interface's texture surface (its target and depth go through
 * v9x_d3d_set_target, and are counted as sites 3 and 4). */
#define V9X_D3D_LCL_SITE_R3D_TEXTURE     12ul

static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface, DWORD site);

/*
 * The Direct3D clients seen in this DLL's lifetime.
 *
 * Eight is enough to say "one application" or "more than one", which is the
 * question: a capture whose counters are cumulative from the boot cannot be
 * attributed to a run without it. Beyond eight the distinct count keeps
 * rising and the table stops growing, because the exact number matters far
 * less than whether it is one.
 */
/*
 * The most render states this driver will walk in one block.
 *
 * A backstop, not a limit on what an application may send: the real bound is
 * the execute buffer's extent, and this only catches a buffer that reports
 * none. 1024 states is 8 KB, against the 276 intel96 measured - and against
 * the 64 that used to make a longer block apply nothing at all.
 */
#define V9X_D3D_STATE_MAX 1024u

#define V9X_D3D_CLIENT_SLOTS 8u
static DWORD v9x_d3d_clients[V9X_D3D_CLIENT_SLOTS];
static DWORD v9x_d3d_client_count = 0ul;

static void v9x_d3d_note_client(DWORD pid)
{
    DWORD index;

    if (v9x_hal == 0 || pid == 0ul) {
        return;
    }
    v9x_hal->d3d_diagnostics.d3d_pid_last = pid;
    if (v9x_hal->d3d_diagnostics.d3d_pid_first == 0ul) {
        v9x_hal->d3d_diagnostics.d3d_pid_first = pid;
    }

    for (index = 0ul; index < v9x_d3d_client_count &&
                      index < (DWORD)V9X_D3D_CLIENT_SLOTS; ++index) {
        if (v9x_d3d_clients[index] == pid) {
            return;
        }
    }

    if (v9x_d3d_client_count < (DWORD)V9X_D3D_CLIENT_SLOTS) {
        v9x_d3d_clients[v9x_d3d_client_count] = pid;
    }
    ++v9x_d3d_client_count;
    ++v9x_hal->d3d_diagnostics.d3d_pid_distinct;
}

/*
 * The calling program, into the trace identity's process table
 * (diag_identity.h), so a snapshot can say which programs its counters came
 * from. Both callers run in the application's own process - DirectDraw
 * calls the HAL there, and the ICD is loaded into it - so the module that
 * started the process is the program. KERNEL32 only: the r3d caller holds
 * the Win16 mutex.
 */
static void v9x_d3d_note_process(v9x_u32 kind)
{
    char path[MAX_PATH];
    char name[V9X_DIAG_PROCESS_NAME_BYTES];

    if (v9x_hal == 0) {
        return;
    }
    if (GetModuleFileNameA(0, path, sizeof(path)) == 0ul) {
        return;
    }
    path[sizeof(path) - 1u] = '\0';
    v9x_diag_base_name(name, path, (v9x_u32)sizeof(name));
    v9x_diag_note_process(&v9x_hal->identity, name, kind, GetTickCount());
}

static void v9x_d3d_textures_destroy_context(DWORD context)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].context == context) {
            v9x_d3d_textures[index].active = 0ul;
            v9x_d3d_textures[index].context = 0ul;
            v9x_d3d_textures[index].surface = 0;
            v9x_d3d_textures[index].lcl = 0;
        }
    }
}

/*
 * A surface is going away; no texture record may still name it.
 *
 * Not part of the context lifetime, and that is the point. An application can
 * release a texture's surface without calling TextureDestroy - the runtime
 * destroys handles with their context, not with their surface - and the
 * record then held a pointer to a freed lpLcl for the sampler to read. That
 * is reachable today and this closes it; DestroySurface already forgets a
 * colour key by surface, and this is the same call beside it for the same
 * reason.
 */
void v9x_d3d_textures_forget_surface(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD index;

    if (surface == 0) {
        return;
    }
    /* Compared as the value resolved at creation. Reading the wrapper here
     * is what intel55 measured (site 1, twice in one boot): a wrapper freed
     * earlier answers "no surface", compares unequal, and the entry is never
     * cleared. */
    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].lcl == surface) {
            v9x_d3d_textures[index].active = 0ul;
            v9x_d3d_textures[index].context = 0ul;
            v9x_d3d_textures[index].surface = 0;
            v9x_d3d_textures[index].lcl = 0;
        }
    }
}

DWORD v9x_d3d_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (data == 0 || ops == 0 || ops->create_surface == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    return ops->create_surface(data);
}

void v9x_d3d_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (data == 0 || ops == 0 || ops->destroy_surface == 0) {
        return;
    }
    ops->destroy_surface(data);
}

/*
 * The surface the context has a texture bound to, or null.
 *
 * The one service the engine asks of the core: the handle table is core
 * state, and whether the surface behind a handle is sampleable is an engine
 * question, so the lookup is here and the judgement is there.
 */
V9X_DD_SURFACE_LCL *v9x_d3d_context_texture_surface(
    const V9X_D3D_CONTEXT *context)
{
    V9X_D3D_TEXTURE *texture;

    if (context == 0 || context->texture_handle == 0ul) {
        /*
         * No handle is not an error - untextured geometry is ordinary. It is
         * counted because intel94 measured 3DMark99 drawing 96.5 per cent of
         * its primitives untextured with every refusal counter at zero, and
         * a zero here was the only way that could happen. Which zero it is
         * decides the fix, so the two are separated.
         */
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.draws_no_handle;
        }
        return 0;
    }
    texture = v9x_d3d_texture_from_handle(context->texture_handle,
                                           (DWORD)context);
    if (texture == 0 || texture->lcl == 0) {
        /* A handle that names no live texture of this context. Unlike the
         * case above this is a defect wherever it comes from: the
         * application bound something, and the binding was lost. */
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.draws_handle_unresolved;
        }
        return 0;
    }
    return texture->lcl;
}

DWORD __stdcall V9xD3dRenderPrimitive(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data);

/*
 * The screen-space clipper and the list builder live in the neutral render
 * core, src\display323d3d_clip.c, since Phase 1a of the OpenGL plan
 * (2026-09-26); what stays here is the Direct3D context's part of each
 * call - the executing engine's guard band and the render target's size -
 * and the two decisions the list builder asks the front end for. The
 * V9X_D3DTLVERTEX arrays are handed over as V9X_R3D_VERTEX, the same layout
 * field for field: the size is asserted here, the offsets on the host.
 */
typedef char v9x_d3d_assert_r3d_vertex[
    sizeof(V9X_R3D_VERTEX) == sizeof(V9X_D3DTLVERTEX) ? 1 : -1];
typedef char v9x_d3d_assert_r3d_list_staging[
    V9X_D3D_INDEXED_BATCH * 3u * sizeof(V9X_R3D_VERTEX) == 6144u ? 1 : -1];
typedef char v9x_d3d_assert_r3d_vertex_offsets[
    (offsetof(V9X_R3D_VERTEX, sx) == offsetof(V9X_D3DTLVERTEX, sx) &&
     offsetof(V9X_R3D_VERTEX, rhw) == offsetof(V9X_D3DTLVERTEX, rhw) &&
     offsetof(V9X_R3D_VERTEX, color) == offsetof(V9X_D3DTLVERTEX, color) &&
     offsetof(V9X_R3D_VERTEX, specular) == offsetof(V9X_D3DTLVERTEX, specular) &&
     offsetof(V9X_R3D_VERTEX, tu) == offsetof(V9X_D3DTLVERTEX, tu) &&
     offsetof(V9X_R3D_VERTEX, tv) == offsetof(V9X_D3DTLVERTEX, tv)) ? 1 : -1];
typedef char v9x_d3d_assert_r3d_fan[
    V9X_D3D_MAX_FAN_TRIANGLES == V9X_R3D_MAX_FAN_TRIANGLES ? 1 : -1];

static int v9x_d3d_clip_triangle(const V9X_D3D_CONTEXT *context,
                                 const V9X_D3DTLVERTEX *triangle,
                                 V9X_D3DTLVERTEX *result)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (ops == 0) {
        return -1;
    }
    return v9x_r3d_clip_triangle((const V9X_R3D_VERTEX *)triangle,
                                 ops->limits->coordinate_limit,
                                 (float)context->width,
                                 (float)context->height,
                                 (V9X_R3D_VERTEX *)result);
}

/*
 * Whether this triangle is a back face the application asked to remove.
 *
 * The mode applied is the context's, gated on the caps the engine published
 * (r3d_cull.h says why): describe_caps wrote them into the shared block, and
 * the same bits are what the application read before choosing a cull mode.
 * Decided on the vertices as handed over - before clipping, which keeps the
 * winding, and after the strip builders, which swap every odd triangle so
 * that a strip arrives here with one consistent winding.
 */
static int v9x_d3d_triangle_culled(const V9X_D3D_CONTEXT *context,
                                   const V9X_D3DTLVERTEX *triangle)
{
    DWORD misc = v9x_hal != 0
        ? v9x_hal->d3d_global.hwCaps.dpcTriCaps.dwMiscCaps : 0ul;
    unsigned long mode = v9x_r3d_cull_honoured(
        context->cull_mode,
        (misc & V9X_D3DPMISCCAPS_CULLCW) != 0ul,
        (misc & V9X_D3DPMISCCAPS_CULLCCW) != 0ul);

    return v9x_r3d_cull_triangle(mode,
                                 triangle[0].sx, triangle[0].sy,
                                 triangle[1].sx, triangle[1].sy,
                                 triangle[2].sx, triangle[2].sy);
}

/* What the list builder is given back: the engine and the context a batch
 * is drawn on, behind the void pointer the neutral core carries. */
typedef struct v9x_d3d_list_sink {
    const V9X_D3D_ENGINE_OPS *ops;
    V9X_D3D_CONTEXT *context;
} V9X_D3D_LIST_SINK;

static int v9x_d3d_list_batch(void *user, const V9X_R3D_VERTEX *vertices,
                              v9x_u32 triangle_count)
{
    V9X_D3D_LIST_SINK *sink = (V9X_D3D_LIST_SINK *)user;

    return v9x_d3d_draw_batch(sink->ops, sink->context,
                              (const V9X_D3DTLVERTEX *)vertices,
                              triangle_count);
}

static int v9x_d3d_list_culled(void *user, const V9X_R3D_VERTEX *triangle)
{
    V9X_D3D_LIST_SINK *sink = (V9X_D3D_LIST_SINK *)user;

    return v9x_d3d_triangle_culled(sink->context,
                                   (const V9X_D3DTLVERTEX *)triangle);
}

/*
 * A triangle list from one of the DX5 entry points, clipped where the engine
 * needs it, then drawn; r3d_clip.c says how the runs and fans are formed.
 */
static int v9x_d3d_draw_list(const V9X_D3D_ENGINE_OPS *ops,
                             V9X_D3D_CONTEXT *context,
                             const V9X_D3DTLVERTEX *vertices,
                             DWORD triangle_count)
{
    V9X_D3D_LIST_SINK sink;
    V9X_R3D_LIST list;
    V9X_R3D_LIST_STATS stats;
    int ok;

    sink.ops = ops;
    sink.context = context;
    list.guard_limit = ops->limits->coordinate_limit;
    list.width = (float)context->width;
    list.height = (float)context->height;
    list.clip_in_core = ops->limits->clip_in_core;
    list.batch = v9x_d3d_list_batch;
    list.culled = v9x_d3d_list_culled;
    list.user = &sink;
    list.staging = v9x_d3d_list_staging;
    list.staging_triangles = (v9x_u32)V9X_D3D_INDEXED_BATCH;
    list.stats = &stats;
    ok = v9x_r3d_draw_list(&list, (const V9X_R3D_VERTEX *)vertices,
                           triangle_count);
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.r3d_list_calls;
        v9x_hal->d3d_diagnostics.r3d_list_triangles_in +=
            stats.triangles_in;
        v9x_hal->d3d_diagnostics.r3d_list_culled +=
            stats.triangles_culled;
        v9x_hal->d3d_diagnostics.r3d_list_clipped +=
            stats.triangles_clipped;
        v9x_hal->d3d_diagnostics.r3d_list_sink_batches +=
            stats.sink_batches;
    }
    return ok;
}

/*
 * The DirectDraw surface wrapper's local half, or nothing.
 *
 * THE POINTER IS NOT OURS. It arrives in a HAL callback's data block, and
 * this function used to test it for null and then read lpLcl - which is
 * exactly what it did on 2026-09-16 when Final Reality issued its first
 * RenderPrimitive with an lpExeBuf or lpTLBuf that was non-null and not a
 * surface. The HAL took an access violation at the `mov eax,0x4[eax]` that
 * reads that field and the application died with it, in two consecutive
 * boots, at the same module offset both times
 * (docs\issues\2026-09-16-final-reality-renders-black-and-the-hal-faults.md).
 *
 * A HAL cannot validate a pointer the runtime hands it. What it can do is
 * refuse to die on one: IsBadReadPtr asks the kernel whether the two dwords
 * are readable, and a callback that returns "no surface" produces a refused
 * draw, which the caller already handles everywhere this is used.
 *
 * Why this is worth a kernel call on a path taken twice per primitive: the
 * alternative measured behaviour is the whole application terminating. It is
 * also the only instrument that can say WHICH pointer was bad - the counters
 * carry the value, and a bad pointer that is merely refused leaves a number
 * behind rather than a machine somebody had to power off.
 *
 * IsBadReadPtr is a KERNEL32 import, which is the only DLL this HAL is
 * permitted to import from and already does.
 */
static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface, DWORD site)
{
    V9X_DD_SURFACE_INT *wrapper = (V9X_DD_SURFACE_INT *)surface;

    if (wrapper == 0) {
        return 0;
    }
    if (IsBadReadPtr(wrapper, sizeof(V9X_DD_SURFACE_INT))) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.surface_int_rejected;
            v9x_hal->d3d_diagnostics.surface_int_last = (DWORD)wrapper;
            /* The site, and the set of sites. A capture that named only the
             * last one could be read as naming the only one, which is the
             * mistake this whole field exists to stop being repeated. */
            v9x_hal->d3d_diagnostics.surface_int_site = site;
            if (site != 0ul && site < 32ul) {
                v9x_hal->d3d_diagnostics.surface_int_sites |=
                    (1ul << site);
            }
        }
        return 0;
    }
    return wrapper->lpLcl;
}

/*
 * Which of the two 16 bpp layouts a pixel format describes, or neither.
 *
 * Both engines render into 16 bits and nothing else, so a format that is not
 * one of these two is refused by the caller rather than guessed at: a driver
 * that rendered 5:6:5 into a surface described as 5:5:5 would shift every
 * colour by a bit with every HRESULT reporting success - which is exactly what
 * happened on 2026-09-02 when only display-layout targets were classified and
 * an offscreen one fell through to a default.
 */
static int v9x_d3d_target_format_of(const V9X_DDPIXELFORMAT *format,
                                    DWORD *format_out)
{
    if (format == 0 || format_out == 0 ||
        format->dwSize != sizeof(V9X_DDPIXELFORMAT) ||
        (format->dwFlags & V9X_DDPF_RGB) == 0ul ||
        format->dwRGBBitCount != 16ul) {
        return 0;
    }
    if (format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_D3D_TARGET_FORMAT_RGB565;
        return 1;
    }
    if (format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_D3D_TARGET_FORMAT_XRGB1555;
        return 1;
    }
    return 0;
}

/*
 * Record why a depth surface was refused, and return the failure so the
 * caller can write "return v9x_d3d_depth_reject(REASON);" in place of a bare
 * "return 0;". Every arm of the depth validation below has its own reason,
 * because the whole point of the field is that one guest run should say which
 * check fired rather than only that one did.
 */
static int v9x_d3d_depth_reject(DWORD reason)
{
    if (v9x_hal != 0) {
        v9x_hal->d3d_diagnostics.depth_reject = reason;
    }
    return 0;
}

static int v9x_d3d_set_target(V9X_D3D_CONTEXT *context, void *surface,
                              void *zbuffer)
{
    V9X_DD_SURFACE_LCL *target =
        v9x_d3d_surface_lcl(surface, V9X_D3D_LCL_SITE_TARGET);
    V9X_DD_SURFACE_LCL *depth = zbuffer != 0
        ? v9x_d3d_surface_lcl(zbuffer, V9X_D3D_LCL_SITE_ZBUFFER) : 0;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    const V9X_D3D_ENGINE_LIMITS *limits;
    V9X_DD_SURFACE_GBL *global;
    DWORD offset;
    DWORD last_byte;
    DWORD pitch;
    DWORD width;
    DWORD height;
    DWORD depth_offset = 0ul;
    DWORD depth_pitch = 0ul;
    DWORD target_format = V9X_D3D_TARGET_FORMAT_RGB565;
    int primary;
    int display_layout;

    /*
     * Record the offer before anything can reject it, and key it off zbuffer
     * rather than depth: a non-null lpDDSZ that resolves to no lpLcl leaves
     * depth null, and that case must not read as "the runtime passed no depth
     * surface" - it is the one path on which the driver renders without depth
     * while every HRESULT reports success.
     */
    if (v9x_hal != 0 && zbuffer != 0) {
        ++v9x_hal->d3d_diagnostics.depth_offered;
        v9x_hal->d3d_diagnostics.depth_caps = depth != 0 ? depth->ddsCaps
                                                         : 0ul;
        v9x_hal->d3d_diagnostics.depth_offset = 0ul;
        v9x_hal->d3d_diagnostics.depth_pitch = 0ul;
        v9x_hal->d3d_diagnostics.depth_reject =
            depth != 0 ? V9X_D3D_ZREJECT_NONE : V9X_D3D_ZREJECT_NO_LCL;
    }

    if (ops == 0 || context == 0 || target == 0 || target->lpGbl == 0 ||
        (target->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    limits = ops->limits;
    global = target->lpGbl;
    offset = v9x_surface_offset(target);
    primary = (target->ddsCaps & V9X_DDSCAPS_PRIMARYSURFACE) != 0ul;
    display_layout = (target->ddsCaps &
        (V9X_DDSCAPS_PRIMARYSURFACE | V9X_DDSCAPS_BACKBUFFER)) != 0ul;
    pitch = (DWORD)global->lPitch;
    width = global->wWidth;
    height = global->wHeight;
    /* Low byte: 0x80 marks raw DDRAW metadata; bits 1:0 identify
     * offscreen/primary/backbuffer. The following event records the pitch
     * actually selected after display-layout normalization. */
    v9x_trace_push(V9X_TRACE_D3D_TARGET_LAYOUT,
                   ((pitch & 0xfffful) << 16) |
                   ((v9x_hal->fb.bits_per_pixel & 0xfful) << 8) | 0x80ul |
                   (primary ? 1ul : (display_layout ? 2ul : 0ul)));
    if (display_layout) {
        V9X_DDPIXELFORMAT *format = &v9x_hal->info.vmiData.ddpfDisplay;

        /* DDRAW's primary/flip-chain metadata has varied across the legacy
         * runtime paths. The scanout descriptor is authoritative for these
         * display-sized surfaces: using a stale surface pitch here creates
         * diagonal/striped S3D output and can walk beyond the page. */
        if ((primary && offset != 0ul) ||
            v9x_hal->fb.bits_per_pixel != limits->target_bits_per_pixel ||
            v9x_hal->info.vmiData.lDisplayPitch !=
                (LONG)v9x_hal->fb.pitch) {
            return 0;
        }
        /*
         * 5:6:5 or 5:5:5, and which one is recorded rather than assumed.
         *
         * This was a check for exactly 5:6:5 and nothing else. A 5:5:5 desktop
         * is the mode in which the S3D triangle engine's output lands in the
         * right channels, because that engine has no RGB565 destination
         * format at all - so refusing 5:5:5 here refused the only display mode
         * in which hardware Direct3D on this silicon is correct.
         */
        if (!v9x_d3d_target_format_of(format, &target_format)) {
            return 0;
        }
        pitch = v9x_hal->fb.pitch;
        width = v9x_hal->fb.width;
        height = v9x_hal->fb.height;
    } else {
        /*
         * An offscreen render target is in its own format when it carries one
         * and in the display's when it does not - DDRAW allocates ddpfSurface
         * only in the differing case, which DDRAWISURF_HASPIXELFORMAT reports,
         * so reading it unconditionally would read past the allocation.
         *
         * Classified for the same reason the display-layout branch is, and it
         * was not until 2026-09-02: the probe's 64x64 target took this branch,
         * kept a default of 5:6:5, and had 0xF800 written into a surface
         * everything else described as 5:5:5. A format that is neither layout
         * - a 32 bpp offscreen surface, say - is refused here, where before it
         * passed the size checks and was rasterized as 16 bits.
         */
        const V9X_DDPIXELFORMAT *format =
            (target->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
                ? &global->ddpfSurface
                : &v9x_hal->info.vmiData.ddpfDisplay;

        if (!v9x_d3d_target_format_of(format, &target_format)) {
            return 0;
        }
    }
    v9x_trace_push(V9X_TRACE_D3D_TARGET_LAYOUT,
                   ((pitch & 0xfffful) << 16) |
                   ((v9x_hal->fb.bits_per_pixel & 0xfful) << 8) |
                   (primary ? 1ul : (display_layout ? 2ul : 0ul)));
    if (offset == 0xfffffffful || (!display_layout && global->lPitch <= 0l) ||
        (pitch & (limits->target_pitch_align - 1ul)) != 0ul ||
        pitch > limits->target_pitch_max ||
        width == 0ul || width > pitch / 2ul || height == 0ul ||
        width > limits->target_dimension_max ||
        height > limits->target_dimension_max) {
        return 0;
    }
    last_byte = (height - 1ul) * pitch + width * 2ul;
    if (last_byte > v9x_hal->fb.vram_bytes ||
        offset > v9x_hal->fb.vram_bytes - last_byte) {
        return 0;
    }
    if (depth != 0) {
        DWORD depth_bytes = limits->depth_bits_per_pixel >> 3;
        DWORD depth_last_byte;
        DWORD depth_expected;

        /*
         * One arm per reason. This was a single disjunction; it is split
         * because a run that reaches here and refuses has to say which check
         * refused, and folding six conditions into one return makes the
         * cheapest question - is the surface in system memory, or merely the
         * wrong size - cost another guest round trip to answer.
         */
        if (depth->lpGbl == 0) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_NO_GBL);
        }
        if ((depth->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_NOT_ZBUFFER);
        }
        if ((depth->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_SYSTEM_MEMORY);
        }
        if (depth->lpGbl->lPitch <= 0l) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_PITCH);
        }
        if (depth->lpGbl->wWidth < width || depth->lpGbl->wHeight < height) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_DIMENSIONS);
        }
        depth_offset = v9x_surface_offset(depth);
        depth_pitch = (DWORD)depth->lpGbl->lPitch;
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.depth_offset = depth_offset;
            v9x_hal->d3d_diagnostics.depth_pitch = depth_pitch;
        }
        /*
         * The footprint is measured on the depth surface's own dimensions
         * rather than the render target's. The target is never larger - that
         * is checked just above - so the old form was not out of bounds, but
         * it was correct only because the hardware clip rectangle happens to
         * be programmed from the target. Bounding the surface by its own size
         * makes it safe by construction instead of by coincidence.
         */
        depth_last_byte =
            ((DWORD)depth->lpGbl->wHeight - 1ul) * depth_pitch +
            (DWORD)depth->lpGbl->wWidth * depth_bytes;
        /*
         * The engine drops the low three bits of the depth base address, so an
         * unaligned offset would have the chip addressing up to seven bytes
         * below what was validated here. dwZBufferAlign = 8 asks DirectDraw
         * for an aligned surface; it does not promise one.
         */
        if ((depth_offset & 7ul) != 0ul) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_UNALIGNED);
        }
        /*
         * And it must not overlap the visible framebuffer.
         *
         * Everything else here bounds the depth surface inside VRAM, which
         * includes the scanned-out region. Depth writes landing there are the
         * one failure in this feature that is destructive rather than merely
         * wrong - the screen fills with depth values - so the region is
         * excluded explicitly rather than left to the heap always allocating
         * above it.
         */
        if (depth_offset < v9x_hal->fb.visible_bytes) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_OVERLAPS_FB);
        }
        /*
         * The DDK ignores lPitch for depth and programs its own stride,
         * (width * bytes + 7) & ~7 (D3DDRV.C:71). If DirectDraw's pitch
         * disagrees with that, the driver and the chip hold different ideas of
         * where each depth row starts and nothing downstream can tell which is
         * right - so refuse rather than pick one.
         */
        depth_expected =
            (((DWORD)depth->lpGbl->wWidth * depth_bytes) + 7ul) & ~7ul;
        /* An engine that programs the surface's own pitch may have one wider
         * than the packed row (depth_pitch_own); it may never be narrower. */
        if (limits->depth_pitch_own != 0ul
                ? depth_pitch < depth_expected
                : depth_pitch != depth_expected) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_PITCH);
        }
        if ((depth_pitch & (limits->target_pitch_align - 1ul)) != 0ul ||
            depth_pitch > limits->target_pitch_max) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_PITCH);
        }
        if (depth_offset == 0xfffffffful ||
            depth_last_byte > v9x_hal->fb.vram_bytes ||
            depth_offset > v9x_hal->fb.vram_bytes - depth_last_byte) {
            return v9x_d3d_depth_reject(V9X_D3D_ZREJECT_BOUNDS);
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.depth_accepted;
            v9x_hal->d3d_diagnostics.depth_reject = V9X_D3D_ZREJECT_ACCEPTED;
        }
    }
    context->target = target;
    context->zbuffer = depth;
    context->target_offset = offset;
    context->target_format = target_format;
    context->pitch = pitch;
    context->width = width;
    context->height = height;
    context->depth_offset = depth_offset;
    context->depth_pitch = depth_pitch;
    /*
     * Attaching a depth surface turns depth testing on; detaching turns it
     * off. That is the DDK's behaviour (D3DCB2.C:57-66), and it is what makes
     * a title that attaches a Z buffer and never sends ZENABLE render with
     * depth rather than without it. The render states own these afterwards.
     */
    context->z_enable = depth != 0 ? 1ul : 0ul;
    context->z_write = depth != 0 ? 1ul : 0ul;
    return 1;
}

DWORD __stdcall V9xD3dContextCreate(V9X_D3DHAL_CONTEXTCREATEDATA *data)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    DWORD index;
    V9X_D3D_CONTEXT *context;

    v9x_trace_enter(V9X_TRACE_D3D_CTXCREATE,
                    data != 0 ? data->dwPID : 0ul);
    v9x_d3d_dp2_log("CTX create", data != 0 ? data->dwPID : 0ul, 0ul);
    if (ops == 0 || data == 0 || v9x_hal == 0 || data->lpDDS == 0 ||
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        v9x_hal->fb.bits_per_pixel != ops->limits->target_bits_per_pixel) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.context_rejects;
        }
        v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        context = &v9x_d3d_contexts[index];
        if (context->active == 0ul) {
            if (!v9x_d3d_set_target(context, data->lpDDS, data->lpDDSZ)) {
                data->ddrval = 0x80070057ul;
                ++v9x_hal->d3d_diagnostics.context_rejects;
                v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
                return V9X_DDHAL_DRIVER_HANDLED;
            }
            context->pid = data->dwPID;
            v9x_d3d_note_client(data->dwPID);
            v9x_d3d_note_process(V9X_DIAG_PROCESS_D3D);
            if (v9x_hal->d3d_diagnostics.uptime_first_d3d == 0ul) {
                v9x_hal->d3d_diagnostics.uptime_first_d3d = GetTickCount();
            }
            context->specular_enable = 0ul;
            context->fog_enable = 0ul;
            context->fog_color = 0ul;
            context->alpha_blend_enable = 0ul;
            context->src_blend = V9X_D3DBLEND_SRCALPHA;
            context->dest_blend = V9X_D3DBLEND_INVSRCALPHA;
            context->texture_handle = 0ul;
            context->texture_min = V9X_D3DFILTER_NEAREST;
            context->texture_mag = V9X_D3DFILTER_NEAREST;
            context->texture_blend = V9X_D3DTBLEND_MODULATE;
            context->texture_wrap = 1ul;
            /* Direct3D's own default for D3DRENDERSTATE_TEXTUREADDRESS, so an
             * application that never sets it tiles rather than stretching. */
            context->texture_address = V9X_D3DTADDRESS_WRAP;
            context->texture_border = 0ul;
            context->wrap_u = 0ul;
            context->wrap_v = 0ul;
            context->shade_mode = V9X_D3DSHADE_GOURAUD;
            /* Direct3D's own default: back faces run counterclockwise. */
            context->cull_mode = V9X_R3D_CULL_CCW;
            /*
             * Only z_func. depth_offset, depth_pitch, z_enable and z_write are
             * owned by v9x_d3d_set_target, which ran above - resetting them
             * here would discard the decision it just made about the attached
             * depth surface. D3DCMP_LESS is the DDK's default (D3DCTXT.C:351).
             */
            context->z_func = V9X_D3DCMP_LESS;
            context->alpha_test_enable = 0ul;
            context->alpha_func = V9X_D3DCMP_ALWAYS;
            context->alpha_ref = 0ul;
            /* Direct3D's stage-0 defaults (d3dtypes.h): modulate the
             * texture by the diffuse colour, take alpha from the texture,
             * point sampling, no mip-mapping. */
            context->stage_texture = 0ul;
            context->stage_color_op = V9X_D3DTOP_MODULATE;
            context->stage_color_arg1 = V9X_D3DTA_TEXTURE;
            context->stage_color_arg2 = V9X_D3DTA_CURRENT;
            context->stage_alpha_op = V9X_D3DTOP_SELECTARG1;
            context->stage_min = V9X_D3DTFG_POINT;
            context->stage_mip = V9X_D3DTFP_NONE;
            context->active = 1ul;
            data->dwhContext = (DWORD)context;
            data->ddrval = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.context_creates;
            v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    data->ddrval = 0x8007000eul;
    ++v9x_hal->d3d_diagnostics.context_rejects;
    v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dContextDestroy(V9X_D3DHAL_CONTEXTDESTROYDATA *data)
{
    V9X_D3D_CONTEXT *context;

    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    v9x_trace_enter(V9X_TRACE_D3D_CTXDESTROY,
                    data != 0 ? data->dwhContext : 0ul);
    if (context == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.context_rejects;
        }
        v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROY, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    /*
     * The context's textures go with it, and that is right even though the
     * runtime destroys a context to switch render target: it retires its own
     * texture handles at the same moment and re-creates them on the next
     * GetHandle, so a record that outlived the context would be a record no
     * caller can name
     * (docs\issues\2026-09-10-a-target-switch-loses-every-texture.md).
     */
    v9x_d3d_textures_destroy_context(data->dwhContext);
    context->active = 0ul;
    context->pid = 0ul;
    context->target = 0;
    context->zbuffer = 0;
    context->target_offset = 0ul;
    context->target_format = V9X_D3D_TARGET_FORMAT_RGB565;
    context->pitch = 0ul;
    context->width = 0ul;
    context->height = 0ul;
    context->specular_enable = 0ul;
    context->fog_enable = 0ul;
    context->fog_color = 0ul;
    context->alpha_blend_enable = 0ul;
    context->src_blend = 0ul;
    context->dest_blend = 0ul;
    context->texture_handle = 0ul;
    context->texture_min = 0ul;
    context->texture_mag = 0ul;
    context->texture_blend = 0ul;
    context->texture_wrap = 0ul;
    context->texture_address = V9X_D3DTADDRESS_WRAP;
    context->texture_border = 0ul;
    context->wrap_u = 0ul;
    context->wrap_v = 0ul;
    context->depth_offset = 0ul;
    context->depth_pitch = 0ul;
    context->z_enable = 0ul;
    context->z_write = 0ul;
    context->z_func = 0ul;
    context->alpha_test_enable = 0ul;
    context->alpha_func = 0ul;
    context->alpha_ref = 0ul;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.context_destroys;
    v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROY, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dContextDestroyAll(
    V9X_D3DHAL_CONTEXTDESTROYALLDATA *data)
{
    DWORD index;

    v9x_trace_enter(V9X_TRACE_D3D_CTXDESTROYALL,
                    data != 0 ? data->dwPID : 0ul);
    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        if (v9x_d3d_contexts[index].active != 0ul &&
            v9x_d3d_contexts[index].pid == data->dwPID) {
            v9x_d3d_textures_destroy_context(
                (DWORD)&v9x_d3d_contexts[index]);
            v9x_d3d_contexts[index].active = 0ul;
            v9x_d3d_contexts[index].pid = 0ul;
            v9x_d3d_contexts[index].target = 0;
            v9x_d3d_contexts[index].zbuffer = 0;
            v9x_d3d_contexts[index].target_offset = 0ul;
            v9x_d3d_contexts[index].pitch = 0ul;
            v9x_d3d_contexts[index].width = 0ul;
            v9x_d3d_contexts[index].height = 0ul;
            v9x_d3d_contexts[index].specular_enable = 0ul;
            v9x_d3d_contexts[index].fog_enable = 0ul;
            v9x_d3d_contexts[index].fog_color = 0ul;
            v9x_d3d_contexts[index].alpha_blend_enable = 0ul;
            v9x_d3d_contexts[index].src_blend = 0ul;
            v9x_d3d_contexts[index].dest_blend = 0ul;
            v9x_d3d_contexts[index].texture_handle = 0ul;
            v9x_d3d_contexts[index].texture_min = 0ul;
            v9x_d3d_contexts[index].texture_mag = 0ul;
            v9x_d3d_contexts[index].texture_blend = 0ul;
            v9x_d3d_contexts[index].texture_wrap = 0ul;
            v9x_d3d_contexts[index].texture_address = V9X_D3DTADDRESS_WRAP;
            v9x_d3d_contexts[index].texture_border = 0ul;
            v9x_d3d_contexts[index].wrap_u = 0ul;
            v9x_d3d_contexts[index].wrap_v = 0ul;
            v9x_d3d_contexts[index].depth_offset = 0ul;
            v9x_d3d_contexts[index].depth_pitch = 0ul;
            v9x_d3d_contexts[index].z_enable = 0ul;
            v9x_d3d_contexts[index].z_write = 0ul;
            v9x_d3d_contexts[index].z_func = 0ul;
            v9x_d3d_contexts[index].alpha_test_enable = 0ul;
            v9x_d3d_contexts[index].alpha_func = 0ul;
            v9x_d3d_contexts[index].alpha_ref = 0ul;
        }
    }
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.context_destroy_alls;
    v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROYALL, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureCreate(V9X_D3DHAL_TEXTURECREATEDATA *data)
{
    DWORD index;
    DWORD tried;
    V9X_DD_SURFACE_LCL *lcl;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTURECREATE,
                    data != 0 ? data->dwhContext : 0ul);
    if (data == 0 || data->lpDDS == 0 ||
        v9x_d3d_context_from_handle(data->dwhContext) == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    /* The only read of the wrapper in the texture's lifetime: the runtime is
     * handing it over, so it is certainly alive now. Every later consumer -
     * the sampler lookup, the teardown scan, the swap's colour-key touch -
     * uses this value. A wrapper that resolves to nothing still gets a
     * handle, as it did before: the engine treats a null surface as not
     * sampleable, and the refusal is counted at the site either way. */
    lcl = v9x_d3d_surface_lcl(data->lpDDS, V9X_D3D_LCL_SITE_TEXTURE_CREATE);
    /* Where the runtime put this texture, recorded at the one moment the
     * question has a clean answer. See the diagnostics comment. */
    if (lcl != 0 && v9x_hal != 0) {
        v9x_hal->d3d_diagnostics.texture_create_last_caps = lcl->ddsCaps;
        if ((lcl->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
            ++v9x_hal->d3d_diagnostics.texture_create_sysmem;
        }
    }
    /* From after the last slot taken: the table is large, and the slots
     * before it are usually still live. */
    for (tried = 0ul; tried < V9X_D3D_TEXTURE_COUNT; ++tried) {
        index = (v9x_d3d_texture_next + tried) % V9X_D3D_TEXTURE_COUNT;
        if (v9x_d3d_textures[index].active == 0ul) {
            v9x_d3d_textures[index].active = 1ul;
            v9x_d3d_textures[index].context = data->dwhContext;
            v9x_d3d_textures[index].surface = data->lpDDS;
            v9x_d3d_textures[index].lcl = lcl;
            v9x_d3d_texture_next = (index + 1ul) % V9X_D3D_TEXTURE_COUNT;
            data->dwHandle = (DWORD)&v9x_d3d_textures[index];
            data->ddrval = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.texture_creates;
            v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    /* Full. The runtime then binds handle 0 for this texture and the
     * application's draws go untextured, so it is counted. */
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.texture_table_full;
    }
    data->ddrval = 0x8007000eul;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureDestroy(V9X_D3DHAL_TEXTUREDESTROYDATA *data)
{
    V9X_D3D_TEXTURE *texture;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTUREDESTROY,
                    data != 0 ? data->dwHandle : 0ul);
    texture = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle, data->dwhContext) : 0;
    if (texture == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTUREDESTROY, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    texture->active = 0ul;
    texture->context = 0ul;
    texture->surface = 0;
    texture->lcl = 0;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_destroys;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTUREDESTROY, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureSwap(V9X_D3DHAL_TEXTURESWAPDATA *data)
{
    V9X_D3D_TEXTURE *first;
    V9X_D3D_TEXTURE *second;
    void *surface;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTURESWAP,
                    data != 0 ? data->dwHandle1 : 0ul);
    first = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle1, data->dwhContext) : 0;
    second = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle2, data->dwhContext) : 0;
    if (first == 0 || second == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTURESWAP, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    surface = first->surface;
    first->surface = second->surface;
    second->surface = surface;
    /* The resolved local half moves with its wrapper. Leaving it behind
     * would pair each slot with the other one's surface, which is a texture
     * sampled from the wrong memory and no error anywhere. */
    {
        V9X_DD_SURFACE_LCL *lcl = first->lcl;

        first->lcl = second->lcl;
        second->lcl = lcl;
    }
    /* A swap is how a texture manager gets new texels into an old slot:
     * whichever keyed surface is involved must be rewritten before use.
     * Both values were resolved at creation, so neither wrapper is read. */
    v9x_d3d_color_key_touch(first->lcl);
    v9x_d3d_color_key_touch(second->lcl);
    v9x_d3d_alpha_mask_touch(first->lcl);
    v9x_d3d_alpha_mask_touch(second->lcl);
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_swaps;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTURESWAP, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureGetSurf(V9X_D3DHAL_TEXTUREGETSURFDATA *data)
{
    V9X_D3D_TEXTURE *texture;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTUREGETSURF,
                    data != 0 ? data->dwHandle : 0ul);
    texture = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle, data->dwhContext) : 0;
    if (texture == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTUREGETSURF, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    data->lpDDS = (DWORD)texture->surface;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_get_surfs;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTUREGETSURF, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * One render state, applied.
 *
 * Lifted out of V9xD3dRenderState on 2026-09-20 because it has a second
 * caller. DrawPrimitives carries its state changes INSIDE the command buffer
 * as (state, value) pairs, and this driver stepped over them with a pointer
 * addition - so an application that sets state only that way set none at
 * all. 3DMark99 is such an application: it ran the whole benchmark on the
 * ViRGE guest with 24,033 primitive calls, zero RenderState calls and every
 * context still holding the NEAREST filter its creation sets, while the caps
 * told it bilinear and trilinear were available.
 *
 * One switch serves both paths so the two cannot drift, which is the whole
 * reason this is a function rather than a copy.
 */
static void v9x_d3d_apply_state(V9X_D3D_CONTEXT *context, DWORD type,
                                DWORD argument)
{
    switch (type) {
    case V9X_D3DRENDERSTATE_SHADEMODE:
        context->shade_mode = argument;
        break;
    case V9X_D3DRENDERSTATE_CULLMODE:
        context->cull_mode = argument;
        break;
    case V9X_D3DRENDERSTATE_TEXTUREHANDLE:
        context->texture_handle = argument;
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.texture_handle_sets;
            v9x_hal->d3d_diagnostics.texture_handle_last = argument;
        }
        break;
    case V9X_D3DRENDERSTATE_TEXTUREPERSPECTIVE:
        /* Perspective setup is added after the affine texture gate. */
        break;
    case V9X_D3DRENDERSTATE_WRAPU:
    case V9X_D3DRENDERSTATE_WRAPV:
        context->texture_wrap = argument != 0ul;
        if (type == V9X_D3DRENDERSTATE_WRAPU) {
            context->wrap_u = argument != 0ul;
        } else {
            context->wrap_v = argument != 0ul;
        }
        break;
    case V9X_D3DRENDERSTATE_TEXTUREMAG:
        context->texture_mag = argument;
        /* One bit per value, so the capture says which filters the
         * application asked for rather than how often. The values
         * run 1..7; anything outside that lands in bit 0. */
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.filter_mag_seen |=
                argument < 32ul ? (1ul << argument) : 1ul;
        }
        break;
    case V9X_D3DRENDERSTATE_TEXTUREMIN:
        context->texture_min = argument;
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.filter_min_seen |=
                argument < 32ul ? (1ul << argument) : 1ul;
        }
        break;
    case V9X_D3DRENDERSTATE_TEXTUREMAPBLEND:
        context->texture_blend = argument;
        break;
    case V9X_D3DRENDERSTATE_TEXTUREADDRESS:
    case V9X_D3DRENDERSTATE_TEXTUREADDRESSU:
    case V9X_D3DRENDERSTATE_TEXTUREADDRESSV:
        context->texture_address = argument;
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.address_seen[
                type == V9X_D3DRENDERSTATE_TEXTUREADDRESS ? 0 :
                type == V9X_D3DRENDERSTATE_TEXTUREADDRESSU ? 1 : 2] |=
                argument < 31ul ? (1ul << argument) : 0x80000000ul;
        }
        break;
    case V9X_D3DRENDERSTATE_BORDERCOLOR:
        context->texture_border = argument;
        break;
    case V9X_D3DRENDERSTATE_SRCBLEND:
        context->src_blend = argument;
        break;
    case V9X_D3DRENDERSTATE_DESTBLEND:
        context->dest_blend = argument;
        break;
    case V9X_D3DRENDERSTATE_ALPHABLENDENABLE:
        context->alpha_blend_enable = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_COLORKEYENABLE:
        context->color_key_enable = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_V9X_ALPHAFORCE:
        /*
         * An instrument riding on a real render state; see the
         * header. Only the magic argument is taken, so a stipple
         * pattern - which is what this state is - leaves the engine's
         * choice alone however it is written.
         */
        if ((argument & V9X_D3D_ALPHAFORCE_MASK) ==
            V9X_D3D_ALPHAFORCE_MAGIC) {
            context->alpha_force =
        argument & ~V9X_D3D_ALPHAFORCE_MASK;
        } else {
            context->alpha_force = V9X_D3D_ALPHAFORCE_ENGINE;
        }
        break;
    case V9X_D3DRENDERSTATE_FOGENABLE:
        context->fog_enable = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_SPECULARENABLE:
        context->specular_enable = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_FOGCOLOR:
        context->fog_color = argument;
        break;
    case V9X_D3DRENDERSTATE_ZENABLE:
        /* DirectX 5 allows D3DZB_USEW (2) here. This driver publishes
         * no W-buffer capability, so anything non-zero is plain Z. */
        context->z_enable = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_ZWRITEENABLE:
        context->z_write = argument != 0ul;
        break;
    case V9X_D3DRENDERSTATE_ZFUNC:
        /* Not validated here. The engine's mapping table has a
         * default arm, which is where an unknown function is decided
         * - and the safe default is not the one a zeroed field would
         * give. */
        context->z_func = argument;
        break;
    case V9X_D3DRENDERSTATE_ALPHATESTENABLE:
        context->alpha_test_enable = argument != 0ul;
        if (argument != 0ul && v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.alpha_test_sets;
        }
        break;
    case V9X_D3DRENDERSTATE_ALPHAFUNC:
        context->alpha_func = argument;
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.alpha_test_func_seen |=
                argument < 32ul ? (1ul << argument) : 1ul;
        }
        break;
    case V9X_D3DRENDERSTATE_ALPHAREF:
        context->alpha_ref = argument;
        if (v9x_hal != 0) {
            v9x_hal->d3d_diagnostics.alpha_test_ref_last = argument;
        }
        break;
    default:
        break;
    }
}

DWORD __stdcall V9xD3dRenderState(V9X_D3DHAL_RENDERSTATEDATA *data)
{
    V9X_D3D_CONTEXT *context;
    V9X_DD_SURFACE_LCL *exe;
    V9X_D3DSTATE *states;
    DWORD index;

    v9x_trace_enter(V9X_TRACE_D3D_RENDERSTATE,
                    data != 0 ? data->dwCount : 0ul);
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_state_calls;
    }
    context = data != 0
        ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    exe = data != 0
        ? v9x_d3d_surface_lcl(data->lpExeBuf, V9X_D3D_LCL_SITE_ONEPRIM_EXE)
        : 0;
    /*
     * Everything below applies or nothing does, and until intel94 nothing
     * counted the nothing. A call whose context or execute buffer does not
     * resolve returns handled and applies no state at all, so a texture
     * handle in it is a binding the application believes it made and the
     * driver never saw.
     *
     * The LENGTH used to be one of those cases and is not any more. intel96
     * measured 3DMark99 sending a block of 276 states and Final Reality one
     * of 81, and a cap of 64 threw every state in them away - not the
     * excess, the whole block. The cap was this loop's, not the interface's.
     *
     * What replaces it is a bound that means something. The states must lie
     * inside the execute buffer, whose size DirectDraw reports in
     * dwBlockSizeX for a linear surface; that field's meaning on this path
     * is not established by measurement, so it is used only to make the
     * bound TIGHTER and never to widen it, and the value seen is recorded so
     * a capture can settle what it holds. V9X_D3D_STATE_MAX is the backstop
     * for a zero or absurd report, and is larger than anything measured.
     */
    if (!(context != 0 && exe != 0 && exe->lpGbl != 0 &&
          exe->lpGbl->fpVidMem != 0ul) &&
        v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_state_dropped;
        /* Preserve the failed precondition and the affected context without
         * dereferencing a rejected state block. The retained handle is not
         * evidence of what the application attempted to bind. */
        v9x_hal->d3d_diagnostics.state_drop_reason = context == 0
            ? V9X_D3D_STATE_DROP_CONTEXT : exe == 0
            ? V9X_D3D_STATE_DROP_SURFACE : exe->lpGbl == 0
            ? V9X_D3D_STATE_DROP_GLOBAL : V9X_D3D_STATE_DROP_MEMORY;
        v9x_hal->d3d_diagnostics.state_drop_context =
            data != 0 ? data->dwhContext : 0ul;
        v9x_hal->d3d_diagnostics.state_drop_count =
            data != 0 ? data->dwCount : 0ul;
        v9x_hal->d3d_diagnostics.state_drop_offset =
            data != 0 ? data->dwOffset : 0ul;
        v9x_hal->d3d_diagnostics.state_drop_handle =
            context != 0 ? context->texture_handle : 0ul;
    }

    if (context != 0 && exe != 0 && exe->lpGbl != 0 &&
        exe->lpGbl->fpVidMem != 0ul) {
        DWORD applied = data->dwCount;
        DWORD room;

        if (v9x_hal != 0) {
            if (data->dwCount > v9x_hal->d3d_diagnostics.state_max_count) {
                v9x_hal->d3d_diagnostics.state_max_count = data->dwCount;
            }
            v9x_hal->d3d_diagnostics.state_exe_bytes_last =
                exe->lpGbl->dwBlockSizeX;
        }

        if (applied > (DWORD)V9X_D3D_STATE_MAX) {
            applied = (DWORD)V9X_D3D_STATE_MAX;
        }

        /*
         * The buffer's own extent, when it reports one. An offset at or past
         * the end leaves nothing to read and applies nothing, which is the
         * one length case that still declines the block entirely.
         */
        if (exe->lpGbl->dwBlockSizeX > data->dwOffset) {
            room = (exe->lpGbl->dwBlockSizeX - data->dwOffset) /
                   (DWORD)sizeof(V9X_D3DSTATE);
            if (applied > room) {
                applied = room;
            }
        } else if (exe->lpGbl->dwBlockSizeX != 0ul) {
            applied = 0ul;
        }

        if (applied != data->dwCount && v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.state_clamped;
            v9x_hal->d3d_diagnostics.state_clamped_count = data->dwCount;
        }

        states = (V9X_D3DSTATE *)(exe->lpGbl->fpVidMem + data->dwOffset);
        for (index = 0ul; index < applied; ++index) {
            v9x_d3d_apply_state(context, states[index].type,
                                states[index].argument);
        }
    }
    if (data != 0) {
        data->ddrval = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_D3D_RENDERSTATE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

static BYTE v9x_d3d_saturating_add_byte(BYTE first, BYTE second)
{
    WORD sum = (WORD)first + (WORD)second;

    return sum > 255u ? 255u : (BYTE)sum;
}

static BYTE v9x_d3d_fog_byte(BYTE color, BYTE fog, BYTE factor)
{
    return (BYTE)(((DWORD)color * factor +
                   (DWORD)fog * (255u - factor) + 127ul) / 255ul);
}

/*
 * Flat shading, done here for every engine.
 *
 * Direct3D defines D3DSHADE_FLAT as the FIRST vertex's colour across the
 * triangle. Neither engine's flat-shade control is programmed - the Intel
 * provoking-vertex state is unset and unmeasured (docs\issues\2026-09-17-
 * flat-shading-is-claimed-and-the-provoking-vertex-is-not-programmed.md) -
 * and none needs to be: with the colour, alpha and specular copied to all
 * three vertices, a Gouraud interpolator produces the flat result exactly.
 * After apply_vertex_color, so specular and fog are folded into the copied
 * value once rather than three times. Fans and strips reach here as lists
 * one triangle at a time, so "first" is the first of each triangle as the
 * runtime handed it over.
 */
static void v9x_d3d_apply_flat_shading(const V9X_D3D_CONTEXT *context,
                                       V9X_D3DTLVERTEX *source)
{
    if (context->shade_mode != V9X_D3DSHADE_FLAT) {
        return;
    }
    source[1].color = source[0].color;
    source[2].color = source[0].color;
    source[1].specular = source[0].specular;
    source[2].specular = source[0].specular;
}

static void v9x_d3d_apply_vertex_color(const V9X_D3D_CONTEXT *context,
                                       V9X_D3DTLVERTEX *vertex)
{
    DWORD color = vertex->color;
    BYTE alpha = (BYTE)(color >> 24);
    BYTE red = (BYTE)(color >> 16);
    BYTE green = (BYTE)(color >> 8);
    BYTE blue = (BYTE)color;

    if (context->specular_enable != 0ul) {
        red = v9x_d3d_saturating_add_byte(
            red, (BYTE)(vertex->specular >> 16));
        green = v9x_d3d_saturating_add_byte(
            green, (BYTE)(vertex->specular >> 8));
        blue = v9x_d3d_saturating_add_byte(blue, (BYTE)vertex->specular);
    }
    if (context->fog_enable != 0ul) {
        BYTE factor = (BYTE)(vertex->specular >> 24);

        red = v9x_d3d_fog_byte(red, (BYTE)(context->fog_color >> 16),
                               factor);
        green = v9x_d3d_fog_byte(green, (BYTE)(context->fog_color >> 8),
                                 factor);
        blue = v9x_d3d_fog_byte(blue, (BYTE)context->fog_color, factor);
    }
    vertex->color = ((DWORD)alpha << 24) | ((DWORD)red << 16) |
                    ((DWORD)green << 8) | (DWORD)blue;
}

#define V9X_D3DOP_TRIANGLE             3u
#define V9X_D3DOP_EXIT                11u
#define V9X_D3DHAL_EXECUTE_OVERRIDE    1ul
#define V9X_D3DHAL_EXECUTE_UNHANDLED   0x00000211ul

DWORD __stdcall V9xD3dExecute(V9X_D3DHAL_EXECUTEDATA *data)
{
    V9X_DD_SURFACE_LCL *exe;
    V9X_D3DINSTRUCTION *instruction;
    BYTE *base;
    DWORD offset;
    DWORD end;
    int one_instruction;

    v9x_trace_enter(V9X_TRACE_D3D_EXECUTE,
                    data != 0 ? data->dwFlags : 0ul);
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.execute_calls;
    }
    exe = data != 0
        ? v9x_d3d_surface_lcl(data->lpExeBuf, V9X_D3D_LCL_SITE_PRIMS_EXE)
        : 0;
    if (data == 0 || v9x_d3d_context_from_handle(data->dwhContext) == 0 ||
        exe == 0 || exe->lpGbl == 0 || exe->lpGbl->fpVidMem == 0ul ||
        data->deExData.dwInstructionLength > 0x00100000ul) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    base = (BYTE *)exe->lpGbl->fpVidMem;
    one_instruction = (data->dwFlags & V9X_D3DHAL_EXECUTE_OVERRIDE) != 0ul;
    offset = one_instruction ? data->dwOffset
                             : data->deExData.dwInstructionOffset +
                               data->dwOffset;
    end = data->deExData.dwInstructionOffset +
          data->deExData.dwInstructionLength;
    for (;;) {
        DWORD bytes;

        if (!one_instruction && (offset > end ||
            end - offset < sizeof(V9X_D3DINSTRUCTION))) {
            data->ddrval = 0x80070057ul;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        instruction = one_instruction ? &data->diInstruction
                                      : (V9X_D3DINSTRUCTION *)(base + offset);
        bytes = (DWORD)instruction->bSize * (DWORD)instruction->wCount;
        if (!one_instruction && bytes > end - offset -
            sizeof(V9X_D3DINSTRUCTION)) {
            data->ddrval = 0x80070057ul;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (instruction->bOpcode == V9X_D3DOP_EXIT) {
            data->ddrval = V9X_DD_OK;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (instruction->bOpcode == V9X_D3DOP_TRIANGLE) {
            V9X_D3DHAL_RENDERPRIMITIVEDATA primitive;

            primitive.dwhContext = data->dwhContext;
            primitive.dwOffset = one_instruction ? data->dwOffset
                : offset + sizeof(V9X_D3DINSTRUCTION);
            primitive.dwStatus = data->dwStatus;
            primitive.lpExeBuf = data->lpExeBuf;
            primitive.dwTLOffset = data->lpTLBuf != 0
                ? 0ul : data->deExData.dwVertexOffset;
            primitive.lpTLBuf = data->lpTLBuf != 0
                ? data->lpTLBuf : data->lpExeBuf;
            primitive.diInstruction = *instruction;
            primitive.ddrval = V9X_DD_OK;
            (void)V9xD3dRenderPrimitive(&primitive);
            if (primitive.ddrval != V9X_DD_OK) {
                data->ddrval = primitive.ddrval;
                v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
                return V9X_DDHAL_DRIVER_HANDLED;
            }
        } else {
            data->dwOffset = one_instruction ? data->dwOffset
                : offset - data->deExData.dwInstructionOffset;
            data->ddrval = V9X_D3DHAL_EXECUTE_UNHANDLED;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return one_instruction ? V9X_DDHAL_DRIVER_NOTHANDLED
                                   : V9X_DDHAL_DRIVER_HANDLED;
        }
        if (one_instruction) {
            data->ddrval = V9X_DD_OK;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        offset += sizeof(V9X_D3DINSTRUCTION) + bytes;
    }
}

DWORD __stdcall V9xD3dExecuteClipped(
    V9X_D3DHAL_EXECUTECLIPPEDDATA *data)
{
    V9X_D3DHAL_EXECUTEDATA execute;
    DWORD handled;

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    execute.dwhContext = data->dwhContext;
    execute.dwOffset = data->dwOffset;
    execute.dwFlags = data->dwFlags;
    execute.dwStatus = data->dwStatus;
    execute.deExData = data->deExData;
    execute.lpExeBuf = data->lpExeBuf;
    execute.lpTLBuf = data->lpTLBuf;
    execute.diInstruction = data->diInstruction;
    execute.ddrval = data->ddrval;
    handled = V9xD3dExecute(&execute);
    data->dwOffset = execute.dwOffset;
    data->dwStatus = execute.dwStatus;
    data->ddrval = execute.ddrval;
    return handled;
}

static DWORD v9x_d3d_render_primitive_body(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data);

/* Timed as V9X_TIME_D3D_CALLS; the work is in the body. */
DWORD __stdcall V9xD3dRenderPrimitive(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data)
{
    DWORD started = V9X_TIME_BEGIN();
    DWORD result;

    v9x_win16_sample(V9X_WIN16_SITE_D3D_RENDERPRIM);
    result = v9x_d3d_render_primitive_body(data);

    V9X_TIME_END(V9X_TIME_D3D_CALLS, started);
    return result;
}

static DWORD v9x_d3d_render_primitive_body(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    V9X_DD_SURFACE_LCL *exe;
    V9X_DD_SURFACE_LCL *tl;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    const V9X_D3DTRIANGLE *triangles;
    const V9X_D3DTLVERTEX *vertices;
    V9X_D3DTLVERTEX fan_list[V9X_D3D_MAX_FAN_TRIANGLES * 3u];
    DWORD fan_triangles;
    DWORD index;
    int ok = 0;
    int served = 0;

    v9x_trace_enter(V9X_TRACE_D3D_RENDERPRIM,
                    data != 0
                        ? (((DWORD)data->diInstruction.bOpcode << 24) |
                           ((DWORD)data->diInstruction.bSize << 16) |
                           (DWORD)data->diInstruction.wCount)
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    exe = data != 0
        ? v9x_d3d_surface_lcl(data->lpExeBuf, V9X_D3D_LCL_SITE_RENDERPRIM_EXE)
        : 0;
    tl = data != 0
        ? v9x_d3d_surface_lcl(data->lpTLBuf,
                              V9X_D3D_LCL_SITE_RENDERPRIM_TL)
        : 0;
    if (ops != 0 && context != 0 && exe != 0 && exe->lpGbl != 0 && tl != 0 &&
        tl->lpGbl != 0 && ops->ready() &&
        data->diInstruction.bOpcode == 3u &&
        data->diInstruction.bSize >= sizeof(V9X_D3DTRIANGLE) &&
        data->diInstruction.wCount != 0u) {
        ok = 1;
        served = 1;
    } else if (data != 0) {
        /* A refused instruction is a whole mesh not drawn, and until now it
         * left nothing behind but an HRESULT the application ignores. The
         * detail word is the same packing the enter event uses, so the ring
         * shows what was refused. */
        v9x_trace_push(V9X_TRACE_D3D_PRIMREJECT,
                       0x80000000ul |
                       ((DWORD)data->diInstruction.bOpcode << 24) |
                       ((DWORD)data->diInstruction.bSize << 16) |
                       (DWORD)data->diInstruction.wCount);
    }
    if (ok) {
        triangles = (const V9X_D3DTRIANGLE *)
            (exe->lpGbl->fpVidMem + data->dwOffset);
        vertices = (const V9X_D3DTLVERTEX *)
            (tl->lpGbl->fpVidMem + data->dwTLOffset);
        for (index = 0ul; index < data->diInstruction.wCount; ++index) {
            const V9X_D3DTRIANGLE *triangle =
                (const V9X_D3DTRIANGLE *)
                ((const BYTE *)triangles +
                 index * data->diInstruction.bSize);
            V9X_D3DTLVERTEX source[3];
            V9X_D3DTLVERTEX clipped[8];
            int clipped_count;
            int fan;

            source[0] = vertices[triangle->v1];
            source[1] = vertices[triangle->v2];
            source[2] = vertices[triangle->v3];
            /* Once per call, after the buffers have been read and before
             * anything is clipped or drawn. intel53-55 show this entry point
             * entered and never left; with this in the ring the next capture
             * says which side of the vertex read it died on. */
            if (index == 0ul) {
                v9x_trace_push(V9X_TRACE_D3D_RENDERLOOP,
                               ((DWORD)triangle->v1 << 16) |
                               (DWORD)data->diInstruction.wCount);
            }
            /* A back face is dropped before any work is spent on it. */
            if (v9x_d3d_triangle_culled(context, source)) {
                continue;
            }
            v9x_d3d_apply_vertex_color(context, &source[0]);
            v9x_d3d_apply_vertex_color(context, &source[1]);
            v9x_d3d_apply_vertex_color(context, &source[2]);
            v9x_d3d_apply_flat_shading(context, source);
            clipped_count = v9x_d3d_clip_triangle(context, source, clipped);
            if (clipped_count < 0) {
                v9x_trace_push(V9X_TRACE_D3D_PRIMREJECT,
                               0x20000000ul | index);
                ok = 0;
                break;
            }
            /* Materialise the fan as a triangle list so the engine sees a
             * run rather than one triangle at a time. The clipper emits at
             * most eight vertices, so the fan is at most six triangles and
             * fan_list is sized to that. */
            fan_triangles = 0ul;
            for (fan = 1; fan + 1 < clipped_count; ++fan) {
                fan_list[fan_triangles * 3ul] = clipped[0];
                fan_list[fan_triangles * 3ul + 1ul] = clipped[fan];
                fan_list[fan_triangles * 3ul + 2ul] = clipped[fan + 1];
                ++fan_triangles;
            }
            if (fan_triangles != 0ul &&
                !v9x_d3d_draw_batch(ops, context, fan_list, fan_triangles)) {
                v9x_trace_push(V9X_TRACE_D3D_PRIMREJECT,
                               0x30000000ul | index);
                ok = 0;
            }
            if (!ok) {
                break;
            }
        }
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }

    /*
     * The same three answers the two single-primitive paths give.
     *
     * An instruction this path does not serve is handed back for the
     * runtime to execute; a batch the engine refused is DD_OK with a count,
     * because it is this driver's problem and not a description of the
     * call. Final Reality quits on DDERR_INVALIDPARAMS from any of them,
     * and this entry point was the last one still returning it.
     */
    if (!served) {
        v9x_fpu_restore(&fpu);
        v9x_trace_exit(V9X_TRACE_D3D_RENDERPRIM, 0ul);
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (!ok && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.batches_engine_refused;
    }
    if (data != 0) {
        data->ddrval = V9X_DD_OK;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_RENDERPRIM, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dSetRenderTarget(
    V9X_D3DHAL_SETRENDERTARGETDATA *data)
{
    V9X_D3D_CONTEXT *context = data != 0
        ? v9x_d3d_context_from_handle(data->dwhContext) : 0;

    v9x_trace_enter(V9X_TRACE_D3D_SETRENDERTARGET,
                    data != 0 ? data->dwhContext : 0ul);
    if (context == 0 ||
        !v9x_d3d_set_target(context, data->lpDDS, data->lpDDSZ)) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_SETRENDERTARGET, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    data->ddrval = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_D3D_SETRENDERTARGET, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * One bit per type value, so a capture names what an application asked for
 * rather than how often. D3DPRIMITIVETYPE runs 1..6 and D3DVERTEXTYPE 1..3;
 * anything outside a mask's width lands in bit 0 rather than shifting off
 * the end, which is undefined and would report nothing at all.
 */
static DWORD v9x_d3d_type_bit(DWORD value)
{
    return value < 32ul ? (1ul << value) : 1ul;
}

static DWORD v9x_d3d_draw_one_primitive_body(
    V9X_D3DHAL_DRAWONEPRIMITIVEDATA *data);

/* Timed as V9X_TIME_D3D_CALLS; the work is in the body. */
DWORD __stdcall V9xD3dDrawOnePrimitive(
    V9X_D3DHAL_DRAWONEPRIMITIVEDATA *data)
{
    DWORD started = V9X_TIME_BEGIN();
    DWORD result;

    v9x_win16_sample(V9X_WIN16_SITE_D3D_DRAWONE);
    result = v9x_d3d_draw_one_primitive_body(data);

    V9X_TIME_END(V9X_TIME_D3D_CALLS, started);
    return result;
}

static DWORD v9x_d3d_draw_one_primitive_body(
    V9X_D3DHAL_DRAWONEPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    int ok = 0;
    int served = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEPRIM,
                    data != 0
                        ? ((data->PrimitiveType << 16) |
                           (data->dwNumVertices & 0xfffful))
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    /*
     * Counted, which it was not until 2026-09-20. This entry point is
     * advertised in the callbacks table and serves exactly one shape - a
     * three-vertex TRIANGLELIST - so everything else set an error and drew
     * nothing with no record that it had happened.
     */
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.oneprim_calls;
        if (data != 0) {
            v9x_hal->d3d_diagnostics.oneprim_primtype_seen |=
                v9x_d3d_type_bit(data->PrimitiveType);
            v9x_hal->d3d_diagnostics.oneprim_vertextype_seen |=
                v9x_d3d_type_bit(data->VertexType);
        }
    }
    if (ops != 0 && context != 0 && ops->ready() &&
        data->PrimitiveType == V9X_D3DPT_TRIANGLELIST &&
        data->VertexType == V9X_D3DVT_TLVERTEX &&
        data->lpvVertices != 0 && data->dwNumVertices >= 3ul &&
        (data->dwNumVertices % 3ul) == 0ul) {
        /*
         * Any length, not exactly three.
         *
         * This accepted dwNumVertices == 3 and nothing else, and the ViRGE
         * guest measured what that cost: 852 calls, 852 refusals, ZERO
         * served, every one a TRIANGLELIST and the last of them 1,260
         * vertices - 420 triangles thrown away in a single call. A list is a
         * list; the count was never a property of the shape.
         *
         * The vertices are already contiguous, so unlike the indexed path
         * there is nothing to gather - the batches are windows on the
         * caller's array. Chunked at the same bound for the same reason.
         */
        const V9X_D3DTLVERTEX *vertices =
            (const V9X_D3DTLVERTEX *)data->lpvVertices;
        DWORD remaining = data->dwNumVertices / 3ul;

        ok = 1;
        while (remaining != 0ul) {
            DWORD batch = remaining > (DWORD)V9X_D3D_INDEXED_BATCH
                              ? (DWORD)V9X_D3D_INDEXED_BATCH : remaining;

            if (!v9x_d3d_draw_list(ops, context, vertices, batch)) {
                ok = 0;
                break;
            }
            vertices += batch * 3ul;
            remaining -= batch;
        }
        served = 1;
        if (ok && v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.oneprim_drawn;
            v9x_hal->d3d_diagnostics.oneprim_triangles +=
                data->dwNumVertices / 3ul;
        }
    } else if (v9x_hal != 0 && data != 0) {
        if (data->PrimitiveType != V9X_D3DPT_TRIANGLELIST) {
            ++v9x_hal->d3d_diagnostics.oneprim_refused_primtype;
        } else if (data->VertexType != V9X_D3DVT_TLVERTEX) {
            ++v9x_hal->d3d_diagnostics.oneprim_refused_vertextype;
        } else if (data->dwNumVertices != 3ul) {
            /* A TRIANGLELIST of more than one triangle, which this path
             * could serve and does not. The last count says how many. */
            ++v9x_hal->d3d_diagnostics.oneprim_refused_count;
            v9x_hal->d3d_diagnostics.oneprim_count_last =
                data->dwNumVertices;
        }
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }

    /* Declined, not failed - see the note in
     * V9xD3dDrawOneIndexedPrimitive. This path served exactly one shape for
     * most of its life and answered every other with an error. */
    if (!served) {
        v9x_fpu_restore(&fpu);
        v9x_trace_exit(V9X_TRACE_D3D_DRAWONEPRIM, 0ul);
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* There is no malformed case on this path - the vertices are the
     * caller's own contiguous array - so a failure here is always the
     * engine's, and always DD_OK with a count. */
    if (!ok && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.batches_engine_refused;
    }
    if (data != 0) {
        data->ddrval = V9X_DD_OK;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEPRIM, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/* Count refusal once per submitted record run and keep parsing records. */
static int v9x_d3d_records_batch(void *user,
                                 const V9X_R3D_VERTEX *vertices,
                                 v9x_u32 triangles)
{
    V9X_D3D_LIST_SINK *sink = (V9X_D3D_LIST_SINK *)user;
    DWORD submitted_before = v9x_hal != 0 ?
        v9x_hal->d3d_diagnostics.i9xx_draws_submitted : 0ul;
    DWORD refused_before = v9x_hal != 0 ?
        v9x_hal->d3d_diagnostics.i9xx_draws_refused : 0ul;
    int accepted = v9x_d3d_draw_list(sink->ops, sink->context,
                                    (const V9X_D3DTLVERTEX *)vertices,
                                    triangles);
    if (!accepted && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.batches_engine_refused;
        /* Gen3 reason 6 rejects the CPU vertex stream before ring submission.
         * With no submitted prefix it is safe to replay original records;
         * an invalid record must not take its valid neighbours with it.
         * Never retry submission/timeout failures or another engine's refusal. */
        if (v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3 &&
            v9x_hal->d3d_diagnostics.i9xx_draws_refused != refused_before &&
            v9x_hal->d3d_diagnostics.i9xx_refuse_last == 6ul &&
            v9x_hal->d3d_diagnostics.i9xx_draws_submitted == submitted_before) {
            return -1;
        }
    }
    return accepted;
}

static DWORD v9x_d3d_draw_primitives_body(
    V9X_D3DHAL_DRAWPRIMITIVESDATA *data);

/*
 * Whether two copies of a context are identical, DWORD by DWORD. The
 * context holds only DWORDs and 32-bit pointers, so there is no padding to
 * compare. There is no C library in the HAL, hence no memcmp.
 *
 * Used to ask whether a DrawPrimitives record's state pairs changed
 * anything: v9x_d3d_apply_state writes nothing but the context and
 * diagnostic counters, so an unchanged context means the pairs were
 * redundant, and a merge could have carried the record in the previous run.
 */
typedef char v9x_d3d_assert_context_dwords[
    sizeof(V9X_D3D_CONTEXT) % sizeof(DWORD) == 0u ? 1 : -1];

static int v9x_d3d_context_same(const V9X_D3D_CONTEXT *a,
                                const V9X_D3D_CONTEXT *b)
{
    const DWORD *left = (const DWORD *)a;
    const DWORD *right = (const DWORD *)b;
    DWORD index;

    for (index = 0ul; index < sizeof(V9X_D3D_CONTEXT) / sizeof(DWORD);
         ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

/* Timed as V9X_TIME_D3D_CALLS; the work is in the body. */
DWORD __stdcall V9xD3dDrawPrimitives(
    V9X_D3DHAL_DRAWPRIMITIVESDATA *data)
{
    DWORD started = V9X_TIME_BEGIN();
    DWORD result;

    v9x_win16_sample(V9X_WIN16_SITE_D3D_DRAWPRIMS);
    result = v9x_d3d_draw_primitives_body(data);

    V9X_TIME_END(V9X_TIME_D3D_CALLS, started);
    return result;
}

static DWORD v9x_d3d_draw_primitives_body(
    V9X_D3DHAL_DRAWPRIMITIVESDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts;
    BYTE *cursor;
    DWORD record;
    V9X_R3D_RECORDS run;
    V9X_D3D_LIST_SINK sink;
    v9x_u32 record_run = 0ul;
    v9x_u32 record_run_noop = 0ul;
    v9x_u32 record_triangles;
    V9X_D3D_CONTEXT state_before;
    int record_state_noop;
    int record_texture_changed;
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWPRIMS,
                    data != 0 ? (DWORD)data->lpvData : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    if (ops != 0 && context != 0 && ops->ready() &&
        data->lpvData != 0) {
        cursor = (BYTE *)data->lpvData;
        sink.ops = ops;
        sink.context = context;
        run.vertices = (V9X_R3D_VERTEX *)v9x_d3d_gather;
        run.capacity = (v9x_u32)V9X_D3D_INDEXED_BATCH;
        run.pending = 0ul;
        run.record_count = 0ul;
        run.batch = v9x_d3d_records_batch;
        run.user = &sink;
        ok = 1;
        for (record = 0ul; record < 64ul; ++record) {
            counts = (V9X_D3DHAL_DRAWPRIMCOUNTS *)cursor;
            cursor += sizeof(*counts);
            /*
             * The state changes, APPLIED. This used to add the pairs' width
             * to the cursor and walk on, which is why 3DMark99 ran an entire
             * benchmark without ever setting a filter: it sends its states
             * here and nowhere else.
             *
             * The pairs are (state, value), the same shape V9X_D3DSTATE has,
             * so one switch serves this and the RenderState callback.
             *
             * THE COUNT IS BOUNDED, and it has to be here: the DDK's data
             * block carries no length, only the wNumVertices == 0 terminator,
             * so wNumStateChanges is the sole thing saying how far to read.
             * It is a WORD, so an unchecked one walks up to half a megabyte
             * of whatever follows the buffer.
             *
             * The old bound was 64 and rejected the record. That was too
             * small - 3DMark99 sends 101 - so it would have thrown the record
             * away even if the pairs had been read, which is a second defect
             * in the same three lines. V9X_D3D_STATE_MAX is the same backstop
             * the RenderState path uses and is ten times the measured
             * maximum; past it the stream cannot be trusted to be a stream,
             * so parsing stops rather than advancing over a length it does
             * not believe.
             */
            if ((DWORD)counts->wNumStateChanges > (DWORD)V9X_D3D_STATE_MAX) {
                if (v9x_hal != 0) {
                    ++v9x_hal->d3d_diagnostics.state_clamped;
                    v9x_hal->d3d_diagnostics.state_clamped_count =
                        (DWORD)counts->wNumStateChanges;
                }
                ok = 0;
                break;
            }
            {
                DWORD change;
                DWORD *pairs = (DWORD *)cursor;

                if (v9x_hal != 0 &&
                    (DWORD)counts->wNumStateChanges >
                        v9x_hal->d3d_diagnostics.state_max_count) {
                    v9x_hal->d3d_diagnostics.state_max_count =
                        (DWORD)counts->wNumStateChanges;
                }
                if (counts->wNumStateChanges != 0u) {
                    state_before = *context;
                }
                for (change = 0ul;
                     change < (DWORD)counts->wNumStateChanges; ++change) {
                    v9x_d3d_apply_state(context, pairs[change * 2ul],
                                        pairs[change * 2ul + 1ul]);
                }
                record_state_noop = 0;
                record_texture_changed = 0;
                if (counts->wNumStateChanges != 0u) {
                    record_state_noop =
                        v9x_d3d_context_same(&state_before, context);
                    record_texture_changed =
                        state_before.texture_handle !=
                        context->texture_handle;
                    if (!record_state_noop && run.pending != 0ul) {
                        V9X_D3D_CONTEXT state_after = *context;
                        /* The sink reads the live context. Draw the old run
                         * before exposing the new record's state to it. */
                        *context = state_before;
                        (void)v9x_r3d_records_flush(&run);
                        *context = state_after;
                    }
                }
            }
            cursor += (DWORD)counts->wNumStateChanges * 2ul * sizeof(DWORD);
            if (counts->wNumVertices == 0u) {
                break;
            }
            cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
            if (v9x_hal != 0) {
                v9x_hal->d3d_diagnostics.dp_primtype_seen |=
                    v9x_d3d_type_bit((DWORD)counts->wPrimitiveType);
                v9x_hal->d3d_diagnostics.dp_verttype_seen |=
                    v9x_d3d_type_bit((DWORD)counts->wVertexType);
            }
            if ((counts->wPrimitiveType != V9X_D3DPT_TRIANGLELIST &&
                 counts->wPrimitiveType != V9X_D3DPT_TRIANGLEFAN) ||
                counts->wVertexType != V9X_D3DVT_TLVERTEX ||
                counts->wNumVertices > 192u ||
                (counts->wPrimitiveType == V9X_D3DPT_TRIANGLEFAN
                     ? counts->wNumVertices < 3u
                     : (counts->wNumVertices % 3u) != 0u)) {
                /* Which of the four, and whether this buffer had already
                 * drawn - see the note beside dp_primtype_seen. */
                if (v9x_hal != 0) {
                    if (counts->wPrimitiveType != V9X_D3DPT_TRIANGLELIST) {
                        ++v9x_hal->d3d_diagnostics.dp_refused_primtype;
                    } else if (counts->wVertexType != V9X_D3DVT_TLVERTEX) {
                        ++v9x_hal->d3d_diagnostics.dp_refused_verttype;
                    } else {
                        ++v9x_hal->d3d_diagnostics.dp_refused_count;
                    }
                    v9x_hal->d3d_diagnostics.dp_refused_vertices_last =
                        (DWORD)counts->wNumVertices;
                    if (record != 0ul) {
                        ++v9x_hal->d3d_diagnostics.dp_drawn_before_refusal;
                    }
                }
                ok = 0;
                break;
            }
            /*
             * Keep the original state-free and unchanged-context run
             * predictions for comparison with actual list calls. Half-Life
             * arrives as records of about two triangles; these counters
             * measure the opportunity independently of the accumulator.
             */
            record_triangles = counts->wPrimitiveType ==
                                       V9X_D3DPT_TRIANGLEFAN
                                   ? (v9x_u32)counts->wNumVertices - 2ul
                                   : (v9x_u32)counts->wNumVertices / 3ul;
            if (v9x_hal != 0) {
                v9x_u32 previous_run = record_run;

                ++v9x_hal->d3d_diagnostics.dp_records;
                v9x_hal->d3d_diagnostics.dp_record_triangles +=
                    record_triangles;
                if (v9x_r3d_record_run_step(
                        &record_run,
                        (v9x_u32)counts->wNumStateChanges,
                        record_triangles,
                        (v9x_u32)V9X_D3D_INDEXED_BATCH)) {
                    ++v9x_hal->d3d_diagnostics.dp_record_runs;
                    if (previous_run != 0ul &&
                        counts->wNumStateChanges != 0u) {
                        ++v9x_hal->d3d_diagnostics.dp_record_state_breaks;
                    }
                }
                /* The same merge if pairs that changed nothing did not
                 * break a run: what comparing state would add. */
                if (counts->wNumStateChanges != 0u) {
                    ++v9x_hal->d3d_diagnostics.dp_state_records;
                    if (record_state_noop) {
                        ++v9x_hal->d3d_diagnostics.dp_state_records_noop;
                    }
                    if (record_texture_changed) {
                        ++v9x_hal->d3d_diagnostics.dp_state_records_texture;
                    }
                }
                if (v9x_r3d_record_run_step(
                        &record_run_noop,
                        record_state_noop
                            ? 0ul
                            : (v9x_u32)counts->wNumStateChanges,
                        record_triangles,
                        (v9x_u32)V9X_D3D_INDEXED_BATCH)) {
                    ++v9x_hal->d3d_diagnostics.dp_record_runs_noop_joined;
                }
            }
            /* Accumulate only inside this call and under one context. */
            if (counts->wPrimitiveType == V9X_D3DPT_TRIANGLEFAN) {
                (void)v9x_r3d_records_append_fan(
                    &run, (const V9X_R3D_VERTEX *)cursor,
                    (v9x_u32)counts->wNumVertices);
            } else {
                (void)v9x_r3d_records_append_list(
                    &run, (const V9X_R3D_VERTEX *)cursor, record_triangles);
            }
            cursor += (DWORD)counts->wNumVertices *
                      sizeof(V9X_D3DTLVERTEX);
        }
        /* Terminator, bound, clamped state count, and refused shape all
         * leave through here with the pending run drawn. */
        (void)v9x_r3d_records_flush(&run);
        if (record == 64ul) {
            ok = 0;
        }
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }
    /* A record shape this build cannot parse leaves the rest of the buffer
     * undrawn, which is a hole; saying INVALIDPARAMS makes an application
     * stop altogether, which is worse. Counted, not reported. */
    if (!ok && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.batches_engine_refused;
    }
    if (data != 0) {
        data->ddrval = V9X_DD_OK;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWPRIMS, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * One indexed primitive, drawn.
 *
 * This was a stub that returned NOTHANDLED, and the callbacks2 table
 * advertised it anyway. intel97 measured the cost: 64,251 calls on the
 * netbook, more than DrawPrimitives and DrawOnePrimitive together, every one
 * declined by a driver that had told the runtime it served them. That is the
 * advertise-then-ignore pattern the TEXTURESYSTEMMEMORY comment in
 * d3d_i9xx.c exists to warn against, in the path an application uses most.
 *
 * The indices are a WORD array choosing from a vertex pool, so the batch is
 * gathered rather than pointed at: the engines take a contiguous triangle
 * list and nothing here may hand them one the application did not build.
 * V9X_D3D_INDEXED_BATCH bounds the scratch, and a long list is flushed in
 * pieces rather than refused - the same reasoning as the state-block clamp.
 *
 * EVERY index is range-checked against dwNumVertices before it is used. That
 * is not defensive style, it is the memory-safety boundary: an index the
 * driver trusts is an arbitrary read at four-byte granularity out of a
 * pointer the runtime supplied.
 */
static DWORD v9x_d3d_draw_one_indexed_primitive_body(
    V9X_D3DHAL_DRAWONEINDEXEDPRIMITIVEDATA *data);

/* Timed as V9X_TIME_D3D_CALLS; the work is in the body. */
DWORD __stdcall V9xD3dDrawOneIndexedPrimitive(
    V9X_D3DHAL_DRAWONEINDEXEDPRIMITIVEDATA *data)
{
    DWORD started = V9X_TIME_BEGIN();
    DWORD result;

    v9x_win16_sample(V9X_WIN16_SITE_D3D_DRAWINDEX);
    result = v9x_d3d_draw_one_indexed_primitive_body(data);

    V9X_TIME_END(V9X_TIME_D3D_CALLS, started);
    return result;
}

static DWORD v9x_d3d_draw_one_indexed_primitive_body(
    V9X_D3DHAL_DRAWONEINDEXEDPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3DTLVERTEX *batch = v9x_d3d_gather;
    const V9X_D3DTLVERTEX *pool;
    DWORD triangles = 0ul;
    DWORD index;
    int ok = 0;
    int served = 0;
    int bad_index = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEINDEXED,
                    data != 0
                        ? ((data->PrimitiveType << 16) |
                           (data->dwNumIndices & 0xfffful))
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.indexed_calls;
        if (data != 0) {
            v9x_hal->d3d_diagnostics.indexed_primtype_seen |=
                v9x_d3d_type_bit(data->PrimitiveType);
            v9x_hal->d3d_diagnostics.indexed_vertextype_seen |=
                v9x_d3d_type_bit(data->VertexType);
        }
    }

    if (ops != 0 && context != 0 && ops->ready() && data != 0 &&
        (data->PrimitiveType == V9X_D3DPT_TRIANGLELIST ||
         data->PrimitiveType == V9X_D3DPT_TRIANGLESTRIP) &&
        data->VertexType == V9X_D3DVT_TLVERTEX &&
        data->lpvVertices != 0 && data->lpwIndices != 0 &&
        data->dwNumVertices != 0ul && data->dwNumIndices >= 3ul &&
        (data->PrimitiveType == V9X_D3DPT_TRIANGLESTRIP ||
         (data->dwNumIndices % 3ul) == 0ul)) {
        /*
         * A strip, as well as a list.
         *
         * The ViRGE guest measured every one of 44,952 refusals as the
         * primitive type, with IndexedPrimTypeSeen 0x30 - lists and strips
         * and nothing else. So this one type is the whole of what was being
         * turned away, and the vertex type, the pointers and the index count
         * never refused a single call.
         *
         * A strip of N indices is N-2 triangles sharing edges, and its
         * winding alternates: the odd triangle takes its first two vertices
         * swapped. The swap is what gives the whole strip one winding, which
         * the core's back-face culling depends on (v9x_d3d_triangle_culled),
         * and the order also decides which vertex provokes for flat shading.
         */
        DWORD step = data->PrimitiveType == V9X_D3DPT_TRIANGLESTRIP
                         ? 1ul : 3ul;

        pool = (const V9X_D3DTLVERTEX *)data->lpvVertices;
        ok = 1;
        served = 1;
        for (index = 0ul; index + 2ul < data->dwNumIndices; index += step) {
            DWORD swap = step == 1ul && (index & 1ul) != 0ul;
            DWORD first = (DWORD)data->lpwIndices[index + (swap ? 1ul : 0ul)];
            DWORD second = (DWORD)data->lpwIndices[index + (swap ? 0ul : 1ul)];
            DWORD third = (DWORD)data->lpwIndices[index + 2ul];

            if (first >= data->dwNumVertices ||
                second >= data->dwNumVertices ||
                third >= data->dwNumVertices) {
                if (v9x_hal != 0) {
                    ++v9x_hal->d3d_diagnostics.indexed_refused_index;
                }
                /* The one failure that IS the call's description. */
                bad_index = 1;
                ok = 0;
                break;
            }
            batch[triangles * 3ul] = pool[first];
            batch[triangles * 3ul + 1ul] = pool[second];
            batch[triangles * 3ul + 2ul] = pool[third];
            ++triangles;

            if (triangles == (DWORD)V9X_D3D_INDEXED_BATCH) {
                if (!v9x_d3d_draw_list(ops, context, batch, triangles)) {
                    ok = 0;
                    break;
                }
                if (v9x_hal != 0) {
                    v9x_hal->d3d_diagnostics.indexed_triangles += triangles;
                }
                triangles = 0ul;
            }
        }
        if (ok && triangles != 0ul) {
            if (!v9x_d3d_draw_list(ops, context, batch, triangles)) {
                ok = 0;
            } else if (v9x_hal != 0) {
                v9x_hal->d3d_diagnostics.indexed_triangles += triangles;
            }
        }
    } else if (v9x_hal != 0) {
        /*
         * A shape this build does not serve, split by WHICH condition failed.
         * The aggregate stays for continuity with the captures that have only
         * it; the four below say what to implement next, which the aggregate
         * could not.
         */
        ++v9x_hal->d3d_diagnostics.indexed_refused_shape;
        if (data != 0) {
            if (data->PrimitiveType != V9X_D3DPT_TRIANGLELIST &&
                data->PrimitiveType != V9X_D3DPT_TRIANGLESTRIP) {
                ++v9x_hal->d3d_diagnostics.indexed_refused_primtype;
            } else if (data->VertexType != V9X_D3DVT_TLVERTEX) {
                ++v9x_hal->d3d_diagnostics.indexed_refused_vertextype;
            } else if (data->lpvVertices == 0 || data->lpwIndices == 0 ||
                       data->dwNumVertices == 0ul) {
                ++v9x_hal->d3d_diagnostics.indexed_refused_null;
            } else if (data->dwNumIndices < 3ul ||
                       (data->dwNumIndices % 3ul) != 0ul) {
                ++v9x_hal->d3d_diagnostics.indexed_refused_count;
            }
        }
    }

    if (ok && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.indexed_drawn;
    }

    /*
     * A SHAPE THIS BUILD DOES NOT SERVE IS DECLINED, NOT FAILED.
     *
     * The stub this replaced returned DRIVER_NOTHANDLED, which tells the
     * runtime to do the work itself. Answering HANDLED with an error instead
     * says the call was mine and it went wrong, and an application that
     * believes that stops: Final Reality put up "DrawPrimitive
     * DDERR_INVALIDPARAMS" and quit on the first fan it sent, against a
     * native S3 driver rendering the same scene beside it.
     *
     * So the distinction is between cannot and failed. A shape outside this
     * path's repertoire is handed back untouched. A supported shape whose
     * draw genuinely failed keeps the error, because that IS this driver's
     * fault and hiding it would make a dropped frame look like a declined
     * one.
     */
    if (!served) {
        v9x_fpu_restore(&fpu);
        v9x_trace_exit(V9X_TRACE_D3D_DRAWONEINDEXED, 0ul);
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (!ok && !bad_index && v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.batches_engine_refused;
    }
    if (data != 0) {
        data->ddrval = bad_index ? 0x80070057ul : V9X_DD_OK;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEINDEXED,
                   bad_index ? 0x80070057ul : V9X_DD_OK);
    return V9X_DDHAL_DRIVER_HANDLED;
}


/*
 * DrawPrimitives2, the DirectX 6 driver interface (DDI 6).
 *
 * Served only when the 16-bit side stamps V9X_DD_ENGINE_CAP_D3D_DP2, from
 * [Velocity9x] Direct3DDdi=6: a runtime that finds this entry sends every
 * Direct3D application through it, so it replaces the DX5 paths above
 * rather than adding to them, and stays off until it has been measured on
 * each family (include\velocity9x\engine_abi.h).
 *
 * It is a front end on the same machinery, not a new draw path. The command
 * stream is walked by d3d_dp2.c, which checks every length and index; render
 * states go through v9x_d3d_apply_state exactly as the DX5 RenderState and
 * DrawPrimitives pairs do; and the triangles go into the same record run,
 * clipper, culling and engine batch as DrawPrimitives. Texture stage 0 is
 * re-expressed as the DX5 fields, because that is what every engine reads.
 *
 * The vertices are D3DTLVERTEX and nothing else: the extended caps report
 * dwFVFCaps zero, which d3dhal.h defines as "TLVERTEX only", so the runtime
 * converts any other format before it gets here. A call that arrives with
 * another format anyway is refused whole and counted rather than read at a
 * stride the engines do not take.
 */
static const V9X_D3D_ENGINE_OPS *v9x_d3d_publish_engine(void);

static int v9x_d3d_dp2_enabled(void)
{
    return v9x_hal != 0 &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D_DP2) != 0ul &&
           v9x_d3d_publish_engine() != 0;
}

/* The instrument's bits (engine_abi.h), zero for the plain answer. */
static DWORD v9x_d3d_dp2_probe(void)
{
    if (!v9x_d3d_dp2_enabled()) {
        return 0ul;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_DP2_PROBE_MASK)
           >> V9X_DD_ENGINE_CAP_DP2_PROBE_SHIFT;
}

/*
 * The parts of the published description that follow the DDI level: the
 * DrawPrimitives2 device cap, and under the probe bits the DX3 execute
 * entries and the execute-buffer table. Applied when the tables are
 * published and again on every GetDriverInfo, because the level is read
 * per driver object and nothing here knows which copy the runtime keeps.
 */
static void v9x_d3d_apply_ddi_level(void)
{
    DWORD probe = v9x_d3d_dp2_probe();

    if (v9x_hal == 0) {
        return;
    }
    if (v9x_d3d_dp2_enabled() && (probe & V9X_DP2_PROBE_NO_DEVCAP) == 0ul) {
        v9x_hal->d3d_global.hwCaps.dwDevCaps |= V9X_D3DDEVCAPS_DRAWPRIMITIVES2;
    } else {
        v9x_hal->d3d_global.hwCaps.dwDevCaps &=
            ~V9X_D3DDEVCAPS_DRAWPRIMITIVES2;
    }
    if ((probe & V9X_DP2_PROBE_NO_DX3_ENTRIES) != 0ul) {
        v9x_hal->d3d_callbacks.RenderState = 0;
        v9x_hal->d3d_callbacks.RenderPrimitive = 0;
    } else {
        v9x_hal->d3d_callbacks.RenderState =
            (V9X_DD_CODE_PTR)V9xD3dRenderState;
        v9x_hal->d3d_callbacks.RenderPrimitive =
            (V9X_DD_CODE_PTR)V9xD3dRenderPrimitive;
    }
    v9x_hal->info.lpDDExeBufCallbacks =
        (probe & V9X_DP2_PROBE_EXEBUF_CALLBACKS) != 0ul
            ? (V9X_DD_VOID_PTR)&v9x_hal->execute_buffer_callbacks : 0;
}

/* What the walker's callbacks need, behind its void pointer. */
typedef struct v9x_d3d_dp2_user {
    V9X_D3D_CONTEXT *context;
    V9X_R3D_RECORDS *run;
    DWORD *rstates;
} V9X_D3D_DP2_USER;

/*
 * After a state change: if the context moved and triangles are pending,
 * draw them under the state they were recorded with. The same rule as the
 * DX5 DrawPrimitives records, and for the same reason - the batch sink reads
 * the live context.
 */
static void v9x_d3d_dp2_settle(V9X_D3D_DP2_USER *user,
                               const V9X_D3D_CONTEXT *before)
{
    if (user->run->pending != 0ul &&
        !v9x_d3d_context_same(before, user->context)) {
        V9X_D3D_CONTEXT after = *user->context;

        *user->context = *before;
        (void)v9x_r3d_records_flush(user->run);
        *user->context = after;
    }
}

static void v9x_d3d_dp2_render_state(void *opaque, v9x_u32 state,
                                     v9x_u32 value)
{
    V9X_D3D_DP2_USER *user = (V9X_D3D_DP2_USER *)opaque;
    V9X_D3D_CONTEXT before = *user->context;

    v9x_d3d_apply_state(user->context, (DWORD)state, (DWORD)value);
    /* The runtime keeps its copy of the render states in this array and
     * reads GetRenderState answers from it; a DDI 6 driver writes what it
     * was sent. */
    if (user->rstates != 0 && state < V9X_D3DHAL_MAX_RSTATES_DX6) {
        user->rstates[state] = (DWORD)value;
    }
    v9x_d3d_dp2_settle(user, &before);
}

/* D3DTSS_MINFILTER and MIPFILTER folded into the one DX5 value. Anything
 * past LINEAR (anisotropic) is drawn linear, which is what this caps claim. */
static DWORD v9x_d3d_dp2_min_filter(const V9X_D3D_CONTEXT *context)
{
    int linear = context->stage_min >= V9X_D3DTFG_LINEAR;

    if (context->stage_mip <= V9X_D3DTFP_NONE) {
        return linear ? V9X_D3DFILTER_LINEAR : V9X_D3DFILTER_NEAREST;
    }
    if (context->stage_mip == V9X_D3DTFP_POINT) {
        return linear ? V9X_D3DFILTER_MIPLINEAR : V9X_D3DFILTER_MIPNEAREST;
    }
    return linear ? V9X_D3DFILTER_LINEARMIPLINEAR
                  : V9X_D3DFILTER_LINEARMIPNEAREST;
}

/*
 * Stage 0's combine as the DX5 texture blend, and whether it samples the
 * texture at all.
 *
 * DX5 says "no texture" with handle 0, so a stage that is disabled, or that
 * selects or multiplies only non-texture arguments, binds handle 0. The four
 * combines DX5 has names for map exactly: SELECTARG of the texture is DECAL,
 * MODULATE is MODULATE (MODULATEALPHA when alpha modulates too), and
 * BLENDTEXTUREALPHA is DECALALPHA. Anything else is drawn as MODULATE and
 * counted, because the caps claim only those ops and an application that
 * asks for another has not checked them.
 */
static void v9x_d3d_dp2_express_stage(V9X_D3D_CONTEXT *context)
{
    DWORD op = context->stage_color_op;
    DWORD arg1 = context->stage_color_arg1 & V9X_D3DTA_SELECTMASK;
    DWORD arg2 = context->stage_color_arg2 & V9X_D3DTA_SELECTMASK;
    DWORD textured;
    DWORD blend = V9X_D3DTBLEND_MODULATE;

    if (op == V9X_D3DTOP_DISABLE) {
        textured = 0ul;
    } else if (op == V9X_D3DTOP_SELECTARG1) {
        textured = arg1 == V9X_D3DTA_TEXTURE;
        blend = V9X_D3DTBLEND_DECAL;
    } else if (op == V9X_D3DTOP_SELECTARG2) {
        textured = arg2 == V9X_D3DTA_TEXTURE;
        blend = V9X_D3DTBLEND_DECAL;
    } else if (op == V9X_D3DTOP_MODULATE) {
        textured = arg1 == V9X_D3DTA_TEXTURE || arg2 == V9X_D3DTA_TEXTURE;
        blend = context->stage_alpha_op == V9X_D3DTOP_MODULATE
                    ? V9X_D3DTBLEND_MODULATEALPHA : V9X_D3DTBLEND_MODULATE;
    } else if (op == V9X_D3DTOP_BLENDTEXTUREALPHA) {
        textured = 1ul;
        blend = V9X_D3DTBLEND_DECALALPHA;
    } else {
        textured = 1ul;
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.dp2_stage0_approximated;
        }
    }
    v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREMAPBLEND, blend);
    v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                        textured ? context->stage_texture : 0ul);
}

static void v9x_d3d_dp2_stage_state(void *opaque, v9x_u32 stage,
                                    v9x_u32 state, v9x_u32 value)
{
    V9X_D3D_DP2_USER *user = (V9X_D3D_DP2_USER *)opaque;
    V9X_D3D_CONTEXT *context = user->context;
    V9X_D3D_CONTEXT before;

    /* One texture unit, reported as one blend stage: a later stage has
     * nothing to configure, and counting it says whether anyone tried. */
    if (stage != 0ul) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.dp2_stage1_states;
        }
        return;
    }
    before = *context;
    switch (state) {
    case V9X_D3DTSS_TEXTUREMAP:
        context->stage_texture = (DWORD)value;
        v9x_d3d_dp2_express_stage(context);
        break;
    case V9X_D3DTSS_COLOROP:
        context->stage_color_op = (DWORD)value;
        v9x_d3d_dp2_express_stage(context);
        break;
    case V9X_D3DTSS_COLORARG1:
        context->stage_color_arg1 = (DWORD)value;
        v9x_d3d_dp2_express_stage(context);
        break;
    case V9X_D3DTSS_COLORARG2:
        context->stage_color_arg2 = (DWORD)value;
        v9x_d3d_dp2_express_stage(context);
        break;
    case V9X_D3DTSS_ALPHAOP:
        context->stage_alpha_op = (DWORD)value;
        v9x_d3d_dp2_express_stage(context);
        break;
    case V9X_D3DTSS_ADDRESS:
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREADDRESS,
                            (DWORD)value);
        break;
    case V9X_D3DTSS_ADDRESSU:
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREADDRESSU,
                            (DWORD)value);
        break;
    case V9X_D3DTSS_ADDRESSV:
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREADDRESSV,
                            (DWORD)value);
        break;
    case V9X_D3DTSS_BORDERCOLOR:
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_BORDERCOLOR,
                            (DWORD)value);
        break;
    case V9X_D3DTSS_MAGFILTER:
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREMAG,
                            value >= V9X_D3DTFG_LINEAR
                                ? V9X_D3DFILTER_LINEAR
                                : V9X_D3DFILTER_NEAREST);
        break;
    case V9X_D3DTSS_MINFILTER:
        context->stage_min = (DWORD)value;
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREMIN,
                            v9x_d3d_dp2_min_filter(context));
        break;
    case V9X_D3DTSS_MIPFILTER:
        context->stage_mip = (DWORD)value;
        v9x_d3d_apply_state(context, V9X_D3DRENDERSTATE_TEXTUREMIN,
                            v9x_d3d_dp2_min_filter(context));
        break;
    default:
        /* The alpha arguments, texture coordinate index, LOD bias and the
         * bump-map states: one unit with the DX5 blends has no use for
         * them. */
        break;
    }
    v9x_d3d_dp2_settle(user, &before);
}

static void v9x_d3d_dp2_list(void *opaque, const v9x_u8 *first,
                             v9x_u32 triangles)
{
    V9X_D3D_DP2_USER *user = (V9X_D3D_DP2_USER *)opaque;

    (void)v9x_r3d_records_append_list(user->run,
                                      (const V9X_R3D_VERTEX *)first,
                                      triangles);
}

static void v9x_d3d_dp2_fan(void *opaque, const v9x_u8 *first,
                            v9x_u32 vertices)
{
    V9X_D3D_DP2_USER *user = (V9X_D3D_DP2_USER *)opaque;

    (void)v9x_r3d_records_append_fan(user->run,
                                     (const V9X_R3D_VERTEX *)first, vertices);
}

static void v9x_d3d_dp2_triangle(void *opaque, const v9x_u8 *a,
                                 const v9x_u8 *b, const v9x_u8 *c)
{
    V9X_D3D_DP2_USER *user = (V9X_D3D_DP2_USER *)opaque;
    V9X_R3D_VERTEX triangle[3];

    triangle[0] = *(const V9X_R3D_VERTEX *)a;
    triangle[1] = *(const V9X_R3D_VERTEX *)b;
    triangle[2] = *(const V9X_R3D_VERTEX *)c;
    (void)v9x_r3d_records_append_list(user->run, triangle, 1ul);
}

/*
 * The memory behind a DrawPrimitives2 surface: the LOCAL object the runtime
 * passed (not an interface wrapper, unlike the DX5 callbacks), its GLOBAL
 * half, and fpVidMem. Every pointer is the runtime's and is tested before
 * it is read, for the reason v9x_d3d_surface_lcl gives.
 */
static BYTE *v9x_d3d_dp2_memory(void *surface)
{
    V9X_DD_SURFACE_LCL *lcl = (V9X_DD_SURFACE_LCL *)surface;

    if (lcl == 0 || IsBadReadPtr(lcl, sizeof(*lcl)) || lcl->lpGbl == 0 ||
        IsBadReadPtr(lcl->lpGbl, sizeof(*lcl->lpGbl)) ||
        lcl->lpGbl->fpVidMem == 0ul) {
        return 0;
    }
    return (BYTE *)lcl->lpGbl->fpVidMem;
}

static DWORD v9x_d3d_draw_primitives2_body(
    V9X_D3DHAL_DRAWPRIMITIVES2DATA *data);

/* Timed as V9X_TIME_D3D_CALLS; the work is in the body. */
DWORD __stdcall V9xD3dDrawPrimitives2(V9X_D3DHAL_DRAWPRIMITIVES2DATA *data)
{
    DWORD started = V9X_TIME_BEGIN();
    DWORD result = v9x_d3d_draw_primitives2_body(data);

    V9X_TIME_END(V9X_TIME_D3D_CALLS, started);
    return result;
}

static DWORD v9x_d3d_draw_primitives2_body(
    V9X_D3DHAL_DRAWPRIMITIVES2DATA *data)
{
    V9X_FPU_AREA fpu;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3D_CONTEXT *context;
    BYTE *commands;
    BYTE *vertices;
    V9X_DP2_STREAM stream;
    V9X_DP2_SINK sink;
    V9X_DP2_RESULT walked;
    V9X_D3D_DP2_USER user;
    V9X_D3D_LIST_SINK batch_sink;
    V9X_R3D_RECORDS run;

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.dp2_calls;
        v9x_hal->d3d_diagnostics.dp2_flags_seen |= data->dwFlags;
    }
    v9x_d3d_dp2_log("DP2 enter", data->dwFlags, data->dwCommandLength);
    v9x_fpu_save(&fpu);
    context = v9x_d3d_context_from_handle(data->dwhContext);

    /*
     * Nothing to draw with, or nothing to draw from. Answered DD_OK with
     * nothing drawn, which is what the DX5 entry points do for the same
     * conditions; an error here would stop an application over a frame.
     */
    if (data->dwVertexType != V9X_D3DFVF_TLVERTEX) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.dp2_refused_fvf;
            v9x_hal->d3d_diagnostics.dp2_fvf_last = data->dwVertexType;
        }
        data->dwErrorOffset = 0ul;
        data->ddrval = V9X_DD_OK;
        v9x_fpu_restore(&fpu);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    commands = v9x_d3d_dp2_memory(data->lpDDCommands);
    vertices = (data->dwFlags & V9X_D3DHALDP2_USERMEMVERTICES) != 0ul
                   ? (BYTE *)data->lpVertices
                   : v9x_d3d_dp2_memory(data->lpVertices);
    if (ops == 0 || context == 0 || !ops->ready() || commands == 0 ||
        (data->dwVertexLength != 0ul &&
         (vertices == 0 ||
          IsBadReadPtr(vertices + data->dwVertexOffset,
                       data->dwVertexLength * sizeof(V9X_D3DTLVERTEX)))) ||
        (data->dwCommandLength != 0ul &&
         IsBadReadPtr(commands + data->dwCommandOffset,
                      data->dwCommandLength))) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.dp2_refused_buffers;
        }
        data->dwErrorOffset = 0ul;
        data->ddrval = V9X_DD_OK;
        v9x_fpu_restore(&fpu);
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    stream.commands = commands + data->dwCommandOffset;
    stream.command_bytes = data->dwCommandLength;
    stream.vertices = vertices != 0 ? vertices + data->dwVertexOffset : 0;
    stream.vertex_count = vertices != 0 ? data->dwVertexLength : 0ul;
    stream.vertex_stride = sizeof(V9X_D3DTLVERTEX);

    batch_sink.ops = ops;
    batch_sink.context = context;
    run.vertices = (V9X_R3D_VERTEX *)v9x_d3d_gather;
    run.capacity = (v9x_u32)V9X_D3D_INDEXED_BATCH;
    run.pending = 0ul;
    run.record_count = 0ul;
    run.batch = v9x_d3d_records_batch;
    run.user = &batch_sink;

    user.context = context;
    user.run = &run;
    user.rstates = data->lpdwRStates;
    if (user.rstates != 0 &&
        IsBadWritePtr(user.rstates,
                      V9X_D3DHAL_MAX_RSTATES_DX6 * sizeof(DWORD))) {
        user.rstates = 0;
    }

    sink.user = &user;
    sink.render_state = v9x_d3d_dp2_render_state;
    sink.stage_state = v9x_d3d_dp2_stage_state;
    sink.list = v9x_d3d_dp2_list;
    sink.fan = v9x_d3d_dp2_fan;
    sink.triangle = v9x_d3d_dp2_triangle;

    v9x_dp2_walk(&stream, &sink, &walked);
    /*
     * A record this walker does not parse goes to the runtime's own parser
     * when it gave us one, and the walk resumes after it. The pending run
     * is drawn first so the order of the stream is kept. Bounded, so a
     * parser that keeps answering with the same record cannot hold the
     * Win16 lock forever; what is left over is handed back with
     * D3DERR_COMMAND_UNPARSED as before.
     */
    {
        DWORD rounds = 0ul;
        V9X_DP2_RESULT next;

        while (walked.status == V9X_DP2_UNPARSED &&
               v9x_d3d_parse_unknown != 0 && rounds < 64ul) {
            const v9x_u8 *at = stream.commands + walked.stop_offset;
            void *after = 0;
            DWORD parsed;
            DWORD resumed;

            ++rounds;
            (void)v9x_r3d_records_flush(&run);
            parsed = v9x_d3d_parse_unknown((void *)at, &after);
            v9x_d3d_dp2_log("DP2 parse", walked.stop_op, parsed);
            if (parsed != V9X_DD_OK || after == 0 ||
                (const v9x_u8 *)after <= at ||
                (const v9x_u8 *)after > stream.commands +
                                            stream.command_bytes) {
                break;
            }
            if (v9x_hal != 0) {
                ++v9x_hal->d3d_diagnostics.dp2_parsed_by_runtime;
            }
            resumed = (DWORD)((const v9x_u8 *)after - stream.commands);
            {
                V9X_DP2_STREAM rest = stream;

                rest.commands = stream.commands + resumed;
                rest.command_bytes = stream.command_bytes - resumed;
                v9x_dp2_walk(&rest, &sink, &next);
            }
            walked.records += next.records + 1ul;
            walked.triangles += next.triangles;
            walked.states += next.states;
            walked.stage_states += next.stage_states;
            walked.undrawn += next.undrawn;
            walked.ops_seen[0] |= next.ops_seen[0];
            walked.ops_seen[1] |= next.ops_seen[1];
            walked.status = next.status;
            walked.stop_op = next.stop_op;
            walked.stop_offset = resumed + next.stop_offset;
        }
    }
    (void)v9x_r3d_records_flush(&run);

    if (v9x_hal != 0) {
        V9X_D3D_DIAGNOSTICS *d = &v9x_hal->d3d_diagnostics;

        d->dp2_records += walked.records;
        d->dp2_triangles += walked.triangles;
        d->dp2_states += walked.states;
        d->dp2_stage_states += walked.stage_states;
        d->dp2_undrawn += walked.undrawn;
        d->dp2_ops_seen[0] |= walked.ops_seen[0];
        d->dp2_ops_seen[1] |= walked.ops_seen[1];
        ++d->render_primitive_calls;
        if (walked.status == V9X_DP2_UNPARSED) {
            ++d->dp2_unparsed;
            d->dp2_unparsed_op_last = walked.stop_op;
        } else if (walked.status == V9X_DP2_MALFORMED) {
            ++d->dp2_malformed;
            d->dp2_malformed_op_last = walked.stop_op;
        }
    }

    /*
     * An opcode the walker does not parse goes back to the runtime with its
     * offset, which is the interface's contract for an execute buffer's own
     * instructions: the runtime parses that record and calls again with the
     * rest. A malformed record is this call's own description - an index or
     * a count outside the buffers - and is answered as the DX5 indexed path
     * answers a bad index.
     */
    data->dwErrorOffset = data->dwCommandOffset + walked.stop_offset;
    if (walked.status == V9X_DP2_UNPARSED) {
        data->ddrval = V9X_D3DERR_COMMAND_UNPARSED;
    } else if (walked.status == V9X_DP2_MALFORMED) {
        data->ddrval = 0x80070057ul;
    } else {
        data->ddrval = V9X_DD_OK;
    }
    v9x_d3d_dp2_log("DP2 exit", data->ddrval, walked.records);
    v9x_fpu_restore(&fpu);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * ValidateTextureStageState: whether the current stage setup draws in one
 * pass. Required by this runtime before it keeps the DDI 6 table at all
 * (see v9x_guid_d3d_parse_unknown). One pass for the stage-0 combines the
 * DX5 blends express exactly - the ones the extended caps claim - and
 * D3DERR_UNSUPPORTEDCOLOROPERATION for the rest, which v9x_d3d_dp2_express_
 * stage would draw as MODULATE: an application that asks deserves the truth
 * rather than the approximation.
 */
DWORD __stdcall V9xD3dValidateTextureStageState(
    V9X_D3DHAL_VALIDATETEXTURESTAGESTATEDATA *data)
{
    V9X_D3D_CONTEXT *context;
    DWORD op;

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    context = v9x_d3d_context_from_handle(data->dwhContext);
    if (context == 0) {
        data->dwNumPasses = 0ul;
        data->ddrval = 0x80070057ul;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    op = context->stage_color_op;
    if (op == V9X_D3DTOP_DISABLE || op == V9X_D3DTOP_SELECTARG1 ||
        op == V9X_D3DTOP_SELECTARG2 || op == V9X_D3DTOP_MODULATE ||
        op == V9X_D3DTOP_BLENDTEXTUREALPHA) {
        data->dwNumPasses = 1ul;
        data->ddrval = V9X_DD_OK;
    } else {
        data->dwNumPasses = 0ul;
        data->ddrval = 0x88760819ul;
    }
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * The DX6 extended caps: the engine's nine DX5 fields, then what one texture
 * unit with the DX5 blends and no transform engine can say. Built per call
 * rather than kept in the shared block, so the block does not grow for an
 * answer that is a function of the nine.
 *
 * Guard band zero says the runtime must clip to the viewport, which is what
 * it has always done for this driver. dwFVFCaps zero is "TLVERTEX only".
 * dwMaxTextureRepeat and dvMaxVertexW are not limits measured on any card:
 * the first is the DDK samples' customary 2048, the second large enough
 * that no W-based depth is refused.
 */
static void v9x_d3d_extended_caps7(V9X_D3DHAL_D3DEXTENDEDCAPS7 *caps)
{
    BYTE *bytes = (BYTE *)caps;
    DWORD index;

    for (index = 0ul; index < sizeof(*caps); ++index) {
        bytes[index] = 0u;
    }
    caps->dx5 = v9x_hal->d3d_extended_caps;
    caps->dx5.dwSize = sizeof(*caps);
    caps->dwMaxTextureRepeat = 2048ul;
    caps->dwMaxTextureAspectRatio =
        (v9x_hal->d3d_global.hwCaps.dpcTriCaps.dwTextureCaps &
         V9X_D3DPTEXTURECAPS_SQUAREONLY) != 0ul ? 1ul : 0ul;
    caps->dwMaxAnisotropy = 1ul;
    caps->dwFVFCaps = 0ul;
    caps->dwTextureOpCaps = V9X_D3DTEXOPCAPS_DISABLE |
                            V9X_D3DTEXOPCAPS_SELECTARG1 |
                            V9X_D3DTEXOPCAPS_SELECTARG2 |
                            V9X_D3DTEXOPCAPS_MODULATE |
                            V9X_D3DTEXOPCAPS_BLENDTEXTUREALPHA;
    caps->wMaxTextureBlendStages = 1u;
    caps->wMaxSimultaneousTextures = 1u;
    caps->dvMaxVertexW = 1.0e10f;
}

/*
 * GUID_ZPixelFormats: a DWORD count, then that many DDPIXELFORMATs. The
 * engine's one depth format, which is all any engine here has - its width
 * from the engine limits the core already sizes Z surfaces by. The Z
 * fields share the RGB slots: bit depth, stencil depth, Z mask, stencil
 * mask.
 */
static DWORD v9x_d3d_z_pixel_formats(BYTE *out, DWORD room)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_publish_engine();
    DWORD bits = ops != 0 && ops->limits != 0
                     ? ops->limits->depth_bits_per_pixel : 0ul;
    DWORD answer[1 + sizeof(V9X_DDPIXELFORMAT) / sizeof(DWORD)];
    V9X_DDPIXELFORMAT *format = (V9X_DDPIXELFORMAT *)&answer[1];
    DWORD bytes;
    DWORD index;

    answer[0] = bits != 0ul ? 1ul : 0ul;
    format->dwSize = sizeof(V9X_DDPIXELFORMAT);
    format->dwFlags = V9X_DDPF_ZBUFFER;
    format->dwFourCC = 0ul;
    format->dwRGBBitCount = bits;
    format->dwRBitMask = 0ul;
    format->dwGBitMask = bits >= 32ul ? 0xfffffffful : (1ul << bits) - 1ul;
    format->dwBBitMask = 0ul;
    format->dwRGBAlphaBitMask = 0ul;
    bytes = answer[0] != 0ul ? sizeof(answer) : sizeof(DWORD);
    for (index = 0ul; out != 0 && index < bytes && index < room; ++index) {
        out[index] = ((const BYTE *)answer)[index];
    }
    return bytes;
}

/*
 * Sixteen bytes, compared. The callbacks2 path below walks its own GUID
 * inline and is left as it is; this exists because a second GUID makes the
 * comparison worth naming.
 */
static int v9x_d3d_guid_matches(const BYTE *asked, const BYTE *known)
{
    DWORD index;

    for (index = 0ul; index < 16ul; ++index) {
        if (asked[index] != known[index]) {
            return 0;
        }
    }

    return 1;
}

/*
 * Every distinct GUID the runtime asks for, by Data1.
 *
 * driver_info_last held only the most recent and the trace ring is bounded,
 * so the 2026-09-20 run could name three of eighteen calls. The table is not
 * a set of results - whether each was served is a separate question - only
 * of what was asked.
 */
static void v9x_d3d_note_driver_info_guid(DWORD data1)
{
    DWORD index;
    DWORD count = v9x_hal->d3d_diagnostics.driver_info_guid_count;

    for (index = 0ul; index < count && index < 16ul; ++index) {
        if (v9x_hal->d3d_diagnostics.driver_info_guids[index] == data1) {
            return;
        }
    }
    if (count < 16ul) {
        v9x_hal->d3d_diagnostics.driver_info_guids[count] = data1;
    }
    ++v9x_hal->d3d_diagnostics.driver_info_guid_count;
}

DWORD __stdcall V9xHalGetDriverInfo(V9X_DDHAL_GETDRIVERINFODATA *data)
{
#if V9X_C3_SERVE_D3D_CALLBACKS2
    DWORD index;
    DWORD bytes;
    BYTE *destination;
    const BYTE *source;
#endif

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_trace_enter(V9X_TRACE_GETDRIVERINFO,
                    ((DWORD)data->guidInfo[3] << 24) |
                    ((DWORD)data->guidInfo[2] << 16) |
                    ((DWORD)data->guidInfo[1] << 8) |
                    (DWORD)data->guidInfo[0]);
    data->dwActualSize = 0ul;
    data->ddRVal = 0x88760028ul;
    /*
     * What was asked for, and whether it was declined. This entry point
     * answers GUID_D3DCallbacks2 alone; intel95 raised the question of
     * whether an application's capability report depends on one of the
     * GUIDs it turns away, and nothing recorded which arrived.
     */
    if (v9x_hal != 0) {
        DWORD data1 = ((DWORD)data->guidInfo[3] << 24) |
                      ((DWORD)data->guidInfo[2] << 16) |
                      ((DWORD)data->guidInfo[1] << 8) |
                      (DWORD)data->guidInfo[0];

        ++v9x_hal->d3d_diagnostics.driver_info_calls;
        v9x_hal->d3d_diagnostics.driver_info_last = data1;
        v9x_d3d_note_driver_info_guid(data1);
        v9x_d3d_dp2_log("GDI", data1, data->dwExpectedSize);
    }

    /*
     * The DDI 6 answers, when the setting asks for them. The device caps'
     * DrawPrimitives2 bit moves with the same decision on every call, so a
     * runtime never sees the bit without the callbacks or the reverse:
     * the setting is read per driver object on the 16-bit side, and the
     * runtime asks for these after that.
     */
    v9x_d3d_apply_ddi_level();
    if (v9x_d3d_dp2_enabled() &&
        v9x_d3d_guid_matches(data->guidInfo, v9x_guid_d3d_callbacks3)) {
        DWORD copy = sizeof(v9x_d3d_callbacks3);
        DWORD at;
        V9X_D3DHAL_CALLBACKS3 answer = v9x_d3d_callbacks3;

        /* Probe bit 2: the table with no DrawPrimitives2 in it. */
        if ((v9x_d3d_dp2_probe() & V9X_DP2_PROBE_NO_DP2_ENTRY) != 0ul) {
            answer.dwFlags = 0ul;
            answer.DrawPrimitives2 = 0;
        }
        if (data->dwExpectedSize < copy) {
            copy = data->dwExpectedSize;
        }
        data->dwActualSize = sizeof(v9x_d3d_callbacks3);
        if (data->lpvData != 0 && copy != 0ul) {
            for (at = 0ul; at < copy; ++at) {
                ((BYTE *)data->lpvData)[at] = ((const BYTE *)&answer)[at];
            }
            data->ddRVal = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.dp2_callbacks3_served;
        }
        v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (v9x_d3d_dp2_enabled() &&
        v9x_d3d_guid_matches(data->guidInfo, v9x_guid_d3d_parse_unknown)) {
        /* Here lpvData is not a buffer to fill but the parser itself. */
        data->dwActualSize = 0ul;
        if (data->lpvData != 0 &&
            !IsBadCodePtr((FARPROC)data->lpvData)) {
            v9x_d3d_parse_unknown = (V9X_D3D_PARSE_UNKNOWN_FN)data->lpvData;
        }
        data->ddRVal = V9X_DD_OK;
        v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (v9x_d3d_dp2_enabled() &&
        v9x_d3d_guid_matches(data->guidInfo, v9x_guid_z_pixel_formats)) {
        data->dwActualSize = v9x_d3d_z_pixel_formats(0, 0ul);
        if (data->lpvData != 0 && data->dwExpectedSize != 0ul) {
            (void)v9x_d3d_z_pixel_formats((BYTE *)data->lpvData,
                                          data->dwExpectedSize);
            data->ddRVal = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.dp2_zformats_served;
        }
        v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    /*
     * GUID_D3DExtendedCaps, which the runtime asks for and this driver
     * refused until 2026-09-20.
     *
     * The answer comes from the shared block, where the engine that
     * published the device description put its own texture limits; the
     * handler cannot ask v9x_d3d_engine() because it may run before the
     * engine descriptor is filled, which is the same reason
     * v9x_d3d_publish_engine exists.
     *
     * An engine that filled nothing leaves dwSize zero and is declined
     * rather than answered with zeros: a maximum texture width of zero is a
     * worse answer than no answer, and the runtime's own default is at
     * least a working one.
     */
    if (v9x_hal != 0 &&
        v9x_hal->d3d_extended_caps.dwSize != 0ul &&
        v9x_d3d_guid_matches(data->guidInfo, v9x_guid_d3d_extended_caps)) {
        /* At DDI 6 the DX6 and DX7 fields follow the nine: a DrawPrimitives2
         * runtime reads the blend stages, texture ops and FVF caps from
         * them. Otherwise the nine alone, exactly as before. */
        int dx6 = v9x_d3d_dp2_enabled();
        V9X_D3DHAL_D3DEXTENDEDCAPS7 caps7;
        DWORD size = dx6 ? sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS7)
                         : sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
        DWORD copy = size;
        DWORD index;
        BYTE *destination;
        const BYTE *source;

        if (dx6) {
            v9x_d3d_extended_caps7(&caps7);
        }
        if (data->dwExpectedSize < copy) {
            copy = data->dwExpectedSize;
        }
        data->dwActualSize = size;
        if (data->lpvData != 0 && copy != 0ul) {
            destination = (BYTE *)data->lpvData;
            source = dx6 ? (const BYTE *)&caps7
                         : (const BYTE *)&v9x_hal->d3d_extended_caps;
            for (index = 0ul; index < copy; ++index) {
                destination[index] = source[index];
            }
            data->ddRVal = V9X_DD_OK;
        }
        v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
#if V9X_C3_SERVE_D3D_CALLBACKS2
    for (index = 0ul; index < 16ul; ++index) {
        if (data->guidInfo[index] != v9x_guid_d3d_callbacks2[index]) {
            if (v9x_hal != 0) {
                ++v9x_hal->d3d_diagnostics.driver_info_declined;
            }
            v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    bytes = data->dwExpectedSize < sizeof(v9x_d3d_callbacks2)
        ? data->dwExpectedSize : sizeof(v9x_d3d_callbacks2);
    data->dwActualSize = sizeof(v9x_d3d_callbacks2);
    if (data->lpvData != 0) {
        v9x_d3d_callbacks2.dwSize = bytes;
        destination = (BYTE *)data->lpvData;
        source = (const BYTE *)&v9x_d3d_callbacks2;
        for (index = 0ul; index < bytes; ++index) {
            destination[index] = source[index];
        }
        data->ddRVal = V9X_DD_OK;
    }
#endif
    if (v9x_hal != 0 && data->ddRVal != V9X_DD_OK) {
        ++v9x_hal->d3d_diagnostics.driver_info_declined;
    }
    v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * The engine whose caps this binary publishes at DriverInit.
 *
 * The same selector as v9x_d3d_engine(), without its validity pre-check:
 * the software capability must be honoured on a chip whose descriptor names
 * no engine. Until 2026-09-28 this defaulted to the ViRGE, because
 * engine_type was unreadable at DriverInit (the 2026-08-29 core/engine
 * split record measured D3DHalFound 1 -> 0 when it selected on the unset
 * field). dd16.c's v9x_dd_stamp_engine_caps now stamps engine_type and
 * V9X_DD_ENGINE_VALID on the DDGET32BITDRIVERNAME escape, before
 * DriverInit, for every chip with an engine descriptor, so the choice is
 * the chip's own. A chip with no D3D engine in this binary publishes no
 * caps rather than the ViRGE's.
 */
static const V9X_D3D_ENGINE_OPS *v9x_d3d_publish_engine(void)
{
    if (v9x_hal == 0) {
        return 0;
    }
    return v9x_d3d_selected_ops(
        v9x_d3d_select_engine(
            (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul,
            v9x_hal->engine.engine_type, v9x_hal->engine.engine_caps));
}

/*
 * Publish the D3D tables into the shared block.
 *
 * The callbacks are this file's own entry points, so they are wired here; the
 * caps and the texture-format list come from the engine. Publishing them for a
 * chip that cannot serve them is safe and is what the driver has always done:
 * the 16-bit side nulls GetDriverInfo and both lpD3D* pointers for a family
 * whose engine_caps lack D3D, so DDRAW never reaches any of it, and every
 * entry point above independently declines when v9x_d3d_engine() resolves
 * nothing at call time.
 */
void v9x_d3d_publish(V9X_DD_SHARED *shared)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_publish_engine();

    /* No engine, no caps. The callbacks below are still wired: each one
     * declines when v9x_d3d_engine() resolves nothing at call time. */
    if (ops != 0) {
        ops->describe_caps(shared);
    }

    /*
     * THE TEXTURE ALIGNMENT DirectDraw will honour, corrected here to the
     * engine's own.
     *
     * ddhal_core.c publishes vmiData.dwTextureAlign as a flat 8 before this
     * runs - the ViRGE's requirement, written once for every chip because
     * there was one chip that sampled. Gen3's MAP_STATE address is page
     * aligned, so every texture DirectDraw handed back at an eight-byte
     * boundary was refused at bind time and its triangles drew untextured:
     * a wrong picture, no error, and a promise DirectDraw had been told it
     * could keep.
     *
     * Set from the resolved engine rather than from the chip, because the
     * engine is what binds the surface - and left alone when the engine
     * states no requirement, so a chip that samples nothing keeps the
     * core's answer.
     */
    if (ops != 0 && ops->limits != 0 && ops->limits->texture_align != 0ul) {
        shared->info.vmiData.dwTextureAlign = ops->limits->texture_align;
    }

    shared->d3d_callbacks.dwSize = sizeof(V9X_D3DHAL_CALLBACKS);
    shared->d3d_callbacks.ContextCreate =
        (V9X_DD_CODE_PTR)V9xD3dContextCreate;
    shared->d3d_callbacks.ContextDestroy =
        (V9X_DD_CODE_PTR)V9xD3dContextDestroy;
    shared->d3d_callbacks.ContextDestroyAll =
        (V9X_DD_CODE_PTR)V9xD3dContextDestroyAll;
    shared->d3d_callbacks.Execute = 0;
    shared->d3d_callbacks.ExecuteClipped = 0;
    shared->d3d_callbacks.RenderState =
        (V9X_DD_CODE_PTR)V9xD3dRenderState;
    shared->d3d_callbacks.RenderPrimitive =
        (V9X_DD_CODE_PTR)V9xD3dRenderPrimitive;
    shared->d3d_callbacks.TextureCreate =
        (V9X_DD_CODE_PTR)V9xD3dTextureCreate;
    shared->d3d_callbacks.TextureDestroy =
        (V9X_DD_CODE_PTR)V9xD3dTextureDestroy;
    shared->d3d_callbacks.TextureSwap =
        (V9X_DD_CODE_PTR)V9xD3dTextureSwap;
    shared->d3d_callbacks.TextureGetSurf =
        (V9X_DD_CODE_PTR)V9xD3dTextureGetSurf;

    v9x_d3d_callbacks2.dwSize = sizeof(V9X_D3DHAL_CALLBACKS2);
    v9x_d3d_callbacks2.dwFlags =
        V9X_D3DHAL2_CB32_SETRENDERTARGET |
        V9X_D3DHAL2_CB32_DRAWONEPRIMITIVE |
        V9X_D3DHAL2_CB32_DRAWONEINDEXEDPRIMITIVE |
        V9X_D3DHAL2_CB32_DRAWPRIMITIVES;
    v9x_d3d_callbacks2.SetRenderTarget =
        (V9X_DD_CODE_PTR)V9xD3dSetRenderTarget;
    v9x_d3d_callbacks2.DrawOnePrimitive =
        (V9X_DD_CODE_PTR)V9xD3dDrawOnePrimitive;
    v9x_d3d_callbacks2.DrawOneIndexedPrimitive =
        (V9X_DD_CODE_PTR)V9xD3dDrawOneIndexedPrimitive;
    v9x_d3d_callbacks2.DrawPrimitives =
        (V9X_DD_CODE_PTR)V9xD3dDrawPrimitives;

    /* Served only at DDI 6; see V9xHalGetDriverInfo. Clear2 is the DX6
     * driver's clear (see V9xD3dClear2). ValidateTextureStageState is
     * required by this runtime (see v9x_guid_d3d_parse_unknown), and every
     * pointer needs its flag bit: DDRAW.DLL checks the slots against
     * dwFlags one bit each. */
    v9x_d3d_callbacks3.dwSize = sizeof(V9X_D3DHAL_CALLBACKS3);
    v9x_d3d_callbacks3.dwFlags = V9X_D3DHAL3_CB32_CLEAR2 |
                                 V9X_D3DHAL3_CB32_VALIDATETEXTURESTAGESTATE |
                                 V9X_D3DHAL3_CB32_DRAWPRIMITIVES2;
    v9x_d3d_callbacks3.Clear2 = (V9X_DD_CODE_PTR)V9xD3dClear2;
    v9x_d3d_callbacks3.lpvReserved = 0;
    v9x_d3d_callbacks3.ValidateTextureStageState =
        (V9X_DD_CODE_PTR)V9xD3dValidateTextureStageState;
    v9x_d3d_callbacks3.DrawPrimitives2 =
        (V9X_DD_CODE_PTR)V9xD3dDrawPrimitives2;
    v9x_d3d_apply_ddi_level();
}

/*
 * The render interface, version 1 (include\velocity9x\r3d_abi.h;
 * docs\plans\opengl-1.1-icd.md, Phase 3).
 *
 * A second front end on the same engines, and it lives here rather than in
 * a file of its own for one reason: its surfaces are resolved by
 * v9x_d3d_set_target, the guarded INT -> LCL -> GBL path and every engine
 * rule a Direct3D render target is held to, and its batches go through the
 * same neutral list builder into ops->draw. Moving those out would make
 * them external for the sake of a file boundary. What it does not share is
 * a context: every request is described afresh on a scratch one, under the
 * Win16 mutex, and nothing from it is kept.
 *
 * Every request is an explicit draw (r3d.h): its CPU texture levels,
 * scissor, write mask, full blend factor set, alpha test and fog are all
 * stated, and each engine's accepts() refuses what that engine cannot draw
 * exactly - UNSUPPORTED, never an approximation. The software engine draws
 * all of it; the hardware engines take untextured and DirectDraw-texture
 * batches whose state they express. A DirectDraw texture's combine must be
 * one a D3D texture op is, or it is UNSUPPORTED.
 */

typedef char v9x_d3d_assert_r3d_abi_vertex[
    (sizeof(V9X_R3D_ABI_VERTEX) == sizeof(V9X_R3D_VERTEX) &&
     offsetof(V9X_R3D_ABI_VERTEX, rhw) == offsetof(V9X_R3D_VERTEX, rhw) &&
     offsetof(V9X_R3D_ABI_VERTEX, specular) ==
         offsetof(V9X_R3D_VERTEX, specular) &&
     offsetof(V9X_R3D_ABI_VERTEX, tv) == offsetof(V9X_R3D_VERTEX, tv))
        ? 1 : -1];

typedef char v9x_d3d_assert_r3d_abi_numbers[
    (V9X_R3D_ABI_FORMAT_RGB565 == V9X_D3D_TARGET_FORMAT_RGB565 &&
     V9X_R3D_ABI_FORMAT_XRGB1555 == V9X_D3D_TARGET_FORMAT_XRGB1555 &&
     V9X_R3D_ABI_FORMAT_RGB565 == V9X_R3D_FORMAT_RGB565 &&
     V9X_R3D_ABI_FORMAT_XRGB1555 == V9X_R3D_FORMAT_XRGB1555 &&
     V9X_R3D_ABI_ADDRESS_WRAP == V9X_R3D_ADDRESS_WRAP &&
     V9X_R3D_ABI_ADDRESS_CLAMP == V9X_R3D_ADDRESS_CLAMP &&
     V9X_R3D_ABI_FILTER_NEAREST == V9X_R3D_FILTER_NEAREST &&
     V9X_R3D_ABI_FILTER_LINEAR == V9X_R3D_FILTER_LINEAR) ? 1 : -1];

/* The current session. Starts at one so a zeroed request is always STALE. */
static DWORD v9x_r3d_generation = 1ul;
/* Scratch for one request's surfaces; valid only under the mutex. */
static V9X_D3D_CONTEXT v9x_r3d_context;
/* And for its CPU texture levels, likewise. */
static V9X_R3D_LEVEL v9x_r3d_levels[V9X_R3D_ABI_LEVELS_MAX];
/* And for the second unit's. */
static V9X_R3D_LEVEL v9x_r3d_levels1[V9X_R3D_ABI_LEVELS_MAX];

void v9x_d3d_render_new_session(void)
{
    ++v9x_r3d_generation;
    if (v9x_r3d_generation == 0ul) {
        v9x_r3d_generation = 1ul;
    }
}

/* The HAL links no C runtime, and a struct assignment may become a call to
 * one; a byte loop cannot. */
static void v9x_r3d_zero(void *memory, DWORD bytes)
{
    BYTE *cursor = (BYTE *)memory;

    while (bytes-- != 0ul) {
        *cursor++ = 0u;
    }
}

/* The engine that would draw, ready to, or null. */
static const V9X_D3D_ENGINE_OPS *v9x_r3d_engine(void)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();

    if (v9x_hal == 0 || ops == 0 || ops->ready == 0 || !ops->ready() ||
        ops->draw == 0) {
        return 0;
    }
    return ops;
}

/*
 * The engine a refused draw falls back to, or null when it has none.
 *
 * The plan's answer to a capability refusal is the software engine on the
 * same target and depth surfaces, after the shared drain. That is only
 * sound where the hardware engine and the CPU are measured to agree on the
 * colour and depth encodings and on ordering in a shared target: Phase 0.5
 * measured it for Gen3, all three cells passing
 * (docs\decisions\2026-09-26-phase05-mixed-engine-ordering-and-virge-
 * colour-mismatch.md). The ViRGE is not included. Its S3D writes 1555 into
 * a 565 target, and its 555 target has not been measured, so a mixed frame
 * there could carry two encodings.
 */
static const V9X_D3D_ENGINE_OPS *v9x_r3d_fallback(
    const V9X_D3D_ENGINE_OPS *ops)
{
    return ops == &v9x_d3d_engine_i9xx ? &v9x_d3d_engine_soft : 0;
}

/* The largest texture a draw may name: the fallback's when there is one,
 * since a texture the hardware refuses for its size is then drawn by it. */
static v9x_u32 v9x_r3d_texture_size_max(const V9X_D3D_ENGINE_OPS *ops)
{
    const V9X_D3D_ENGINE_OPS *fallback = v9x_r3d_fallback(ops);

    if (fallback != 0 &&
        fallback->limits->texture_size_max > ops->limits->texture_size_max) {
        return fallback->limits->texture_size_max;
    }
    return ops->limits->texture_size_max;
}

/*
 * Bind target and depth on the scratch context through the Direct3D
 * target path. INVALID when either is refused; the depth refusal reason is
 * recorded where Direct3D's is (d3d_diagnostics.depth_reject), which is
 * shared bookkeeping, not state.
 */
static DWORD v9x_r3d_bind(void *target, void *depth)
{
    v9x_r3d_zero(&v9x_r3d_context, sizeof(v9x_r3d_context));
    if (!v9x_d3d_set_target(&v9x_r3d_context, target, depth)) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (depth != 0 && v9x_r3d_context.zbuffer == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    return V9X_R3D_RESULT_OK;
}

/*
 * The texture combine as the D3D texture op the engines implement, or zero
 * when none is exactly it. D3D MODULATE takes the texel's alpha when the
 * texture has one and the fragment's when it does not, so which ABI alpha op
 * it is depends on the format.
 */
static DWORD v9x_r3d_texture_op(const V9X_R3D_ABI_TEXTURE *texture)
{
    int has_alpha = texture->format != V9X_R3D_ABI_FORMAT_RGB565;

    if (texture->color_op == V9X_R3D_ABI_COLOROP_REPLACE &&
        texture->alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE) {
        return V9X_R3D_TEXOP_DECAL;
    }
    if (texture->color_op == V9X_R3D_ABI_COLOROP_MODULATE) {
        if (texture->alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE) {
            return V9X_R3D_TEXOP_MODULATEALPHA;
        }
        if (texture->alpha_op == (has_alpha ? V9X_R3D_ABI_ALPHAOP_REPLACE
                                            : V9X_R3D_ABI_ALPHAOP_FRAGMENT)) {
            return V9X_R3D_TEXOP_MODULATE;
        }
        return 0ul;
    }
    if (texture->color_op == V9X_R3D_ABI_COLOROP_DECALALPHA &&
        texture->alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
        return V9X_R3D_TEXOP_DECALALPHA;
    }
    return 0ul;
}

/* D3D's six TEXTUREMIN values from a texel filter and a level filter. */
static DWORD v9x_r3d_min_filter(const V9X_R3D_ABI_TEXTURE *texture)
{
    int linear = texture->min_filter == V9X_R3D_ABI_FILTER_LINEAR;

    if (texture->mip == V9X_R3D_ABI_MIP_POINT) {
        return linear ? V9X_R3D_FILTER_MIPLINEAR : V9X_R3D_FILTER_MIPNEAREST;
    }
    if (texture->mip == V9X_R3D_ABI_MIP_LINEAR) {
        return linear ? V9X_R3D_FILTER_LINEARMIPLINEAR
                      : V9X_R3D_FILTER_LINEARMIPNEAREST;
    }
    return texture->min_filter;
}

/*
 * One unit's texture as the neutral core describes it, into `texture`, its
 * CPU levels into `levels`. A one-unit draw's surface texture must be a D3D
 * texture op, which is what the one-unit engines implement; on a two-unit
 * draw the engine reads color_op/alpha_op/env_color for both units, so they
 * are carried whatever the storage and `op` is not needed.
 */
static DWORD v9x_r3d_describe_texture(const V9X_R3D_ABI_TEXTURE *source,
                                      int two_units,
                                      V9X_R3D_LEVEL *levels,
                                      V9X_R3D_TEXTURE *texture)
{
    if (source->storage == V9X_R3D_ABI_TEXTURE_CPU) {
        DWORD level;

        /* The levels array, then each level's declared storage, proven
         * readable before any engine samples it: a bad pointer here would
         * fault inside the HAL with the Win16 mutex held. The validator has
         * already bounded count, sizes and extents arithmetically. */
        if (IsBadReadPtr(source->levels,
                         source->level_count * sizeof(V9X_R3D_ABI_LEVEL))) {
            return V9X_R3D_RESULT_INVALID;
        }
        for (level = 0ul; level < source->level_count; ++level) {
            const V9X_R3D_ABI_LEVEL *from = &source->levels[level];

            if (IsBadReadPtr(from->pixels, from->bytes)) {
                return V9X_R3D_RESULT_INVALID;
            }
            levels[level].pixels = from->pixels;
            levels[level].pitch = from->pitch;
            levels[level].width = from->width;
            levels[level].height = from->height;
        }
        texture->levels = levels;
        texture->level_count = source->level_count;
        texture->format = source->format;
        texture->mip = source->mip;
        texture->color_op = source->color_op;
        texture->alpha_op = source->alpha_op;
        texture->env_color = source->env_color;
        texture->min_filter = v9x_r3d_min_filter(source);
        texture->mag_filter = source->mag_filter;
        texture->address = source->address;
    }
    if (source->storage == V9X_R3D_ABI_TEXTURE_HW) {
        texture->object = v9x_d3d_surface_lcl(source->surface.surface,
                                              V9X_D3D_LCL_SITE_R3D_TEXTURE);
        if (texture->object == 0) {
            return V9X_R3D_RESULT_INVALID;
        }
        if (two_units) {
            texture->format = source->format;
            texture->color_op = source->color_op;
            texture->alpha_op = source->alpha_op;
            texture->env_color = source->env_color;
        } else {
            texture->op = v9x_r3d_texture_op(source);
            if (texture->op == 0ul) {
                return V9X_R3D_RESULT_UNSUPPORTED;
            }
        }
        texture->min_filter = v9x_r3d_min_filter(source);
        texture->mag_filter = source->mag_filter;
        texture->address = source->address;
    }
    return V9X_R3D_RESULT_OK;
}

/* The request as the neutral core describes a batch. */
static DWORD v9x_r3d_describe(const V9X_R3D_ABI_DRAW *request,
                              V9X_R3D_DRAW *draw)
{
    const V9X_R3D_ABI_STATE *state = &request->state;
    const V9X_D3D_CONTEXT *context = &v9x_r3d_context;
    int two_units = request->texcoords1 != 0;
    DWORD result;

    v9x_r3d_zero(draw, sizeof(*draw));
    draw->target.offset = context->target_offset;
    draw->target.pitch = context->pitch;
    draw->target.width = context->width;
    draw->target.height = context->height;
    draw->target.format = context->target_format;
    draw->target.object = context->target;
    draw->depth.offset = context->depth_offset;
    draw->depth.pitch = context->depth_pitch;
    draw->depth.width = context->width;
    draw->depth.height = context->height;
    draw->depth.object = context->zbuffer;

    result = v9x_r3d_describe_texture(&request->texture, two_units,
                                      v9x_r3d_levels, &draw->texture);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    if (two_units) {
        result = v9x_r3d_describe_texture(&request->texture1, two_units,
                                          v9x_r3d_levels1, &draw->texture1);
        if (result != V9X_R3D_RESULT_OK) {
            return result;
        }
        draw->texcoords1 = request->texcoords1;
    }

    /* Everything the request states, stated: each engine's accepts()
     * refuses what it cannot draw exactly (the software engine draws all of
     * it; the hardware engines refuse CPU levels, a scissor and a mask). */
    draw->explicit_state = 1ul;
    draw->scissor_left = state->scissor_left;
    draw->scissor_top = state->scissor_top;
    draw->scissor_right = state->scissor_right;
    draw->scissor_bottom = state->scissor_bottom;
    draw->write_mask = state->write_mask;

    draw->depth_enable = state->depth_enable != 0ul && context->zbuffer != 0
        ? 1ul : 0ul;
    draw->depth_write = draw->depth_enable != 0ul ? state->depth_write : 0ul;
    draw->depth_func = draw->depth_enable != 0ul ? state->depth_func
                                                 : V9X_R3D_CMP_ALWAYS;
    draw->blend_enable = state->blend_enable != 0ul ? 1ul : 0ul;
    draw->src_blend = draw->blend_enable != 0ul ? state->src_blend
                                                : V9X_R3D_BLEND_ONE;
    draw->dst_blend = draw->blend_enable != 0ul ? state->dst_blend
                                                : V9X_R3D_BLEND_ZERO;
    draw->alpha_test_enable = state->alpha_test_enable != 0ul ? 1ul : 0ul;
    draw->alpha_func = draw->alpha_test_enable != 0ul ? state->alpha_func
                                                      : V9X_R3D_CMP_ALWAYS;
    draw->alpha_ref = state->alpha_ref;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->fog_enable = state->fog_enable != 0ul ? 1ul : 0ul;
    draw->fog_color = state->fog_color;
    return V9X_R3D_RESULT_OK;
}

/* The list builder's sink: every batch to the engine, counting what went. */
typedef struct v9x_r3d_sink {
    const V9X_D3D_ENGINE_OPS *ops;
    const V9X_R3D_DRAW *draw;
    DWORD submitted;
} V9X_R3D_SINK;

static int v9x_r3d_sink_batch(void *user, const V9X_R3D_VERTEX *vertices,
                              v9x_u32 triangle_count)
{
    V9X_R3D_SINK *sink = (V9X_R3D_SINK *)user;

    if (!sink->ops->draw(sink->draw, vertices, triangle_count)) {
        return 0;
    }
    sink->submitted += triangle_count;
    return 1;
}

/* The ICD culls in window space before it sends anything. */
static int v9x_r3d_sink_culled(void *user, const V9X_R3D_VERTEX *triangle)
{
    (void)user;
    (void)triangle;
    return 0;
}

static DWORD v9x_r3d_drain(void);

/* Every vertex of a request with alpha 255: V9X_R3D_DRAW.vertex_alpha_opaque.
 * The vertices were proven readable by the caller. */
static v9x_u32 v9x_r3d_vertices_opaque(const V9X_R3D_ABI_VERTEX *vertices,
                                       v9x_u32 triangle_count)
{
    v9x_u32 index;

    if (vertices == 0 || triangle_count == 0ul) {
        return 0ul;
    }
    for (index = 0ul; index < triangle_count * 3ul; ++index) {
        if ((vertices[index].color & 0xFF000000ul) != 0xFF000000ul) {
            return 0ul;
        }
    }
    return 1ul;
}

/* Textures the engine combines in one render-interface draw; an engine that
 * leaves the limit zero has one. */
static DWORD v9x_r3d_texture_units(const V9X_D3D_ENGINE_OPS *ops)
{
    return ops->limits->texture_units > 1ul ? ops->limits->texture_units : 1ul;
}

/*
 * A two-unit draw, straight to the engine. The list builder is not used: it
 * clips, culls and stages, any of which would part a vertex from its entry
 * in texcoords1, and the ICD has already done all three to the drawable.
 * What the builder would refuse is refused here the same way - a coordinate
 * past the engine's guard band or not finite. An engine that needs its
 * triangles on the target (clip_in_core) gets no vertex more than a pixel
 * off it: rounding of the ICD's own clip is inside that, and anything
 * further is a caller that did not clip, whose triangle the engine would
 * otherwise draw with a moved edge.
 */
#define V9X_R3D_ON_TARGET_SLACK 1.0f

static DWORD v9x_r3d_draw_two_units(const V9X_D3D_ENGINE_OPS *ops,
                                    const V9X_R3D_DRAW *draw,
                                    const V9X_R3D_ABI_DRAW *request,
                                    V9X_R3D_ABI_OUTCOME *outcome)
{
    const V9X_R3D_VERTEX *vertices = (const V9X_R3D_VERTEX *)request->vertices;
    float limit = ops->limits->coordinate_limit;
    float left = -limit;
    float top = -limit;
    float right = limit;
    float bottom = limit;
    DWORD index;

    if (ops->limits->clip_in_core != 0ul) {
        left = -V9X_R3D_ON_TARGET_SLACK;
        top = -V9X_R3D_ON_TARGET_SLACK;
        right = (float)v9x_r3d_context.width + V9X_R3D_ON_TARGET_SLACK;
        bottom = (float)v9x_r3d_context.height + V9X_R3D_ON_TARGET_SLACK;
    }
    for (index = 0ul; index < request->triangle_count * 3ul; ++index) {
        /* Written so a NaN fails both compares and is refused. */
        if (!(vertices[index].sx >= left && vertices[index].sx <= right) ||
            !(vertices[index].sy >= top && vertices[index].sy <= bottom)) {
            return V9X_R3D_RESULT_INVALID;
        }
    }
    outcome->submitted = 0ul;
    if (!ops->draw(draw, vertices, request->triangle_count)) {
        return V9X_R3D_RESULT_INDETERMINATE;
    }
    outcome->submitted = request->triangle_count;
    return V9X_R3D_RESULT_OK;
}

static DWORD v9x_r3d_draw_body(const V9X_R3D_ABI_DRAW *request,
                               V9X_R3D_ABI_OUTCOME *outcome)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_r3d_engine();
    V9X_R3D_DRAW draw;
    V9X_R3D_LIST list;
    V9X_R3D_SINK sink;
    DWORD result;

    if (ops == 0) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    result = v9x_r3d_validate_draw(request, v9x_r3d_generation,
                                   v9x_r3d_texture_size_max(ops));
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    if (IsBadReadPtr(request->vertices,
                     request->triangle_count * 3ul *
                         sizeof(V9X_R3D_ABI_VERTEX))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (request->texcoords1 != 0) {
        /* Describe said one unit to the ICD, so this is a caller that did
         * not ask; nothing here could draw it exactly. */
        if (v9x_r3d_texture_units(ops) < 2ul) {
            return V9X_R3D_RESULT_UNSUPPORTED;
        }
        if (IsBadReadPtr(request->texcoords1,
                         request->triangle_count * 3ul * 2ul *
                             sizeof(float))) {
            return V9X_R3D_RESULT_INVALID;
        }
    }
    result = v9x_r3d_bind(request->target.surface, request->depth.surface);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    result = v9x_r3d_validate_state(&request->state, v9x_r3d_context.width,
                                    v9x_r3d_context.height);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    result = v9x_r3d_describe(request, &draw);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    draw.vertex_alpha_opaque =
        v9x_r3d_vertices_opaque(request->vertices, request->triangle_count);
    if (ops->accepts == 0 || !ops->accepts(&draw)) {
        /* Refused before anything was emitted, so the fallback draws the
         * whole request or nothing. The drain comes first: the CPU must
         * not write the target or read the depth buffer while the engine
         * still owes either. The engine then sees the CPU's writes because
         * its next submission is an uncached register write, which empties
         * the processor's write-combining buffers ahead of it. */
        const V9X_D3D_ENGINE_OPS *fallback = v9x_r3d_fallback(ops);

        /* A surface texture stays with its engine: Gen3 places a mip chain
         * as one tree in its own layout, which the software engine's
         * surface sampler (one linear level) would read wrongly. The ICD
         * answers UNSUPPORTED by sending its CPU copy instead. */
        if (fallback == 0 || draw.texture.object != 0 ||
            draw.texture1.object != 0 ||
            (draw.texcoords1 != 0 && v9x_r3d_texture_units(fallback) < 2ul) ||
            !fallback->accepts(&draw)) {
            return V9X_R3D_RESULT_UNSUPPORTED;
        }
        result = v9x_r3d_drain();
        if (result != V9X_R3D_RESULT_OK) {
            return result;
        }
        ops = fallback;
    }

    if (draw.texcoords1 != 0) {
        return v9x_r3d_draw_two_units(ops, &draw, request, outcome);
    }

    sink.ops = ops;
    sink.draw = &draw;
    sink.submitted = 0ul;
    list.guard_limit = ops->limits->coordinate_limit;
    list.width = (float)v9x_r3d_context.width;
    list.height = (float)v9x_r3d_context.height;
    list.clip_in_core = ops->limits->clip_in_core;
    list.batch = v9x_r3d_sink_batch;
    list.culled = v9x_r3d_sink_culled;
    list.user = &sink;
    list.staging = 0;
    list.staging_triangles = 0ul;
    list.stats = 0;
    if (v9x_r3d_draw_list(&list, (const V9X_R3D_VERTEX *)request->vertices,
                          request->triangle_count)) {
        outcome->submitted = sink.submitted;
        return V9X_R3D_RESULT_OK;
    }
    /* An engine that declines a batch does not say how much of it reached
     * the hardware, and a triangle refused by the guard band ends nothing
     * but itself; either way the target's contents are no longer known. */
    outcome->submitted = sink.submitted;
    return V9X_R3D_RESULT_INDETERMINATE;
}

/* The shared drain to completion: DONE, or the one failure it can end in. */
static DWORD v9x_r3d_drain(void)
{
    int drained = v9x_render_drain(1);

    while (drained == V9X_RENDER_DRAIN_BUSY) {
        drained = v9x_render_drain(1);
    }
    return drained == V9X_RENDER_DRAIN_DONE ? V9X_R3D_RESULT_OK
                                            : V9X_R3D_RESULT_TIMEOUT;
}

static DWORD v9x_r3d_clear_body(const V9X_R3D_ABI_CLEAR *request)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_r3d_engine();
    V9X_R3D_CLEAR clear;
    DWORD result;

    if (ops == 0) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    result = v9x_r3d_validate_clear(request, v9x_r3d_generation);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    if (request->rect_count != 0ul &&
        IsBadReadPtr(request->rects,
                     request->rect_count * sizeof(V9X_R3D_ABI_RECT))) {
        return V9X_R3D_RESULT_INVALID;
    }
    result = v9x_r3d_bind(request->target.surface,
                          request->clear_depth != 0ul ? request->depth.surface
                                                      : 0);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    result = v9x_r3d_validate_rects(request->rects, request->rect_count,
                                    v9x_r3d_context.width,
                                    v9x_r3d_context.height);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    /* The CPU writes both surfaces; anything the engine still owes them has
     * to land first, or it would land on top of the clear. */
    result = v9x_r3d_drain();
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    clear.color = (void *)(v9x_hal->fb.linear_base +
                           v9x_r3d_context.target_offset);
    clear.color_pitch = v9x_r3d_context.pitch;
    clear.depth = v9x_r3d_context.zbuffer != 0
        ? (void *)(v9x_hal->fb.linear_base + v9x_r3d_context.depth_offset)
        : 0;
    clear.depth_pitch = v9x_r3d_context.depth_pitch;
    clear.width = v9x_r3d_context.width;
    clear.height = v9x_r3d_context.height;
    clear.format = v9x_r3d_context.target_format;
    clear.clear_color = request->clear_color;
    clear.clear_depth = request->clear_depth;
    clear.color_value = request->color_value;
    /* As the engine's depth buffer holds it: halved on the SiS, whose Z
     * test compares 15 bits (v9x_d3d_depth_fill_value). */
    clear.depth_value = v9x_d3d_depth_fill_value(request->depth_value);
    clear.write_red = (request->write_mask & V9X_R3D_ABI_WRITE_RED) != 0ul;
    clear.write_green = (request->write_mask & V9X_R3D_ABI_WRITE_GREEN) != 0ul;
    clear.write_blue = (request->write_mask & V9X_R3D_ABI_WRITE_BLUE) != 0ul;
    clear.write_depth = request->write_depth;
    clear.rects = (const V9X_R3D_CLEAR_RECT *)request->rects;
    clear.rect_count = request->rect_count;
    return v9x_r3d_clear(&clear) ? V9X_R3D_RESULT_OK
                                 : V9X_R3D_RESULT_INVALID;
}

/*
 * Clear2, the DX6 clear (D3DHAL_CALLBACKS3), served beside DrawPrimitives2.
 *
 * Its doc says it is "no longer used for DirectX 7.0 and beyond", where the
 * clear moved into the DrawPrimitives2 stream - which makes it the DX6
 * driver's clear, and this driver claims the DX6 interface, not the DX7 one.
 * Done on the CPU through the same r3d clear the render interface uses, on
 * the context's own target and Z surface, after the engine has drained so
 * nothing it still owes lands on top. Rectangles are clamped to the target;
 * one that clamps to nothing is skipped. Stencil has no buffer here.
 */
static V9X_R3D_CLEAR_RECT v9x_d3d_clear2_rects[64];

/* A float to the nearest long through the FPU's own store. The HAL links no
 * C runtime and Open Watcom lowers a float-to-integer cast to a __CHP call,
 * so the conversion is the instruction itself, as in src\opengl\gl_state.c. */
static long v9x_d3d_to_long(double value);
#pragma aux v9x_d3d_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];

DWORD __stdcall V9xD3dClear2(V9X_D3DHAL_CLEAR2DATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    V9X_R3D_CLEAR clear;
    DWORD done = 0ul;
    int drained;
    int ok = 1;

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.dp2_clear2_calls;
    }
    v9x_fpu_save(&fpu);
    context = v9x_d3d_context_from_handle(data->dwhContext);
    if (context == 0 || v9x_hal == 0 ||
        (data->dwNumRects != 0ul &&
         (data->lpRects == 0 ||
          IsBadReadPtr(data->lpRects,
                       data->dwNumRects * sizeof(V9X_D3DRECT))))) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.dp2_clear2_refused;
        }
        data->ddrval = 0x80070057ul;
        v9x_fpu_restore(&fpu);
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    /* Bounded: each call is itself a bounded wait, and BUSY forever is an
     * engine that will not drain, which must refuse the clear rather than
     * spin inside a callback that holds the Win16 lock. */
    {
        DWORD tries = 0ul;

        v9x_d3d_dp2_log("CLR2 enter", data->dwFlags, data->dwNumRects);
        drained = v9x_render_drain(1);
        while (drained == V9X_RENDER_DRAIN_BUSY && ++tries < 64ul) {
            drained = v9x_render_drain(1);
        }
        v9x_d3d_dp2_log("CLR2 drained", (DWORD)drained, tries);
    }
    if (drained != V9X_RENDER_DRAIN_DONE) {
        ++v9x_hal->d3d_diagnostics.dp2_clear2_refused;
        data->ddrval = 0x80070057ul;
        v9x_fpu_restore(&fpu);
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    clear.color = (void *)(v9x_hal->fb.linear_base + context->target_offset);
    clear.color_pitch = context->pitch;
    clear.depth = context->zbuffer != 0
        ? (void *)(v9x_hal->fb.linear_base + context->depth_offset) : 0;
    clear.depth_pitch = context->depth_pitch;
    clear.width = context->width;
    clear.height = context->height;
    clear.format = context->target_format;
    clear.clear_color = (data->dwFlags & V9X_D3DCLEAR_TARGET) != 0ul;
    clear.clear_depth = (data->dwFlags & V9X_D3DCLEAR_ZBUFFER) != 0ul &&
                        context->zbuffer != 0;
    clear.color_value = data->dwFillColor & 0x00fffffful;
    {
        float depth = data->dvFillDepth;
        DWORD value;

        if (!(depth > 0.0f)) {
            value = 0ul;
        } else if (depth >= 1.0f) {
            value = 0xfffful;
        } else {
            value = (DWORD)v9x_d3d_to_long((double)depth * 65535.0);
        }
        clear.depth_value = v9x_d3d_depth_fill_value(value);
    }
    clear.write_red = 1ul;
    clear.write_green = 1ul;
    clear.write_blue = 1ul;
    clear.write_depth = 1ul;
    clear.rects = v9x_d3d_clear2_rects;

    while (done < data->dwNumRects) {
        DWORD count = 0ul;

        while (done < data->dwNumRects && count < 64ul) {
            const V9X_D3DRECT *r = &data->lpRects[done++];
            LONG x1 = r->x1 < 0 ? 0 : r->x1;
            LONG y1 = r->y1 < 0 ? 0 : r->y1;
            LONG x2 = r->x2 > (LONG)context->width ? (LONG)context->width
                                                   : r->x2;
            LONG y2 = r->y2 > (LONG)context->height ? (LONG)context->height
                                                    : r->y2;

            if (x1 < x2 && y1 < y2) {
                v9x_d3d_clear2_rects[count].left = (v9x_u32)x1;
                v9x_d3d_clear2_rects[count].top = (v9x_u32)y1;
                v9x_d3d_clear2_rects[count].right = (v9x_u32)x2;
                v9x_d3d_clear2_rects[count].bottom = (v9x_u32)y2;
                ++count;
            }
        }
        if (count != 0ul) {
            clear.rect_count = count;
            if (!v9x_r3d_clear(&clear)) {
                ok = 0;
            }
        }
    }
    if (!ok) {
        ++v9x_hal->d3d_diagnostics.dp2_clear2_refused;
    }
    data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    v9x_d3d_dp2_log("CLR2 exit", data->ddrval, 0ul);
    v9x_fpu_restore(&fpu);
    return V9X_DDHAL_DRIVER_HANDLED;
}

typedef char v9x_d3d_assert_r3d_abi_rect[
    (sizeof(V9X_R3D_ABI_RECT) == sizeof(V9X_R3D_CLEAR_RECT) &&
     offsetof(V9X_R3D_ABI_RECT, bottom) ==
         offsetof(V9X_R3D_CLEAR_RECT, bottom)) ? 1 : -1];

static DWORD v9x_r3d_describe_body(V9X_R3D_ABI_DESCRIBE *out)
{
    const V9X_D3D_ENGINE_OPS *ops = v9x_r3d_engine();
    const char *name = "Velocity9x";
    DWORD format = 0ul;
    DWORD index;

    if (ops == 0) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    out->abi_version = V9X_R3D_ABI_VERSION;
    out->generation = v9x_r3d_generation;
    out->engine = 0ul;
    if (ops == &v9x_d3d_engine_soft) {
        out->engine = V9X_R3D_ABI_ENGINE_SOFTWARE;
        name = "Velocity9x Software";
    } else if (ops == &v9x_d3d_engine_virge) {
        out->engine = V9X_R3D_ABI_ENGINE_VIRGE;
        name = "Velocity9x ViRGE";
    } else if (ops == &v9x_d3d_engine_i9xx) {
        out->engine = V9X_R3D_ABI_ENGINE_GEN3;
        name = "Velocity9x GMA 950";
    } else if (ops == &v9x_d3d_engine_mach64) {
        out->engine = V9X_R3D_ABI_ENGINE_MACH64;
        name = "Velocity9x Mach64";
    } else if (ops == &v9x_d3d_engine_rage2) {
        out->engine = V9X_R3D_ABI_ENGINE_RAGE2;
        name = "Velocity9x Rage IIC";
    } else if (ops == &v9x_d3d_engine_sis6326) {
        out->engine = V9X_R3D_ABI_ENGINE_SIS6326;
        name = "Velocity9x SiS 6326";
    }
    /* The desktop's layout, when an engine can write it: the S3D writes
     * 1555 into any 16-bit target, so on a 565 desktop the ViRGE offers
     * none (docs\decisions\2026-09-26-phase05-mixed-engine-ordering-and-
     * virge-colour-mismatch.md). */
    out->target_formats = 0ul;
    if (v9x_d3d_target_format_of(&v9x_hal->info.vmiData.ddpfDisplay,
                                 &format) &&
        !(ops == &v9x_d3d_engine_virge &&
          format != V9X_D3D_TARGET_FORMAT_XRGB1555) &&
        /* The Rage IIC draws 565 only (rage2_draw.c's policy). */
        !(ops == &v9x_d3d_engine_rage2 &&
          format != V9X_D3D_TARGET_FORMAT_RGB565)) {
        out->target_formats = 1ul << format;
    }
    out->texture_formats = (1ul << V9X_R3D_ABI_FORMAT_RGB565) |
                           (1ul << V9X_R3D_ABI_FORMAT_ARGB1555) |
                           (1ul << V9X_R3D_ABI_FORMAT_ARGB4444);
    out->texture_size_max = v9x_r3d_texture_size_max(ops);
    out->batch_max = V9X_R3D_ABI_BATCH_MAX;
    out->texture_units = v9x_r3d_texture_units(ops);
    /* Surface textures, where the engine samples them for the interface:
     * Gen3's bind takes powers of two within its limits, square or not
     * (v9x_d3d_i9xx_texture_shape); the ViRGE's takes a square power of
     * two, 4 to 512, in ARGB1555 or ARGB4444 only - the S3D samples no
     * 565, so its formats say so and the ICD stores RGB images as 1555
     * with alpha one (v9x_d3d_virge_texture_bindable). The Mach64 takes
     * what its policy accepts: power-of-two edges, 8 to 256 each, square or
     * not (2026-10-01), in all three formats, placed by the HAL
     * (v9x_d3d_mach64_create_surface); the ICD squares only an edge under
     * 8. The software engine reads CPU
     * levels. */
    out->hw_texture_size_max = 0ul;
    out->hw_texture_shape = 0ul;
    out->hw_texture_size_min = 1ul;
    if (ops == &v9x_d3d_engine_i9xx) {
        out->hw_texture_size_max = ops->limits->texture_size_max;
        out->hw_texture_shape = V9X_R3D_ABI_HWTEX_POW2;
    } else if (ops == &v9x_d3d_engine_virge) {
        out->texture_formats = (1ul << V9X_R3D_ABI_FORMAT_ARGB1555) |
                               (1ul << V9X_R3D_ABI_FORMAT_ARGB4444);
        out->hw_texture_size_max = ops->limits->texture_size_max;
        out->hw_texture_shape = V9X_R3D_ABI_HWTEX_SQUARE |
                                V9X_R3D_ABI_HWTEX_POW2;
    } else if (ops == &v9x_d3d_engine_mach64 ||
               ops == &v9x_d3d_engine_rage2) {
        /* The Rage IIC's as the Mach64's: powers of two, 8 to 256, square
         * or not (measured, Phase 4 T10/T14), all three formats, placed by
         * v9x_d3d_rage2_create_surface at pitch = width. */
        out->hw_texture_size_max = ops->limits->texture_size_max;
        out->hw_texture_size_min = ops->limits->texture_size_min;
        out->hw_texture_shape = V9X_R3D_ABI_HWTEX_POW2;
    } else if (ops == &v9x_d3d_engine_sis6326) {
        /* The SiS samples what its Direct3D path does: power-of-two edges,
         * 1 to 512, square or not, all three formats, placed by
         * v9x_d3d_sis_create_surface (docs\decisions\2026-10-05-sis6326-
         * 3d-textures.md). It has no software fallback, so a texture
         * the ICD keeps on the CPU is not drawn. */
        out->hw_texture_size_max = ops->limits->texture_size_max;
        out->hw_texture_size_min = ops->limits->texture_size_min;
        out->hw_texture_shape = V9X_R3D_ABI_HWTEX_POW2;
    }
    for (index = 0ul; index + 1ul < sizeof(out->renderer) &&
                      name[index] != '\0'; ++index) {
        out->renderer[index] = name[index];
    }
    while (index < sizeof(out->renderer)) {
        out->renderer[index++] = '\0';
    }
    return V9X_R3D_RESULT_OK;
}

/*
 * The five entries. Each takes the Win16 mutex for the whole call - the
 * ICD is not DirectDraw, so nobody has taken it on the HAL's behalf - and
 * never calls USER, GDI or DirectDraw while holding it. The only waits under
 * it are the shared drain's, bounded by the engines' own timeouts.
 */
static v9x_u32 V9X_R3D_CALL v9x_r3d_entry_describe(V9X_R3D_ABI_DESCRIBE *out)
{
    DWORD result;

    if (out == 0 || IsBadWritePtr(out, sizeof(v9x_u32))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (out->struct_bytes != (v9x_u32)sizeof(V9X_R3D_ABI_DESCRIBE)) {
        return V9X_R3D_RESULT_ABI;
    }
    if (IsBadWritePtr(out, sizeof(V9X_R3D_ABI_DESCRIBE))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (!v9x_win16_enter()) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    v9x_d3d_note_process(V9X_DIAG_PROCESS_GL);
    result = v9x_r3d_describe_body(out);
    v9x_win16_leave();
    return result;
}

static v9x_u32 V9X_R3D_CALL v9x_r3d_entry_draw(const V9X_R3D_ABI_DRAW *draw,
                                               V9X_R3D_ABI_OUTCOME *outcome)
{
    V9X_FPU_AREA fpu;
    DWORD result;
    DWORD started = V9X_TIME_BEGIN();

    if (outcome == 0 || IsBadWritePtr(outcome, sizeof(*outcome))) {
        return V9X_R3D_RESULT_INVALID;
    }
    outcome->submitted = 0ul;
    if (draw == 0 || IsBadReadPtr(draw, sizeof(v9x_u32))) {
        outcome->result = V9X_R3D_RESULT_INVALID;
        return outcome->result;
    }
    if (draw->struct_bytes == (v9x_u32)sizeof(V9X_R3D_ABI_DRAW) &&
        IsBadReadPtr(draw, sizeof(V9X_R3D_ABI_DRAW))) {
        outcome->result = V9X_R3D_RESULT_INVALID;
        return outcome->result;
    }
    if (!v9x_win16_enter()) {
        outcome->result = V9X_R3D_RESULT_NOT_READY;
        return outcome->result;
    }
    /*
     * The application's FPU state saved, and the FPU reinitialised, as at
     * every Direct3D entry: fnsave leaves it at its default precision and
     * rounding. Without it an engine's floating-point setup ran under the
     * caller's control word - Quake 2's 24-bit precision - and on the Rage
     * IIC, whose triangle setup is double arithmetic, 68,270 of Quake 2's
     * pieces failed the texture fit (A8U4I5 boot 147) that the same inputs
     * pass on the host.
     */
    v9x_fpu_save(&fpu);
    result = v9x_r3d_draw_body(draw, outcome);
    v9x_fpu_restore(&fpu);
    v9x_win16_leave();
    if (V9X_TIME_ENABLED()) {
        DWORD delta = v9x_rdtsc_low() - started;
        DWORD *pair = v9x_hal->d3d_diagnostics.r3d_draw_cycles;

        pair[0] += delta;
        if (pair[0] < delta) {
            ++pair[1];
        }
        ++v9x_hal->d3d_diagnostics.r3d_draw_calls;
    }
    outcome->result = result;
    return result;
}

static v9x_u32 V9X_R3D_CALL v9x_r3d_entry_clear(const V9X_R3D_ABI_CLEAR *clear)
{
    DWORD result;

    if (clear == 0 || IsBadReadPtr(clear, sizeof(v9x_u32))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (clear->struct_bytes == (v9x_u32)sizeof(V9X_R3D_ABI_CLEAR) &&
        IsBadReadPtr(clear, sizeof(V9X_R3D_ABI_CLEAR))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (!v9x_win16_enter()) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    result = v9x_r3d_clear_body(clear);
    v9x_win16_leave();
    return result;
}

/* Every engine submits each batch before its draw returns, so there is
 * nothing pending to push; flush answers only whether the session holds. */
static v9x_u32 V9X_R3D_CALL v9x_r3d_entry_flush(v9x_u32 generation)
{
    DWORD result;

    if (!v9x_win16_enter()) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    if (v9x_r3d_engine() == 0) {
        result = V9X_R3D_RESULT_NOT_READY;
    } else if (generation != v9x_r3d_generation) {
        result = V9X_R3D_RESULT_STALE;
    } else {
        result = V9X_R3D_RESULT_OK;
    }
    v9x_win16_leave();
    return result;
}

static v9x_u32 V9X_R3D_CALL v9x_r3d_entry_finish(v9x_u32 generation)
{
    DWORD result;

    if (!v9x_win16_enter()) {
        return V9X_R3D_RESULT_NOT_READY;
    }
    if (v9x_r3d_engine() == 0) {
        result = V9X_R3D_RESULT_NOT_READY;
    } else if (generation != v9x_r3d_generation) {
        result = V9X_R3D_RESULT_STALE;
    } else {
        result = v9x_r3d_drain();
    }
    v9x_win16_leave();
    return result;
}

static const V9X_R3D_INTERFACE v9x_r3d_interface = {
    V9X_R3D_ABI_VERSION,
    sizeof(V9X_R3D_INTERFACE),
    v9x_r3d_entry_describe,
    v9x_r3d_entry_draw,
    v9x_r3d_entry_clear,
    v9x_r3d_entry_flush,
    v9x_r3d_entry_finish
};

/* The export (build-ddraw-hal-dll.ps1 names it). Exact negotiation: the
 * ICD gets this build's table only when it was built against this header. */
const V9X_R3D_INTERFACE * __stdcall V9xRenderInterface(v9x_u32 abi_version,
                                                       v9x_u32 struct_bytes)
{
    if (abi_version != V9X_R3D_ABI_VERSION ||
        struct_bytes != (v9x_u32)sizeof(V9X_R3D_INTERFACE)) {
        return 0;
    }
    return &v9x_r3d_interface;
}
