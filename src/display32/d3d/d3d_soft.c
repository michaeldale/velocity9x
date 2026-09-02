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

/* The depth and texture-coordinate scales, on the same terms. */
#define V9X_D3D_SOFT_DEPTH_SCALE 65535.0f
#define V9X_D3D_SOFT_TEXCOORD_SCALE 65535.0f

typedef char v9x_assert_soft_texcoord[
    (V9X_D3D_RASTER_TEXCOORD_MAX == 65535l) ? 1 : -1];

typedef char v9x_assert_soft_depth[
    (V9X_D3D_RASTER_DEPTH_MAX == 65535l) ? 1 : -1];

/*
 * The rasterizer numbers its comparison functions as Direct3D numbers them, so
 * the engine hands context->z_func over untranslated. That is only safe while
 * the two agree, and this is what says so. The ViRGE engine needs a real table
 * in the same place, because the S3D encoding differs from D3DCMP - 1 in six
 * of the eight - and a driver that silently used the wrong order would draw a
 * scene that is inside out rather than one that is missing.
 */
/*
 * Filter and blend travel from the render state to the sampler untranslated
 * too, so they get the same treatment. Only the two values each that are
 * implemented are asserted: the mip filters have no meaning without mip
 * selection, and describe_caps advertises none of them.
 */
typedef char v9x_assert_soft_filter[
    (V9X_D3D_RASTER_FILTER_POINT == V9X_D3DFILTER_NEAREST &&
     V9X_D3D_RASTER_FILTER_LINEAR == V9X_D3DFILTER_LINEAR &&
     V9X_D3D_RASTER_BLEND_DECAL == V9X_D3DTBLEND_DECAL &&
     V9X_D3D_RASTER_BLEND_MODULATE == V9X_D3DTBLEND_MODULATE) ? 1 : -1];

typedef char v9x_assert_soft_compare[
    (V9X_D3D_RASTER_CMP_NEVER == V9X_D3DCMP_NEVER &&
     V9X_D3D_RASTER_CMP_LESS == V9X_D3DCMP_LESS &&
     V9X_D3D_RASTER_CMP_EQUAL == V9X_D3DCMP_EQUAL &&
     V9X_D3D_RASTER_CMP_LESSEQUAL == V9X_D3DCMP_LESSEQUAL &&
     V9X_D3D_RASTER_CMP_GREATER == V9X_D3DCMP_GREATER &&
     V9X_D3D_RASTER_CMP_NOTEQUAL == V9X_D3DCMP_NOTEQUAL &&
     V9X_D3D_RASTER_CMP_GREATEREQUAL == V9X_D3DCMP_GREATEREQUAL &&
     V9X_D3D_RASTER_CMP_ALWAYS == V9X_D3DCMP_ALWAYS) ? 1 : -1];

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
 * Whether this surface is one the sampler can read, and as which format.
 *
 * The same two the ViRGE accepts and classified the same way, deliberately:
 * the two engines are meant to be interchangeable, and a software path taking
 * formats the hardware path refuses is a difference nobody asked for. The
 * value written out is this file's own vocabulary - the core carries it back
 * through the context and never interprets it.
 *
 * ddpfSurface exists only when the surface carries its own format, which
 * DDRAWISURF_HASPIXELFORMAT reports; without it the surface is in the
 * primary's format, and reading the field anyway would also read past the
 * allocation, since the DDK only allocates it in the differing case.
 */
static int v9x_d3d_soft_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *pixel;

    if (surface == 0 || surface->lpGbl == 0 || format_out == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) == 0ul) {
        return 0;
    }
    pixel = &surface->lpGbl->ddpfSurface;
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        return 0;
    }
    if (pixel->dwRBitMask == 0x00007c00ul &&
        pixel->dwGBitMask == 0x000003e0ul &&
        pixel->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_D3D_RASTER_TEXFMT_ARGB1555;
        return 1;
    }
    if (pixel->dwRBitMask == 0x00000f00ul &&
        pixel->dwGBitMask == 0x000000f0ul &&
        pixel->dwBBitMask == 0x0000000ful) {
        *format_out = V9X_D3D_RASTER_TEXFMT_ARGB4444;
        return 1;
    }
    /*
     * RGB565, which the ViRGE's classifier deliberately does not have. It is
     * the format of the display on every target this driver serves, so it is
     * the one an application is most likely to hand over - and the S3D
     * texture unit cannot sample it, which is why only this engine accepts it.
     */
    if (pixel->dwRBitMask == 0x0000f800ul &&
        pixel->dwGBitMask == 0x000007e0ul &&
        pixel->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_D3D_RASTER_TEXFMT_RGB565;
        return 1;
    }
    return 0;
}

/*
 * The bound texture, as the sampler wants it, or zero when there is none.
 *
 * Every constraint checked here is one describe_caps declares, and the
 * pairing is the point: POW2 and SQUAREONLY are advertised because this
 * refuses anything else, and a driver that refused without declaring would
 * leave an application no way to comply - its textures would simply be
 * dropped with every call succeeding.
 *
 * Mip levels are not read. A mipmapped surface is accepted and its top level
 * sampled, which is what the filter mapping below already implies, and no mip
 * filter cap is published.
 */
static int v9x_d3d_soft_texture_setup(const V9X_D3D_CONTEXT *context,
                                      V9X_D3D_RASTER_TEXTURE *texture)
{
    V9X_DD_SURFACE_LCL *surface =
        v9x_d3d_context_texture_surface((V9X_D3D_CONTEXT *)context);
    DWORD format;
    DWORD size;
    DWORD offset;
    DWORD last_byte;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    if (surface->lpGbl->wWidth != surface->lpGbl->wHeight) {
        return 0;
    }
    size = (DWORD)surface->lpGbl->wWidth;
    if (size < V9X_D3D_RASTER_TEXTURE_SIZE_MIN ||
        size > V9X_D3D_RASTER_TEXTURE_SIZE_MAX) {
        return 0;
    }
    if (!v9x_d3d_soft_texture_format(surface, &format)) {
        return 0;
    }

    /* The whole surface has to sit inside video memory, because the sampler
     * indexes it with a wrapped texel index and never re-checks. A mipmapped
     * chain adds a third again, the same arithmetic the ViRGE path uses. */
    offset = v9x_surface_offset(surface);
    last_byte = (DWORD)surface->lpGbl->lPitch * size;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        last_byte += last_byte / 3ul;
    }
    if (offset == 0xfffffffful || v9x_hal == 0 ||
        last_byte > v9x_hal->fb.vram_bytes ||
        offset > v9x_hal->fb.vram_bytes - last_byte) {
        return 0;
    }

    texture->pixels = (void *)(v9x_hal->fb.linear_base + offset);
    texture->pitch = (DWORD)surface->lpGbl->lPitch;
    texture->size = size;
    texture->format = format;
    /*
     * One filter for both minification and magnification, taken from the
     * magnification state.
     *
     * Not a shortcut: choosing per pixel needs a texel-density derivative this
     * rasterizer does not compute, and the minification states an application
     * sets are mostly the MIP variants, which mean nothing without mip
     * selection. Mapping those to point would make a LINEARMIPLINEAR request
     * come out sharper than a LINEAR one, which is backwards. So the
     * magnification state decides, and the caps publish NEAREST and LINEAR
     * only.
     */
    texture->filter = context->texture_mag == V9X_D3DFILTER_LINEAR
        ? V9X_D3D_RASTER_FILTER_LINEAR : V9X_D3D_RASTER_FILTER_POINT;
    /*
     * DECAL and MODULATE, with MODULATE the default. COPY is not published and
     * lands here as DECAL, which differs from it only in alpha - and no alpha
     * is sampled.
     */
    texture->blend = context->texture_blend == V9X_D3DTBLEND_DECAL
        ? V9X_D3D_RASTER_BLEND_DECAL : V9X_D3D_RASTER_BLEND_MODULATE;
    return v9x_d3d_raster_texture_valid(texture);
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
 * Advertise what steps 6 to 8 verified, and nothing else.
 *
 * The characteristic failure of this driver's Direct3D work has been
 * publishing a capability and not implementing it: depth testing was advertised
 * complete for weeks while the engine wrote Z_BASE = 0, set no depth bits and
 * never read context->zbuffer, so applications got depth accepted, ignored, and
 * drew in submission order. A second engine is a second chance to make exactly
 * that mistake, so every bit below is one a pixel test can hold this file to,
 * and the ones deliberately absent are listed with their reasons.
 *
 * Not published, each because nothing implements it:
 *
 *   - D3DPTADDRESSCAPS_WRAP. The sampler clamps. The texture coordinate range
 *     holds exactly one repeat, which is an overflow bound in the edge
 *     interpolator rather than a choice - see V9X_D3D_RASTER_TEXCOORD_MAX.
 *     CLAMP is published because clamping is what it actually does.
 *   - The four mip filter caps. Nothing selects a mip level; a mipmapped
 *     surface is accepted and its top level sampled.
 *   - D3DPTEXTURECAPS_ALPHA, and every alpha blend cap. Both texture formats
 *     carry alpha and the sampler decodes neither.
 *   - D3DPTEXTURECAPS_PERSPECTIVE. Interpolation is linear in screen space.
 *   - D3DPTBLENDCAPS_COPY. Distinguished from DECAL only by alpha.
 *   - The fog caps. The core folds fog into the vertex colour before the
 *     engine sees it, so a flat-shaded fogged triangle already comes out
 *     right - but the probe's fog rung is the only evidence for that, and it
 *     is one rung. It goes in when there is a ladder behind it.
 *
 * Published and each held to a measured pixel: RGB colour model, float TL
 * vertices, execute buffers from system memory, texturing from device memory,
 * 16-bit render target, 16-bit Z, all eight comparison functions, flat and
 * Gouraud RGB shading, subpixel setup, point and bilinear filtering, decal and
 * modulate blending, square power-of-two textures, clamped addressing.
 *
 * D3DDEVCAPS_EXECUTESYSTEMMEMORY is the one non-obvious entry and it is not a
 * claim about parsing: a DirectX 2/3-era title renders only through execute
 * buffers and selects its device by that bit, so omitting it makes the
 * application discard the HAL and fall back to a software device. The runtime
 * decomposes the buffers into RenderState and RenderPrimitive calls, which is
 * why the DDK's own ViRGE sample sets the bit while leaving the Execute
 * callbacks null.
 */
static void v9x_d3d_soft_describe_caps(V9X_DD_SHARED *shared)
{
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH |
        V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        /*
         * "Device can texture from device memory", and only from there:
         * v9x_d3d_soft_texture_setup rejects any surface carrying
         * DDSCAPS_SYSTEMMEMORY, because the sampler reaches the texture
         * through the framebuffer's linear aperture. That is a constraint the
         * software engine inherits from how it addresses memory rather than
         * from any hardware, and it is real either way.
         */
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    /* Left empty, and D3DDD_LINECAPS unset: nothing here rasterises a line. */
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);
    shared->d3d_global.hwCaps.dpcTriCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);
    shared->d3d_global.hwCaps.dpcTriCaps.dwMiscCaps =
        V9X_D3DPMISCCAPS_CULLNONE;
    shared->d3d_global.hwCaps.dpcTriCaps.dwRasterCaps =
        V9X_D3DPRASTERCAPS_ZTEST | V9X_D3DPRASTERCAPS_SUBPIXEL;
    shared->d3d_global.hwCaps.dpcTriCaps.dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    shared->d3d_global.hwCaps.dpcTriCaps.dwSrcBlendCaps = 0ul;
    shared->d3d_global.hwCaps.dpcTriCaps.dwDestBlendCaps = 0ul;
    shared->d3d_global.hwCaps.dpcTriCaps.dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB |
        V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        /*
         * Specular. The core folds the specular contribution into the vertex
         * colour before draw_triangles sees it, so this engine got it without
         * a line being written for it - and unlike the fog caps below, it has
         * a measured pixel: D3DSpecularGouraudOk on the Trio64, 2026-09-01.
         */
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureCaps =
        V9X_D3DPTEXTURECAPS_POW2 | V9X_D3DPTEXTURECAPS_SQUAREONLY;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureFilterCaps =
        V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureBlendCaps =
        V9X_D3DPTBLENDCAPS_DECAL | V9X_D3DPTBLENDCAPS_MODULATE;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureAddressCaps =
        V9X_D3DPTADDRESSCAPS_CLAMP;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;

    /* ARGB1555 first, ARGB4444 second, the same two the sampler classifies
     * and in the same order the ViRGE publishes them. */
    shared->texture_formats[0].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[0].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[0].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    /*
     * DDPF_ALPHAPIXELS is set although the sampler ignores alpha, and that is
     * not the advertise-then-ignore pattern: it describes the surface layout
     * an application must create, not a capability the engine offers. Omitting
     * it would describe a format with a spare bit and no way to say which.
     * What is not claimed is D3DPTEXTURECAPS_ALPHA, which is the capability.
     */
    shared->texture_formats[0].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[0].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->texture_formats[1].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[1].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[1].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[1].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    /*
     * RGB565, third and last, and the one entry the ViRGE's list does not
     * carry. No DDPF_ALPHAPIXELS and no alpha mask: the format has no alpha
     * bit to describe, which is a statement about the layout rather than about
     * this engine's alpha support - that is still absent from
     * D3DPTEXTURECAPS_ALPHA above.
     *
     * It is published because it is the display's own format, so it is what an
     * application converting a bitmap for a 16-bit screen will produce, and
     * because DirectX's own diagnostic refuses a driver that offers no texture
     * format matching the display
     * (docs\issues\2026-09-02-dxdiag-fails-at-enumtextureformats.md).
     */
    shared->texture_formats[2].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[2].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[2].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[2].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[2].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->d3d_global.dwNumTextureFormats = 3ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
}

/* RGB565 packing lives in d3d_raster.c now; the engine hands it channels. */

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
 * A vertex depth, scaled into the rasterizer's 16-bit buffer.
 *
 * sz is 0..1 and the buffer holds 0..65535, so this is a multiply and a clamp
 * - the same shape as the coordinate above and for the same reason. The
 * clamping is not defensive: sz = 1.0 is ordinary geometry, a cleared depth
 * buffer's far plane and any unprojected background quad, and it is exactly
 * the value that broke the ViRGE's 1.31 conversion by landing one past the
 * largest representable integer. Here the scale is 65535 rather than 2^31, so
 * the arithmetic has room - but the same input still has to be answered rather
 * than assumed away, and a NaN has to resolve to the far plane so that garbage
 * is occluded instead of occluding. The comparisons are written so a NaN takes
 * the else arm: !(value > 0.0f) is true for a NaN and value >= 1.0f is false.
 * d3d_zfixed.c is that argument at length.
 */
static v9x_s32 v9x_d3d_soft_depth(float value)
{
    if (value >= 1.0f) {
        return (v9x_s32)V9X_D3D_RASTER_DEPTH_MAX;
    }
    if (!(value > 0.0f)) {
        return value == value ? 0l : (v9x_s32)V9X_D3D_RASTER_DEPTH_MAX;
    }
    return (v9x_s32)v9x_float_to_long(value * V9X_D3D_SOFT_DEPTH_SCALE);
}

/*
 * A texture coordinate, scaled into the sampler's 0..65535 range.
 *
 * Clamped to one repeat rather than wrapped, because the range holds exactly
 * one - see V9X_D3D_RASTER_TEXCOORD_MAX for the arithmetic, and note that
 * describe_caps therefore publishes CLAMP and not WRAP. An application that
 * asks for tiling gets the edge texel stretched instead, which is wrong but
 * visibly wrong, where wrapping a coordinate the interpolator cannot carry
 * would put a seam in the middle of every triangle.
 *
 * NaN takes the else arm and resolves to zero, on the same reasoning as the
 * depth conversion above.
 */
static v9x_s32 v9x_d3d_soft_texcoord(float value)
{
    if (value >= 1.0f) {
        return (v9x_s32)V9X_D3D_RASTER_TEXCOORD_MAX;
    }
    if (!(value > 0.0f)) {
        return 0l;
    }
    return (v9x_s32)v9x_float_to_long(value * V9X_D3D_SOFT_TEXCOORD_SCALE);
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
    result->z = v9x_d3d_soft_depth(source->sz);
    result->u = v9x_d3d_soft_texcoord(source->tu);
    result->v = v9x_d3d_soft_texcoord(source->tv);
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
    V9X_D3D_RASTER_DEPTH depth;
    const V9X_D3D_RASTER_DEPTH *depth_arg = 0;
    V9X_D3D_RASTER_TEXTURE texture;
    const V9X_D3D_RASTER_TEXTURE *texture_arg = 0;
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

    /*
     * The same three-part test the ViRGE engine's z_active makes, copied
     * deliberately rather than reduced to the render state. An application may
     * legally set ZENABLE with no Z buffer bound, and the runtime often
     * replays a whole default state block; acting on the render state alone
     * would point the depth unit at depth_offset 0, which is the visible
     * framebuffer. The DDK makes the same test - D3DRENDR.C:266.
     */
    if (context->z_enable != 0ul && context->zbuffer != 0 &&
        context->depth_pitch != 0ul) {
        depth.pixels = (void *)(v9x_hal->fb.linear_base +
                                context->depth_offset);
        depth.pitch = context->depth_pitch;
        depth.compare = context->z_func;
        depth.write = context->z_write;
        if (!v9x_d3d_raster_depth_valid(&depth, &target)) {
            return 0;
        }
        depth_arg = &depth;
    }

    /*
     * An unusable texture draws untextured rather than refusing the batch.
     *
     * The two are not the same and the difference matters: a refusal reports
     * failure for a draw the application asked for legally - the runtime
     * replays default state blocks that bind handles this engine has not
     * accepted - whereas dropping the texture draws the Gouraud colour, which
     * is what an engine with no sampler would have done and is visibly
     * untextured rather than absent.
     */
    if (context->texture_handle != 0ul &&
        v9x_d3d_soft_texture_setup(context, &texture)) {
        texture_arg = &texture;
    }

    for (index = 0ul; index < triangle_count; ++index) {
        V9X_D3D_RASTER_VERTEX triangle[3];
        DWORD corner;

        for (corner = 0ul; corner < 3ul; ++corner) {
            v9x_d3d_soft_vertex(&vertices[index * 3ul + corner], context,
                                &triangle[corner]);
        }
        if (!v9x_d3d_raster_triangle(&target, depth_arg, texture_arg,
                                     triangle)) {
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
