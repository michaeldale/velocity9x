/*
 * ATI Rage 128 Direct3D engine, through the CCE.
 *
 * Phase 5 of docs\plans\ati-rage128.md. The packet streams come from
 * src\chipsets\ati\rage128\r128_3d.c and the translation from the neutral
 * draw from d3d_r128_map.c, both host-tested; the CCE probe drew the same
 * streams right on a Rage Fury Pro: Z16, a texture texel for texel, flat and
 * Gouraud colour (docs\decisions\2026-10-23-rage128-3d-builder-gouraud-z-
 * texture.md). This file resolves DirectDraw surfaces, places textures and
 * Z buffers, and submits.
 *
 * Submission is the CCE in PIO mode, in engines\r128_cce.c: started for
 * the first batch and left running, then drained - finished, caches
 * flushed, stopped back to NONPM4 - by whatever needs the drawing: Flip,
 * Lock, the blit status and eng_r128.c's own fills and copies.
 *
 * The vertices go as they are: V9X_R3D_VERTEX is the CCE's TL vertex. A
 * triangle with a coordinate that is not finite, or textured with an RHW
 * at or below zero, is left out of the stream.
 *
 * Emission order is the refusal contract: the mapping and the state are
 * built and the whole batch assembled first, and only then is the first
 * register written, so a refused batch writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_r128_map.h"
#include "velocity9x/r128_2d.h"
#include "velocity9x/r128_3d.h"

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_R128_MAX_TRIANGLES 64ul

/* The register window the mini-VDD maps (eng_r128.c's). */
#define V9X_D3D_R128_MMIO_BYTES 0x00004000ul
#define V9X_D3D_R128_APERTURE_BYTES 0x02000000ul

/* Texels and Z: textures placed 32-byte aligned (the offset registers count
 * 32-byte units), rows packed (TEX_SIZE_PITCH_C takes the pitch as the
 * width's log2); Z16 32-byte aligned with its pitch a whole number of
 * 8-pixel units. */
#define V9X_D3D_R128_SURFACE_ALIGN 32ul
#define V9X_D3D_R128_Z_PITCH_UNIT 16ul

/* Format, control, header: the stream a whole batch can make, rounded up
 * to a pair. */
#define V9X_D3D_R128_STREAM_DWORDS \
    (V9X_R128_WAIT_DWORDS + \
     V9X_R128_STATE_DWORDS_MAX + V9X_R128_PRIM_HEADER_DWORDS + \
     V9X_D3D_R128_MAX_TRIANGLES * 3ul * V9X_R128_VERTEX_DWORDS + \
     V9X_R128_FLUSH_DWORDS + 2ul)

/* Fault-trace tag: "R13D". */
#define V9X_D3D_R128_FAULT_TAG 0x52313344ul

static struct v9x_r128_3d_stream v9x_d3d_r128_state_stream;
static v9x_u32 v9x_d3d_r128_stream[V9X_D3D_R128_STREAM_DWORDS];

/* 16 and 32 bpp targets; Z16; textures powers of two to 1024; the core
 * clips, because the setup engine has not been measured drawing a vertex
 * off the target; pixel centres at +0.5, as the probe's texture mapped
 * texel for texel from whole-pixel edges. */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_r128_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max: 2047 units of 8 pixels */
    16ul,           /* target_pitch_align */
    2048ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    1024ul,         /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_R128_SURFACE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own: Z_PITCH_C takes the pitch */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    1ul,            /* pixel_centre_half */
    0ul,            /* depth_tiled: linear, read back so */
    32752ul,        /* target_pitch_max_xrgb8888 */
    0ul             /* depth32_bits: Z16 only for now */
};

static DWORD v9x_d3d_r128_vram_base(void)
{
    return v9x_hal->fb.physical_base & (V9X_D3D_R128_APERTURE_BYTES - 1ul);
}

/* RGB565, ARGB1555 and ARGB4444 at 16 bits, ARGB8888 at 32: the datatype,
 * or 0 for a format this engine does not sample. */
static DWORD v9x_d3d_r128_datatype_of(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *bytes_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0ul;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return 0ul;
    }
    if (format->dwRGBBitCount == 16ul) {
        *bytes_out = 2ul;
        if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
            return format->dwRBitMask == 0x0000f800ul &&
                   format->dwGBitMask == 0x000007e0ul &&
                   format->dwBBitMask == 0x0000001ful ?
                   V9X_R128_TEX_RGB565 : 0ul;
        }
        if (format->dwRBitMask == 0x00007c00ul &&
            format->dwGBitMask == 0x000003e0ul &&
            format->dwBBitMask == 0x0000001ful &&
            format->dwRGBAlphaBitMask == 0x00008000ul) {
            return V9X_R128_TEX_ARGB1555;
        }
        if (format->dwRBitMask == 0x00000f00ul &&
            format->dwGBitMask == 0x000000f0ul &&
            format->dwBBitMask == 0x0000000ful &&
            format->dwRGBAlphaBitMask == 0x0000f000ul) {
            return V9X_R128_TEX_ARGB4444;
        }
        return 0ul;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *bytes_out = 4ul;
        return V9X_R128_TEX_ARGB8888;
    }
    return 0ul;
}

static int v9x_d3d_r128_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    DWORD bytes = 0ul;
    DWORD datatype;

    if (format_out == 0) {
        return 0;
    }
    datatype = v9x_d3d_r128_datatype_of(surface, &bytes);
    *format_out = datatype != 0ul ? datatype : V9X_D3D_R128_TEXTURE_UNKNOWN;
    return datatype != 0ul;
}

/*
 * What the builder expresses and d3d_r128_map.c accepts: Z and alpha tests
 * with every function, the blend factors without destination alpha, flat
 * and Gouraud with specular and vertex fog, textures to 1024 in four
 * formats with mip chains, every filter, wrap and clamp. Not measured
 * beyond the probe's Z16, nearest-replace texture and colour: the rest is
 * the builder's encoding of Mesa's choices.
 */
static void v9x_d3d_r128_describe_caps(V9X_DD_SHARED *shared)
{
    V9X_D3DPRIMCAPS *tri;
    DWORD index;

    if (shared == 0) {
        return;
    }
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
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);

    tri = &shared->d3d_global.hwCaps.dpcTriCaps;
    tri->dwSize = sizeof(V9X_D3DPRIMCAPS);
    tri->dwMiscCaps = V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
                      V9X_D3DPMISCCAPS_CULLCCW | V9X_D3DPMISCCAPS_MASKZ;
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_ZTEST |
                        V9X_D3DPRASTERCAPS_SUBPIXEL |
                        V9X_D3DPRASTERCAPS_FOGVERTEX |
                        V9X_D3DPRASTERCAPS_FOGTABLE;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_DESTCOLOR | V9X_D3DPBLENDCAPS_INVDESTCOLOR;
    tri->dwDestBlendCaps = tri->dwSrcBlendCaps;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR |
                               V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
                               V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_r128_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_r128_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_r128_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_r128_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555, ARGB4444, ARGB8888. */
    for (index = 0ul; index < 4ul; ++index) {
        V9X_DDPIXELFORMAT *pf = &shared->texture_formats[index].ddpfPixelFormat;

        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
        pf->dwSize = sizeof(V9X_DDPIXELFORMAT);
        pf->dwRGBBitCount = 16ul;
        pf->dwFlags = V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[3].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[3].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBAlphaBitMask =
        0xff000000ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 4ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* eng_r128.c's conditions, CAP_D3D, the window mapped, and no timeout. */
static int v9x_d3d_r128_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RAGE128 &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_R128_MMIO_BYTES;
}

/* The bound texture's format, shape and the offset of each level of its
 * chain: each attached mip level whose sides halve and whose rows are
 * packed, as create_surface places them. The first that is not ends it. */
static void v9x_d3d_r128_resolve_texture(const V9X_R3D_DRAW *draw,
                                         V9X_D3D_R128_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface;
    const V9X_DD_SURFACE_LCL *level;
    DWORD bytes = 0ul;
    DWORD offset;
    DWORD width;
    DWORD height;

    texture->datatype = V9X_D3D_R128_TEXTURE_UNKNOWN;
    texture->has_alpha = 0;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->bytes_per_texel = 0ul;
    texture->pitch_bytes = 0ul;
    texture->levels = 0ul;

    surface = (const V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l) {
        return;
    }
    texture->datatype = v9x_d3d_r128_datatype_of(surface, &bytes);
    if (texture->datatype == 0ul) {
        texture->datatype = V9X_D3D_R128_TEXTURE_UNKNOWN;
        return;
    }
    texture->has_alpha = texture->datatype != V9X_R128_TEX_RGB565;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->bytes_per_texel = bytes;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->offsets[0] = offset;
    texture->levels = 1ul;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) {
        return;
    }

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    level = surface;
    width = texture->width;
    height = texture->height;
    while (texture->levels < V9X_R128_TEXTURE_LEVELS &&
           (width > 1ul || height > 1ul)) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0 || next->lpGbl == 0) {
            break;
        }
        width = width > 1ul ? width >> 1 : 1ul;
        height = height > 1ul ? height >> 1 : 1ul;
        offset = v9x_surface_offset(next);
        if ((DWORD)next->lpGbl->wWidth != width ||
            (DWORD)next->lpGbl->wHeight != height ||
            (DWORD)next->lpGbl->lPitch != width * bytes ||
            offset == 0xfffffffful ||
            offset % V9X_D3D_R128_SURFACE_ALIGN != 0ul) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            break;
        }
        texture->offsets[texture->levels++] = offset;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = texture->levels;
}

/* Z16 when a depth surface is bound: its bytes per pixel, 0 for none. */
static DWORD v9x_d3d_r128_depth_bytes(const V9X_R3D_DRAW *draw)
{
    const V9X_DD_SURFACE_LCL *depth =
        (const V9X_DD_SURFACE_LCL *)draw->depth.object;

    if (depth == 0) {
        return 0ul;
    }
    return v9x_d3d_depth_bytes_per_pixel(depth);
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the SiS and VSA-100 engines count their own. */
static void v9x_d3d_r128_count_refusal(const V9X_R3D_DRAW *draw,
                                       v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_R128_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_R128_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

static v9x_u32 v9x_d3d_r128_map(const V9X_R3D_DRAW *draw,
                                struct v9x_r128_3d_state *state)
{
    V9X_D3D_R128_TEXTURE resolved;
    v9x_u32 reason;

    v9x_d3d_r128_resolve_texture(draw, &resolved);
    reason = v9x_d3d_r128_map_draw(draw, &resolved,
                                   v9x_d3d_r128_vram_base(),
                                   v9x_hal->fb.vram_bytes,
                                   v9x_d3d_r128_depth_bytes(draw), state);
    if (reason == V9X_D3D_R128_REFUSE_NONE &&
        v9x_r128_3d_build_state(state, &v9x_d3d_r128_state_stream) !=
            V9X_STATUS_OK) {
        reason = V9X_D3D_R128_REFUSE_STATE;
    }
    return reason;
}

static int v9x_d3d_r128_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_r128_3d_state state;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_r128_map(draw, &state);
    if (reason != V9X_D3D_R128_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_R128_REFUSE_NONE;
}

/* A float's exponent all ones: an infinity or a NaN. */
static int v9x_d3d_r128_finite(float value)
{
    union {
        float value;
        DWORD bits;
    } converted;

    converted.value = value;
    return (converted.bits & 0x7f800000ul) != 0x7f800000ul;
}

static int v9x_d3d_r128_triangle_ok(const V9X_R3D_VERTEX *triangle,
                                    int textured)
{
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        const V9X_R3D_VERTEX *v = &triangle[index];

        if (!v9x_d3d_r128_finite(v->sx) || !v9x_d3d_r128_finite(v->sy) ||
            !v9x_d3d_r128_finite(v->sz) || !v9x_d3d_r128_finite(v->rhw)) {
            return 0;
        }
        if (textured &&
            (!(v->rhw > 0.0f) || !v9x_d3d_r128_finite(v->tu) ||
             !v9x_d3d_r128_finite(v->tv))) {
            return 0;
        }
    }
    return 1;
}

static int v9x_d3d_r128_draw(const V9X_R3D_DRAW *draw,
                             const V9X_R3D_VERTEX *vertices,
                             DWORD triangle_count)
{
    struct v9x_r128_3d_state state;
    v9x_u32 header[V9X_R128_PRIM_HEADER_DWORDS];
    DWORD count = 0ul;
    DWORD header_at;
    DWORD kept = 0ul;
    DWORD index;
    DWORD word;
    DWORD started;
    v9x_u32 reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_R128_MAX_TRIANGLES ||
        !v9x_d3d_r128_ready()) {
        return 0;
    }

    /* The mapping and the state first: nothing is written for a draw they
     * refuse. DECODE times them and the batch's assembly; the submit is
     * timed in r128_cce.c. */
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_r128_map(draw, &state);
    if (reason != V9X_D3D_R128_REFUSE_NONE) {
        v9x_d3d_r128_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }

    /* A draw into the buffer the last flip is taking off the screen, while
     * that flip is pending, waits for it in the command stream: with flips
     * queued in the ring (r128_cce.c) the CPU no longer waits for the
     * frame, so the GPU waits for the display instead of drawing into what
     * it is still showing. */
    if (draw->target.offset == v9x_present_retiring_offset() &&
        v9x_flip_pending()) {
        v9x_d3d_r128_stream[count++] =
            V9X_R128_PACKET0(V9X_R128_WAIT_UNTIL, 0u);
        v9x_d3d_r128_stream[count++] = V9X_R128_EVENT_CRTC_OFFSET;
        ++v9x_hal->d3d_diagnostics.draws_flip_waited;
    }

    /* The whole batch, assembled: state, one primitive packet, flush. */
    for (word = 0ul; word < v9x_d3d_r128_state_stream.count; ++word) {
        v9x_d3d_r128_stream[count++] = v9x_d3d_r128_state_stream.dwords[word];
    }
    header_at = count;
    count += V9X_R128_PRIM_HEADER_DWORDS;
    for (index = 0ul; index < triangle_count; ++index) {
        const V9X_R3D_VERTEX *triangle = vertices + index * 3ul;
        const v9x_u32 *words = (const v9x_u32 *)triangle;

        if (!v9x_d3d_r128_triangle_ok(triangle, state.texture.enable != 0ul)) {
            continue;
        }
        for (word = 0ul; word < 3ul * V9X_R128_VERTEX_DWORDS; ++word) {
            v9x_d3d_r128_stream[count++] = words[word];
        }
        ++kept;
    }
    if (kept == 0ul) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 1;
    }
    (void)v9x_r128_3d_build_prim(kept, header);
    for (word = 0ul; word < V9X_R128_PRIM_HEADER_DWORDS; ++word) {
        v9x_d3d_r128_stream[header_at + word] = header[word];
    }
    v9x_r128_3d_build_flush(&v9x_d3d_r128_stream[count]);
    count += V9X_R128_FLUSH_DWORDS;
    if ((count & 1ul) != 0ul) {
        v9x_d3d_r128_stream[count++] = V9X_R128_PACKET2;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);

    /* Left running: whatever needs the drawing drains it (r128_cce.c). */
    if (!v9x_r128_cce_submit(v9x_d3d_r128_stream, count)) {
        return 0;
    }
    v9x_present_note_submission();
    return 1;
}

/* One edge the sampler takes: a power of two in range. */
static int v9x_d3d_r128_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_r128_limits.texture_size_min &&
           edge <= v9x_d3d_r128_limits.texture_size_max;
}

/*
 * Placement. A video-memory texture of a format and size this engine
 * samples gets each level in a block of its own, 32-byte aligned, rows
 * packed: TEX_SIZE_PITCH_C takes the pitch as the width's log2 and every
 * level has its own offset register. A Z16 buffer gets 32-byte alignment
 * and a pitch of whole 8-pixel units, which Z_OFFSET_C and Z_PITCH_C need
 * and DirectDraw's 8-byte heap alignment does not give.
 */
static DWORD v9x_d3d_r128_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_R128_TEXTURE_LEVELS];
    v9x_u32 rows[V9X_R128_TEXTURE_LEVELS];
    DWORD bytes = 0ul;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_R128_TEXTURE_LEVELS || data->lplpSList == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;

    if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (data->dwSCnt != 1ul ||
            v9x_d3d_depth_bytes_per_pixel(surface) != 2ul || width == 0ul ||
            height == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = (width * 2ul + V9X_D3D_R128_Z_PITCH_UNIT - 1ul) &
                     ~(V9X_D3D_R128_Z_PITCH_UNIT - 1ul);
        rows[0] = height;
    } else {
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            !v9x_d3d_r128_texture_edge(width) ||
            !v9x_d3d_r128_texture_edge(height) ||
            v9x_d3d_r128_datatype_of(surface, &bytes) == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        for (level = 0ul; level < data->dwSCnt; ++level) {
            const V9X_DD_SURFACE_LCL *member = list[level];

            if (level != 0ul) {
                if (width == 1ul && height == 1ul) {
                    return V9X_DDHAL_DRIVER_NOTHANDLED;
                }
                width = width > 1ul ? width >> 1 : 1ul;
                height = height > 1ul ? height >> 1 : 1ul;
            }
            if (member == 0 || member->lpGbl == 0 ||
                (data->dwSCnt > 1ul &&
                 (member->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) ||
                (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
                (DWORD)member->lpGbl->wWidth != width ||
                (DWORD)member->lpGbl->wHeight != height) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            pitches[level] = width * bytes;
            rows[level] = height;
        }
    }
    if (v9x_d3d_place_each(data, V9X_D3D_R128_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_r128_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_R128_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_r128 = {
    &v9x_d3d_r128_limits,
    v9x_d3d_r128_texture_format,
    v9x_d3d_r128_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_r128_ready,
    v9x_d3d_r128_create_surface,
    v9x_d3d_r128_destroy_surface,
    v9x_d3d_r128_draw,
    v9x_d3d_r128_accepts
};
