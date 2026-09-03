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

static V9X_D3D_CONTEXT v9x_d3d_contexts[V9X_D3D_CONTEXT_COUNT];
static V9X_D3D_TEXTURE v9x_d3d_textures[V9X_D3D_TEXTURE_COUNT];
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

static V9X_D3D_CONTEXT *v9x_d3d_context_from_handle(DWORD handle)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        if ((DWORD)&v9x_d3d_contexts[index] == handle &&
            v9x_d3d_contexts[index].active != 0ul) {
            return &v9x_d3d_contexts[index];
        }
    }
    return 0;
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

static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface);

static void v9x_d3d_textures_destroy_context(DWORD context)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].context == context) {
            v9x_d3d_textures[index].active = 0ul;
            v9x_d3d_textures[index].context = 0ul;
            v9x_d3d_textures[index].surface = 0;
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
        return 0;
    }
    texture = v9x_d3d_texture_from_handle(context->texture_handle,
                                           (DWORD)context);
    return texture != 0 ? v9x_d3d_surface_lcl(texture->surface) : 0;
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
    result->tu = first->tu + (second->tu - first->tu) * amount;
    result->tv = first->tv + (second->tv - first->tv) * amount;
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
        float boundary = (edge == 0ul || edge == 2ul) ? 0.0f :
            (edge == 1ul ? (float)(context->width - 1ul) :
                           (float)(context->height - 1ul));

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

static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface)
{
    V9X_DD_SURFACE_INT *wrapper = (V9X_DD_SURFACE_INT *)surface;

    return wrapper != 0 ? wrapper->lpLcl : 0;
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
    V9X_DD_SURFACE_LCL *target = v9x_d3d_surface_lcl(surface);
    V9X_DD_SURFACE_LCL *depth = zbuffer != 0
        ? v9x_d3d_surface_lcl(zbuffer) : 0;
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
    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active == 0ul) {
            v9x_d3d_textures[index].active = 1ul;
            v9x_d3d_textures[index].context = data->dwhContext;
            v9x_d3d_textures[index].surface = data->lpDDS;
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
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
    if (context != 0 && exe != 0 && exe->lpGbl != 0 &&
        exe->lpGbl->fpVidMem != 0ul && data->dwCount <= 64ul) {
        states = (V9X_D3DSTATE *)(exe->lpGbl->fpVidMem + data->dwOffset);
        for (index = 0ul; index < data->dwCount; ++index) {
            switch (states[index].type) {
            case V9X_D3DRENDERSTATE_TEXTUREHANDLE:
                context->texture_handle = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREPERSPECTIVE:
                /* Perspective setup is added after the affine texture gate. */
                break;
            case V9X_D3DRENDERSTATE_WRAPU:
            case V9X_D3DRENDERSTATE_WRAPV:
                context->texture_wrap = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMAG:
                context->texture_mag = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMIN:
                context->texture_min = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMAPBLEND:
                context->texture_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREADDRESS:
            case V9X_D3DRENDERSTATE_TEXTUREADDRESSU:
            case V9X_D3DRENDERSTATE_TEXTUREADDRESSV:
                context->texture_address = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_BORDERCOLOR:
                context->texture_border = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_SRCBLEND:
                context->src_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_DESTBLEND:
                context->dest_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_ALPHABLENDENABLE:
                context->alpha_blend_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_FOGENABLE:
                context->fog_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_SPECULARENABLE:
                context->specular_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_FOGCOLOR:
                context->fog_color = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_ZENABLE:
                /* DirectX 5 allows D3DZB_USEW (2) here. This driver publishes
                 * no W-buffer capability, so anything non-zero is plain Z. */
                context->z_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_ZWRITEENABLE:
                context->z_write = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_ZFUNC:
                /* Not validated here. The engine's mapping table has a
                 * default arm, which is where an unknown function is decided
                 * - and the safe default is not the one a zeroed field would
                 * give. */
                context->z_func = states[index].argument;
                break;
            default:
                break;
            }
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
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
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

    v9x_trace_enter(V9X_TRACE_D3D_RENDERPRIM,
                    data != 0
                        ? (((DWORD)data->diInstruction.bOpcode << 24) |
                           ((DWORD)data->diInstruction.bSize << 16) |
                           (DWORD)data->diInstruction.wCount)
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
    tl = data != 0 ? v9x_d3d_surface_lcl(data->lpTLBuf) : 0;
    if (ops != 0 && context != 0 && exe != 0 && exe->lpGbl != 0 && tl != 0 &&
        tl->lpGbl != 0 && ops->ready() &&
        data->diInstruction.bOpcode == 3u &&
        data->diInstruction.bSize >= sizeof(V9X_D3DTRIANGLE) &&
        data->diInstruction.wCount != 0u) {
        ok = 1;
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
            v9x_d3d_apply_vertex_color(context, &source[0]);
            v9x_d3d_apply_vertex_color(context, &source[1]);
            v9x_d3d_apply_vertex_color(context, &source[2]);
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
                !ops->draw_triangles(context, fan_list, fan_triangles)) {
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
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_RENDERPRIM,
                   ok ? V9X_DD_OK : 0x80070057ul);
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

DWORD __stdcall V9xD3dDrawOnePrimitive(
    V9X_D3DHAL_DRAWONEPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEPRIM,
                    data != 0
                        ? ((data->PrimitiveType << 16) |
                           (data->dwNumVertices & 0xfffful))
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    if (ops != 0 && context != 0 && ops->ready() &&
        data->PrimitiveType == V9X_D3DPT_TRIANGLELIST &&
        data->VertexType == V9X_D3DVT_TLVERTEX &&
        data->lpvVertices != 0 && data->dwNumVertices == 3ul) {
        ok = ops->draw_triangles(context,
                                 (const V9X_D3DTLVERTEX *)data->lpvVertices,
                                 1ul);
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEPRIM,
                   ok ? V9X_DD_OK : 0x80070057ul);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dDrawPrimitives(V9X_D3DHAL_DRAWPRIMITIVESDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    const V9X_D3D_ENGINE_OPS *ops = v9x_d3d_engine();
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts;
    BYTE *cursor;
    DWORD record;
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
            if (counts->wNumStateChanges > 64u) {
                ok = 0;
                break;
            }
            cursor += (DWORD)counts->wNumStateChanges * 2ul * sizeof(DWORD);
            if (counts->wNumVertices == 0u) {
                break;
            }
            cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
            if (counts->wPrimitiveType != V9X_D3DPT_TRIANGLELIST ||
                counts->wVertexType != V9X_D3DVT_TLVERTEX ||
                counts->wNumVertices > 192u ||
                (counts->wNumVertices % 3u) != 0u) {
                ok = 0;
                break;
            }
            /* Already a triangle list, so the whole record is one batch. */
            if (!ops->draw_triangles(context,
                                     (const V9X_D3DTLVERTEX *)cursor,
                                     (DWORD)counts->wNumVertices / 3ul)) {
                ok = 0;
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
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWPRIMS,
                   ok ? V9X_DD_OK : 0x80070057ul);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dDrawOneIndexedPrimitive(void *data)
{
    (void)data;
    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEINDEXED, 0ul);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEINDEXED, 0ul);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
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
#if V9X_C3_SERVE_D3D_CALLBACKS2
    for (index = 0ul; index < 16ul; ++index) {
        if (data->guidInfo[index] != v9x_guid_d3d_callbacks2[index]) {
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
