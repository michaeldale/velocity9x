/*
 * The CPU rasterizer's engine, selected by mode rather than by chip.
 *
 * STAGE 1 OF MODE 2, AND DELIBERATELY NOT A RASTERIZER YET. draw_triangles
 * fills each triangle's bounding box with a flat colour. That is not a
 * rendering technique, it is an instrument: it makes the whole path visible
 * end to end - the SYSTEM.INI mode, the capability stamp, engine selection,
 * caps publication, context creation, the clipper, and the draw call arriving
 * here with usable vertices - on a card with no 3D engine, before a single
 * line of edge-stepping arithmetic exists.
 *
 * The reason is this project's own history. The depth path was written
 * complete and then spent two weeks proving that the thing wrong with it was
 * not the depth arithmetic; the D3D split published nothing at all and looked
 * like a caps bug. When a first rasterizer doubles as the first test of six
 * other things, a black screen says nothing about which of the seven is
 * broken. A coloured rectangle where a triangle was asked for says the other
 * six work.
 *
 * So the acceptance test for this file is not "does it look right" - it will
 * look wrong, on purpose. It is: does a rectangle appear, at the right place,
 * on a Trio64.
 *
 * docs\plans\s3-trio64-voodoo2-hybrid-3d.md, mode 2, work-order step 5.
 */
#include "d3d_internal.h"

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
    2048ul,         /* target_dimension_max */
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

/* RGB565 from the vertex's 0x00RRGGBB colour, which is what the core has
 * already normalised the specular and fog contributions into. */
static WORD v9x_d3d_soft_rgb565(DWORD colour)
{
    return (WORD)(((colour >> 8) & 0xf800ul) |
                  ((colour >> 5) & 0x07e0ul) |
                  ((colour >> 3) & 0x001ful));
}

/*
 * Fill each triangle's bounding box, clipped to the render target.
 *
 * The colour comes from the first vertex, so a Gouraud triangle reads as one
 * flat block - which is the point: it is unmistakably not a rendered triangle,
 * so nobody can mistake this stage for working rasterisation, while still
 * proving the vertices arrived with sane coordinates and a sane colour.
 *
 * The bounds are recomputed per triangle and clamped rather than trusted. The
 * core clips against the guard band before calling, but a rasterizer that
 * trusts its input and writes outside the surface corrupts video memory
 * belonging to something else, and this one runs on cards whose framebuffer is
 * also the desktop.
 */
static int v9x_d3d_soft_draw_triangles(V9X_D3D_CONTEXT *context,
                                       const V9X_D3DTLVERTEX *vertices,
                                       DWORD triangle_count)
{
    BYTE *base;
    DWORD index;

    if (context == 0 || vertices == 0 || v9x_hal == 0) {
        return 0;
    }
    if (context->width == 0ul || context->height == 0ul ||
        context->pitch == 0ul) {
        return 0;
    }
    base = (BYTE *)(v9x_hal->fb.linear_base + context->target_offset);

    for (index = 0ul; index < triangle_count; ++index) {
        const V9X_D3DTLVERTEX *v = &vertices[index * 3ul];
        float left = v[0].sx;
        float right = v[0].sx;
        float top = v[0].sy;
        float bottom = v[0].sy;
        WORD colour = v9x_d3d_soft_rgb565(v[0].color);
        LONG x0;
        LONG x1;
        LONG y0;
        LONG y1;
        LONG y;
        DWORD corner;

        for (corner = 1ul; corner < 3ul; ++corner) {
            if (v[corner].sx < left) {
                left = v[corner].sx;
            }
            if (v[corner].sx > right) {
                right = v[corner].sx;
            }
            if (v[corner].sy < top) {
                top = v[corner].sy;
            }
            if (v[corner].sy > bottom) {
                bottom = v[corner].sy;
            }
        }

        /* v9x_float_to_long, not a C cast: the HAL links no runtime, and
         * Watcom's float-to-int cast pulls in __CHP. The inline fistp in
         * ddhal_internal.h is what the ViRGE path uses for the same reason. */
        x0 = v9x_float_to_long(left);
        x1 = v9x_float_to_long(right);
        y0 = v9x_float_to_long(top);
        y1 = v9x_float_to_long(bottom);

        if (x0 < 0l) {
            x0 = 0l;
        }
        if (y0 < 0l) {
            y0 = 0l;
        }
        if (x1 > (LONG)context->width) {
            x1 = (LONG)context->width;
        }
        if (y1 > (LONG)context->height) {
            y1 = (LONG)context->height;
        }
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }

        for (y = y0; y < y1; ++y) {
            WORD *row = (WORD *)(base + (DWORD)y * context->pitch);
            LONG x;

            for (x = x0; x < x1; ++x) {
                row[x] = colour;
            }
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
