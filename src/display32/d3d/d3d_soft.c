/*
 * The CPU rasterizer's engine, selected by mode rather than by chip.
 *
 * This file is the engine, not the rasterizer. It answers the four questions
 * V9X_D3D_ENGINE_OPS asks - what it will accept, whether it is ready, what to
 * publish, and draw this batch - and hands the arithmetic to d3d_raster.c,
 * which knows nothing about DirectDraw and is held to a pixel table by
 * tests\host\test_d3d_raster.c. What stays here is everything that cannot
 * cross that line: the shared block, the DDHAL vertex layout, and the
 * float-to-fixed conversion, which needs a #pragma aux fistp because Open
 * Watcom lowers a float-to-int cast to __CHP and the HAL links with `option
 * nodefaultlibs`.
 *
 * Stage 1 of mode 2 shipped a stub here that filled each triangle's bounding
 * box with a flat colour, and it was not a placeholder - it was the
 * instrument that proved the whole path on a Trio64 before any arithmetic
 * existed: the SYSTEM.INI mode, the capability stamp, engine selection, caps
 * publication, context creation, the clipper, and the draw call arriving with
 * usable vertices. That measurement is in the 2026-08-30 commit and is what
 * lets this change be read as a rasterizer change and nothing else.
 *
 * Work-order step 6, and it has been run by the host tests only. No guest has
 * drawn a triangle through it.
 *
 * docs\plans\s3-trio64-voodoo2-hybrid-3d.md, mode 2, work-order steps 5 and 6.
 */
#include "d3d_internal.h"
#include "d3d_raster.h"

/*
 * The largest render target this engine accepts.
 *
 * It is the rasterizer's bound, restated where the limits table can use it.
 * Exceeding V9X_D3D_RASTER_DIMENSION_MAX would make that file's interpolation
 * products overflow a signed 32-bit integer - on large modes only, silently -
 * so the two numbers are tied together here rather than left to agree by
 * habit. The assert below is a negative array size on a mismatch, which is a
 * compile error rather than a review comment.
 */
#define V9X_D3D_SOFT_DIMENSION_MAX V9X_D3D_RASTER_DIMENSION_MAX

typedef char v9x_assert_soft_dimension[
    (V9X_D3D_SOFT_DIMENSION_MAX <= V9X_D3D_RASTER_DIMENSION_MAX) ? 1 : -1];

/*
 * The subpixel scale as a float, for the one conversion that needs it.
 *
 * Written as a literal because a cast of the integer macro would be a
 * float-to-int conversion's mirror image and this file is already careful
 * about those; the assert holds the two forms together.
 */
#define V9X_D3D_SOFT_SUBPIXEL_SCALE 16.0f

typedef char v9x_assert_soft_subpixel[
    (V9X_D3D_RASTER_SUBPIXEL_ONE == 16l) ? 1 : -1];

/*
 * What the rasterizer will accept, which is not what it can currently draw.
 *
 * 16 bpp only, matching the core's RGB565 surface validation and the ViRGE
 * path's own limit - widening it is a separate change with its own evidence.
 * The pitch ceiling and alignment are the ViRGE's numbers deliberately: a CPU
 * writing through a pointer has no such limits, but the core validates render
 * targets against these before any engine sees them, and loosening them here
 * would let a surface through on the software path that the hardware path
 * refuses, which is a difference nobody asked for while the two are supposed
 * to be interchangeable.
 *
 * APPEND ONLY, positionally initialised, every member arithmetic - see the
 * comment on V9X_D3D_ENGINE_LIMITS. A field inserted mid-struct here reassigns
 * every value after it with no diagnostic, which is how the clipper's guard
 * band once became sixteen pixels.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_soft_limits = {
    16ul,           /* target_bits_per_pixel */
    0x00000ff8ul,   /* target_pitch_max */
    8ul,            /* target_pitch_align */
    V9X_D3D_SOFT_DIMENSION_MAX, /* target_dimension_max */
    4ul,            /* texture_size_min */
    512ul,          /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul            /* depth_bits_per_pixel */
};

/*
 * No texture is sampled yet, so no surface is acceptable as one.
 *
 * Declining here is what keeps the caps honest: the core asks this before
 * binding a texture, so a refusal is the driver saying it cannot texture
 * rather than texturing wrongly. describe_caps below advertises no texture
 * formats to match.
 */
static int v9x_d3d_soft_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    (void)surface;
    (void)format_out;
    return 0;
}

/*
 * A CPU rasterizer is always ready.
 *
 * The ViRGE's answer to this validates a 2D engine, a mapped MMIO window and
 * a status register. This engine has none of those and needs none: if the core
 * resolved it and handed it a context, it can write pixels. Returning the
 * ViRGE's answer here - which is what the chip-neutral code used to ask
 * unconditionally - would have made every draw a no-op on every card without
 * an S3D unit, silently.
 */
static int v9x_d3d_soft_ready(void)
{
    return 1;
}

/*
 * Advertise the minimum that gets a device created, and nothing beyond it.
 *
 * Every bit here is one this stage can be held to. No texture caps, no
 * Z-buffer depths, no blend or fog: those go in when the rasterizer does them
 * and a pixel test says so, one at a time. The characteristic failure of this
 * driver's Direct3D work has been publishing a capability and not implementing
 * it - depth testing was advertised complete for weeks while the engine wrote
 * Z_BASE = 0 - and a second engine is a second chance to make exactly that
 * mistake.
 *
 * dwDeviceRenderBitDepth is the one non-obvious entry: without DDBD_16 the
 * runtime creates no device at all, so it is the floor rather than a claim
 * about quality.
 */
static void v9x_d3d_soft_describe_caps(V9X_DD_SHARED *shared)
{
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps = V9X_D3DDEVCAPS_FLOATTLVERTEX;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = 0ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
    shared->d3d_global.dwNumTextureFormats = 0ul;
    shared->d3d_global.lpTextureFormats = 0ul;
}

/*
 * A screen coordinate, in the rasterizer's 28.4 fixed point.
 *
 * v9x_float_to_long, not a C cast: the HAL links no runtime and Watcom lowers
 * the cast to __CHP. The inline fistp in ddhal_internal.h is what the ViRGE
 * path uses for the same reason, and it rounds to nearest, so a vertex on a
 * subpixel boundary lands on the same side here as it does there.
 *
 * The clamp is the rasterizer's precondition, and it is why that file can
 * refuse an out-of-range coordinate rather than wrap one. The core's clipper
 * has already cut the triangle to [0, extent - 1] in both axes, so this is a
 * no-op on every vertex it will actually see - but "already clipped" is a
 * claim about another file, and the surface being written here is also the
 * desktop.
 */
static v9x_s32 v9x_d3d_soft_coordinate(float value, DWORD extent)
{
    LONG fixed = v9x_float_to_long(value * V9X_D3D_SOFT_SUBPIXEL_SCALE);
    LONG limit = ((LONG)extent - 1l) << V9X_D3D_RASTER_SUBPIXEL_BITS;

    if (fixed < 0l) {
        fixed = 0l;
    }
    if (fixed > limit) {
        fixed = limit;
    }
    return (v9x_s32)fixed;
}

/*
 * One DDHAL vertex as the rasterizer wants it.
 *
 * The colour is 0x00RRGGBB - the core has already folded the specular and fog
 * contributions into it - and is unpacked into separate channels because each
 * one is interpolated independently. Packing to RGB565 happens per pixel, at
 * the far end of the interpolation, not here.
 */
static void v9x_d3d_soft_vertex(const V9X_D3DTLVERTEX *source,
                                const V9X_D3D_CONTEXT *context,
                                V9X_D3D_RASTER_VERTEX *result)
{
    result->x = v9x_d3d_soft_coordinate(source->sx, context->width);
    result->y = v9x_d3d_soft_coordinate(source->sy, context->height);
    result->red = (v9x_s32)((source->color >> 16) & 0xfful);
    result->green = (v9x_s32)((source->color >> 8) & 0xfful);
    result->blue = (v9x_s32)(source->color & 0xfful);
}

/*
 * Rasterize the batch, one triangle at a time.
 *
 * The render target is described once and handed down: the rasterizer takes a
 * pointer, a pitch and an extent and has no opinion about where that memory
 * is. That is deliberate - work-order step 1 of mode 2 is a measurement of
 * VRAM against system-memory write cost, and its answer has to be able to
 * change which pointer goes in here without touching any arithmetic.
 *
 * A refused triangle stops the batch, which is the contract V9X_D3D_ENGINE_OPS
 * states: non-zero when every triangle was emitted, zero on the first that was
 * not. The rasterizer refuses only what it cannot carry safely, and
 * v9x_d3d_soft_vertex has already clamped every coordinate into range, so a
 * zero here means the render target itself is not one this engine can write.
 */
static int v9x_d3d_soft_draw_triangles(V9X_D3D_CONTEXT *context,
                                       const V9X_D3DTLVERTEX *vertices,
                                       DWORD triangle_count)
{
    V9X_D3D_RASTER_TARGET target;
    DWORD index;

    if (context == 0 || vertices == 0 || v9x_hal == 0) {
        return 0;
    }

    target.pixels = (void *)(v9x_hal->fb.linear_base + context->target_offset);
    target.pitch = context->pitch;
    target.width = context->width;
    target.height = context->height;
    if (!v9x_d3d_raster_target_valid(&target)) {
        return 0;
    }

    for (index = 0ul; index < triangle_count; ++index) {
        V9X_D3D_RASTER_VERTEX triangle[3];
        DWORD corner;

        for (corner = 0ul; corner < 3ul; ++corner) {
            v9x_d3d_soft_vertex(&vertices[index * 3ul + corner], context,
                                &triangle[corner]);
        }
        if (!v9x_d3d_raster_triangle(&target, triangle)) {
            return 0;
        }
    }
    return 1;
}

const V9X_D3D_ENGINE_OPS v9x_d3d_engine_soft = {
    &v9x_d3d_soft_limits,
    v9x_d3d_soft_texture_format,
    v9x_d3d_soft_describe_caps,
    v9x_d3d_soft_draw_triangles,
    v9x_d3d_soft_ready
};
