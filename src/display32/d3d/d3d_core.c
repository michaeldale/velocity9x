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

static V9X_D3D_CONTEXT v9x_d3d_contexts[V9X_D3D_CONTEXT_COUNT];
static V9X_D3D_TEXTURE v9x_d3d_textures[V9X_D3D_TEXTURE_COUNT];

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
static V9X_D3DHAL_CALLBACKS2 v9x_d3d_callbacks2;

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
     * Mode first, chip second.
     *
     * The software engine is selected by capability, not by engine_type,
     * because the whole point of it is to serve a chip whose engine_type is
     * NONE. Testing it before the chip means a card with an S3D unit can also
     * be asked for the rasterizer - which is what "Software" in the settings
     * page means on a ViRGE, and is how a game that misbehaves through the
     * narrow S3D path gets a second option.
     */
    if ((v9x_hal->engine.engine_caps &
         V9X_DD_ENGINE_CAP_D3D_SOFTWARE) != 0ul) {
        return &v9x_d3d_engine_soft;
    }
    if (v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        return &v9x_d3d_engine_virge;
    }
    /*
     * Gen3 resolves, and then reports itself NOT READY.
     *
     * The arm exists so the selector is exercised rather than added later
     * beside an engine that also has to be right. Nothing selects it today:
     * the intel-gma manifest declares EngineType NONE, so the descriptor never
     * carries this value. When it does, the engine still refuses every draw
     * until it has a 32-bit submission path and a risk decision covering
     * sustained 3D work.
     */
    if (v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
        return &v9x_d3d_engine_i9xx;
    }
    return 0;
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
        int ok = ops->draw_triangles(context, vertices, triangle_count);
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

static V9X_D3D_TEXTURE *v9x_d3d_texture_from_handle(DWORD handle,
                                                     DWORD context)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if ((DWORD)&v9x_d3d_textures[index] == handle &&
            v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].context == context) {
            return &v9x_d3d_textures[index];
        }
    }
    return 0;
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

static BYTE v9x_d3d_lerp_byte(BYTE first, BYTE second, float amount)
{
    return (BYTE)v9x_float_to_long((float)first +
        ((float)second - (float)first) * amount);
}

static DWORD v9x_d3d_lerp_color(DWORD first, DWORD second, float amount)
{
    return ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 24),
                                     (BYTE)(second >> 24), amount) << 24) |
           ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 16),
                                     (BYTE)(second >> 16), amount) << 16) |
           ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 8),
                                     (BYTE)(second >> 8), amount) << 8) |
           (DWORD)v9x_d3d_lerp_byte((BYTE)first, (BYTE)second, amount);
}

static void v9x_d3d_lerp_vertex(V9X_D3DTLVERTEX *result,
                                const V9X_D3DTLVERTEX *first,
                                const V9X_D3DTLVERTEX *second,
                                float amount)
{
    result->sx = first->sx + (second->sx - first->sx) * amount;
    result->sy = first->sy + (second->sy - first->sy) * amount;
    result->sz = first->sz + (second->sz - first->sz) * amount;
    result->rhw = first->rhw + (second->rhw - first->rhw) * amount;
    result->color = v9x_d3d_lerp_color(first->color, second->color, amount);
    result->specular = v9x_d3d_lerp_color(first->specular,
                                          second->specular, amount);
    /*
     * Texture coordinates are not linear in screen space unless both ends
     * have the same rhw. rhw is, and so is tu * rhw, so the coordinate at the
     * cut is their quotient. Blending tu itself put every clipped vertex of a
     * receding polygon too far along the texture - on 3DMark 99's tunnel,
     * whose walls are clipped at every screen edge, that squeezed the whole
     * texture into the visible part of the wall and drove the mip level to
     * the bottom of the chain. Colour, depth and rhw stay linear: the engines
     * interpolate them that way.
     *
     * Equal rhw keeps the plain blend, so an affine triangle is cut exactly
     * as before, bit for bit.
     */
    if (first->rhw != second->rhw && first->rhw > 0.0f &&
        second->rhw > 0.0f && result->rhw > 0.0f) {
        float first_u = first->tu * first->rhw;
        float first_v = first->tv * first->rhw;
        float inverse = 1.0f / result->rhw;

        result->tu = (first_u +
                      (second->tu * second->rhw - first_u) * amount) * inverse;
        result->tv = (first_v +
                      (second->tv * second->rhw - first_v) * amount) * inverse;
    } else {
        result->tu = first->tu + (second->tu - first->tu) * amount;
        result->tv = first->tv + (second->tv - first->tv) * amount;
    }
}

static int v9x_d3d_clip_triangle(const V9X_D3D_CONTEXT *context,
                                 const V9X_D3DTLVERTEX *triangle,
                                 V9X_D3DTLVERTEX *result)
{
    V9X_D3DTLVERTEX buffers[2][8];
    V9X_D3DTLVERTEX *input = buffers[0];
    V9X_D3DTLVERTEX *output = buffers[1];
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    float limit;
    DWORD count = 3ul;
    DWORD edge;
    DWORD index;

    if (ops == 0) {
        return -1;
    }
    /* The guard band belongs to the engine, not to the clipper: a vertex
     * outside it overflows that engine's fixed-point coordinate conversion,
     * so it is refused here rather than wrapped there. */
    limit = ops->limits->coordinate_limit;
    for (index = 0ul; index < 3ul; ++index) {
        if (!(triangle[index].sx >= -limit &&
              triangle[index].sx < limit &&
              triangle[index].sy >= -limit &&
              triangle[index].sy < limit)) {
            return -1;
        }
        input[index] = triangle[index];
    }
    for (edge = 0ul; edge < 4ul && count != 0ul; ++edge) {
        V9X_D3DTLVERTEX previous = input[count - 1ul];
        int previous_inside;
        DWORD output_count = 0ul;
        /*
         * The right and bottom edges are the viewport's, width and height,
         * not the last pixel's. A full-screen quad is 0..640 in Direct3D's
         * convention, and cut at 639 its spans end one column early on an
         * engine that fills [ceil(x1), ceil(x2)) - a black line down the
         * right and along the bottom of every full-screen plane. The pixel
         * past the edge is the hardware clip rectangle's to discard: S3's
         * own driver sets cmdHWCLIP_EN with CLIP_L_R at width - 1
         * (98DDK s3v\D3DRENDR.C:64, 399-427) and so does this one, and the
         * CPU rasterizer clamps to extent - 1 on its own.
         */
        float boundary = (edge == 0ul || edge == 2ul) ? 0.0f :
            (edge == 1ul ? (float)context->width :
                           (float)context->height);

        if (edge < 2ul) {
            previous_inside = edge == 0ul ? previous.sx >= boundary
                                          : previous.sx <= boundary;
        } else {
            previous_inside = edge == 2ul ? previous.sy >= boundary
                                          : previous.sy <= boundary;
        }
        for (index = 0ul; index < count; ++index) {
            V9X_D3DTLVERTEX current = input[index];
            int current_inside;

            if (edge < 2ul) {
                current_inside = edge == 0ul ? current.sx >= boundary
                                             : current.sx <= boundary;
            } else {
                current_inside = edge == 2ul ? current.sy >= boundary
                                             : current.sy <= boundary;
            }
            if (current_inside != previous_inside) {
                float denominator = edge < 2ul
                    ? current.sx - previous.sx : current.sy - previous.sy;
                float numerator = edge < 2ul
                    ? boundary - previous.sx : boundary - previous.sy;

                if (denominator != 0.0f && output_count < 8ul) {
                    v9x_d3d_lerp_vertex(&output[output_count], &previous,
                                        &current, numerator / denominator);
                    if (edge < 2ul) {
                        output[output_count].sx = boundary;
                    } else {
                        output[output_count].sy = boundary;
                    }
                    ++output_count;
                }
            }
            if (current_inside && output_count < 8ul) {
                output[output_count++] = current;
            }
            previous = current;
            previous_inside = current_inside;
        }
        count = output_count;
        {
            V9X_D3DTLVERTEX *swap = input;
            input = output;
            output = swap;
        }
    }
    for (index = 0ul; index < count; ++index) {
        result[index] = input[index];
    }
    return (int)count;
}

/*
 * Whether all three vertices lie on the render target, edges included.
 *
 * Written so that a NaN coordinate answers no and goes to the clipper, whose
 * guard-band test refuses it, rather than answering yes and reaching the
 * engine's fixed-point conversion.
 */
static int v9x_d3d_triangle_on_target(const V9X_D3D_CONTEXT *context,
                                      const V9X_D3DTLVERTEX *triangle)
{
    float right = (float)context->width;
    float bottom = (float)context->height;
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        if (!(triangle[index].sx >= 0.0f && triangle[index].sx <= right &&
              triangle[index].sy >= 0.0f && triangle[index].sy <= bottom)) {
            return 0;
        }
    }
    return 1;
}

/*
 * A triangle list from one of the DX5 entry points, clipped where the engine
 * needs it, then drawn.
 *
 * The engine contract says the core hands over vertices already clipped, and
 * until 2026-09-23 only RenderPrimitive did. DrawOnePrimitive, DrawPrimitives
 * and DrawOneIndexedPrimitive passed the application's vertices straight
 * through, and the S3D emitter declines any triangle with a vertex off the
 * target, so every triangle that crossed the screen edge was dropped whole.
 * On the Trio3D/2X that was 3DMark 99's fill-rate test drawn entirely black -
 * its planes are full-screen quads ending exactly at 640.0 - the filtering
 * tunnel reduced to one or two walls, and 66,990 declined triangles in one
 * run (build\driver-results\3dmark99-640-20260923-run2).
 *
 * Triangles already on the target go through as runs, windows on the
 * caller's array, so the common case costs one bounds test per triangle and
 * no copy. Only a triangle that crosses an edge is cut, and its fan is drawn
 * on its own between the runs either side of it. A refused triangle - past
 * the guard band, or one the engine declines - is counted by the caller as a
 * refused batch and the rest of the list is still drawn.
 */
static int v9x_d3d_draw_list(const V9X_D3D_ENGINE_OPS *ops,
                             V9X_D3D_CONTEXT *context,
                             const V9X_D3DTLVERTEX *vertices,
                             DWORD triangle_count)
{
    V9X_D3DTLVERTEX clipped[8];
    V9X_D3DTLVERTEX fan_list[V9X_D3D_MAX_FAN_TRIANGLES * 3u];
    DWORD run_start = 0ul;
    DWORD index;
    int ok = 1;

    if (ops->limits->clip_in_core == 0ul) {
        return v9x_d3d_draw_batch(ops, context, vertices, triangle_count);
    }
    for (index = 0ul; index < triangle_count; ++index) {
        const V9X_D3DTLVERTEX *triangle = &vertices[index * 3ul];
        DWORD fan_triangles = 0ul;
        int clipped_count;
        int fan;

        if (v9x_d3d_triangle_on_target(context, triangle)) {
            continue;
        }
        if (index > run_start &&
            !v9x_d3d_draw_batch(ops, context, &vertices[run_start * 3ul],
                                index - run_start)) {
            ok = 0;
        }
        run_start = index + 1ul;

        clipped_count = v9x_d3d_clip_triangle(context, triangle, clipped);
        if (clipped_count < 0) {
            ok = 0;
            continue;
        }
        for (fan = 1; fan + 1 < clipped_count; ++fan) {
            fan_list[fan_triangles * 3ul] = clipped[0];
            fan_list[fan_triangles * 3ul + 1ul] = clipped[fan];
            fan_list[fan_triangles * 3ul + 2ul] = clipped[fan + 1];
            ++fan_triangles;
        }
        if (fan_triangles != 0ul &&
            !v9x_d3d_draw_batch(ops, context, fan_list, fan_triangles)) {
            ok = 0;
        }
    }
    if (run_start < triangle_count &&
        !v9x_d3d_draw_batch(ops, context, &vertices[run_start * 3ul],
                            triangle_count - run_start)) {
        ok = 0;
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
        if (depth_pitch != depth_expected) {
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
            context->shade_mode = V9X_D3DSHADE_GOURAUD;
            /*
             * Only z_func. depth_offset, depth_pitch, z_enable and z_write are
             * owned by v9x_d3d_set_target, which ran above - resetting them
             * here would discard the decision it just made about the attached
             * depth surface. D3DCMP_LESS is the DDK's default (D3DCTXT.C:351).
             */
            context->z_func = V9X_D3DCMP_LESS;
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
    context->depth_offset = 0ul;
    context->depth_pitch = 0ul;
    context->z_enable = 0ul;
    context->z_write = 0ul;
    context->z_func = 0ul;
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
            v9x_d3d_contexts[index].depth_offset = 0ul;
            v9x_d3d_contexts[index].depth_pitch = 0ul;
            v9x_d3d_contexts[index].z_enable = 0ul;
            v9x_d3d_contexts[index].z_write = 0ul;
            v9x_d3d_contexts[index].z_func = 0ul;
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
    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active == 0ul) {
            v9x_d3d_textures[index].active = 1ul;
            v9x_d3d_textures[index].context = data->dwhContext;
            v9x_d3d_textures[index].surface = data->lpDDS;
            v9x_d3d_textures[index].lcl = lcl;
            data->dwHandle = (DWORD)&v9x_d3d_textures[index];
            data->ddrval = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.texture_creates;
            v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
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

DWORD __stdcall V9xD3dRenderPrimitive(
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

DWORD __stdcall V9xD3dDrawOnePrimitive(
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

DWORD __stdcall V9xD3dDrawPrimitives(V9X_D3DHAL_DRAWPRIMITIVESDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts;
    V9X_D3DTLVERTEX fan_batch[V9X_D3D_INDEXED_BATCH * 3u];
    BYTE *cursor;
    DWORD record;
    DWORD fan_triangles;
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWPRIMS,
                    data != 0 ? (DWORD)data->lpvData : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    if (ops != 0 && context != 0 && ops->ready() &&
        data->lpvData != 0) {
        cursor = (BYTE *)data->lpvData;
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
                for (change = 0ul;
                     change < (DWORD)counts->wNumStateChanges; ++change) {
                    v9x_d3d_apply_state(context, pairs[change * 2ul],
                                        pairs[change * 2ul + 1ul]);
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
             * A LIST is already one batch. A FAN is not: its N vertices are
             * N-2 triangles all sharing vertex 0, so they are gathered the
             * way the indexed path gathers its pool.
             *
             * This type was refused until 2026-09-20, and the ViRGE guest
             * measured what that meant: 192,259 records turned away, every
             * one a fan, which is every test Final Reality runs past its
             * intro. The benchmark stopped aborting once a refused batch
             * reported DD_OK, and then drew a black screen, because none of
             * its geometry was a shape this path would take.
             */
            if (counts->wPrimitiveType == V9X_D3DPT_TRIANGLEFAN) {
                const V9X_D3DTLVERTEX *fan =
                    (const V9X_D3DTLVERTEX *)cursor;
                DWORD apex;

                fan_triangles = 0ul;
                for (apex = 1ul;
                     apex + 1ul < (DWORD)counts->wNumVertices; ++apex) {
                    fan_batch[fan_triangles * 3ul] = fan[0];
                    fan_batch[fan_triangles * 3ul + 1ul] = fan[apex];
                    fan_batch[fan_triangles * 3ul + 2ul] = fan[apex + 1ul];
                    ++fan_triangles;
                    if (fan_triangles == (DWORD)V9X_D3D_INDEXED_BATCH) {
                        if (!v9x_d3d_draw_list(ops, context, fan_batch,
                                               fan_triangles)) {
                            ok = 0;
                            break;
                        }
                        fan_triangles = 0ul;
                    }
                }
                if (ok && fan_triangles != 0ul &&
                    !v9x_d3d_draw_list(ops, context, fan_batch,
                                       fan_triangles)) {
                    ok = 0;
                }
                if (!ok && v9x_hal != 0) {
                    ++v9x_hal->d3d_diagnostics.batches_engine_refused;
                    ok = 1;
                }
            } else if (!v9x_d3d_draw_list(ops, context,
                                    (const V9X_D3DTLVERTEX *)cursor,
                                    (DWORD)counts->wNumVertices / 3ul)) {
                /* The engine refused this record. Counted and carried on to
                 * the next one: see batches_engine_refused. Aborting the
                 * rest of a buffer over one refused record is the mistake
                 * the ViRGE's per-triangle loop was making. */
                if (v9x_hal != 0) {
                    ++v9x_hal->d3d_diagnostics.batches_engine_refused;
                }
            }
            if (!ok) {
                break;
            }
            cursor += (DWORD)counts->wNumVertices *
                      sizeof(V9X_D3DTLVERTEX);
        }
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
DWORD __stdcall V9xD3dDrawOneIndexedPrimitive(
    V9X_D3DHAL_DRAWONEINDEXEDPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3DTLVERTEX batch[V9X_D3D_INDEXED_BATCH * 3u];
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
         * swapped. This driver sets CULLMODE_NONE so nothing is culled by
         * it, but the order still decides which vertex is the provoking one
         * for flat shading, so the alternation is kept rather than dropped
         * as unobservable.
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
        DWORD copy = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
        DWORD index;
        BYTE *destination;
        const BYTE *source;

        if (data->dwExpectedSize < copy) {
            copy = data->dwExpectedSize;
        }
        data->dwActualSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
        if (data->lpvData != 0 && copy != 0ul) {
            destination = (BYTE *)data->lpvData;
            source = (const BYTE *)&v9x_hal->d3d_extended_caps;
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
 * NOT v9x_d3d_engine(). DriverInit runs before the 16-bit side fills the
 * engine descriptor - dd16.c says so about the framebuffer descriptor at the
 * same point, and the engine is filled in that same later step - so at publish
 * time engine_type is 0 and engine.flags carries no V9X_DD_ENGINE_VALID.
 * Selecting on it here published nothing at all, and DDRAW then enumerated no
 * hardware Direct3D device. Measured on the ViRGE guest: D3DHalFound went 1 to
 * 0. See the D3D core/engine split decision record of 2026-08-29.
 *
 * So caps publication cannot be chip-selected today, and this returns the one
 * D3D engine the binary carries. That is exactly the pre-split behaviour: the
 * tables were always filled, and the 16-bit side is and remains the capability
 * authority that hides them from a chip whose engine_caps lack D3D.
 *
 * A second D3D engine has to fix this properly, and the fix is on the 16-bit
 * side rather than here: stamp the chip's engine_type into the shared block
 * before DriverInit is called, then select on it. That is a change to the
 * enable ordering and needs its own evidence, which is why it is not being
 * guessed at now.
 */
static const V9X_D3D_ENGINE_OPS *v9x_d3d_publish_engine(void)
{
    /*
     * This can now select, and the comment above is the record of why it could
     * not before. The 16-bit side stamps engine_caps in v9x_dd_block(), which
     * runs on the DDGET32BITDRIVERNAME escape and therefore strictly before
     * DriverInit - so the software capability is readable here where
     * engine_type still is not.
     *
     * Only the software bit is tested. Selecting the chip's engine on
     * engine_type remains impossible at this point and remains the reason this
     * function exists separately from v9x_d3d_engine(): the descriptor's
     * control window and type are filled by the later framebuffer refresh.
     * The fallback is the binary's one hardware engine, which is exactly the
     * pre-split behaviour and is clamped out by the 16-bit side for a family
     * that does not claim D3D.
     */
    if (v9x_hal != 0 &&
        (v9x_hal->engine.engine_caps &
         V9X_DD_ENGINE_CAP_D3D_SOFTWARE) != 0ul) {
        return &v9x_d3d_engine_soft;
    }
    /*
     * Gen3, when the 16-bit side has already stamped the type.
     *
     * engine_type is normally unreadable here - that is why this function is
     * separate - but a family that fills it before DriverInit would otherwise
     * fall through to the ViRGE, and publishing the ViRGE's caps on an Intel
     * part is the failure this whole function exists to prevent. Tested
     * rather than assumed absent.
     */
    if (v9x_hal != 0 &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
        return &v9x_d3d_engine_i9xx;
    }
    return &v9x_d3d_engine_virge;
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

    ops->describe_caps(shared);

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
    if (ops->limits != 0 && ops->limits->texture_align != 0ul) {
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
}
