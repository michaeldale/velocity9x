/*
 * AMD Carrizo (Radeon R7E, GCN 3) Direct3D engine, through the graphics
 * command processor.
 *
 * Phase 5 step 1 of docs\plans\ati-carrizo-gfx8-3d.md. The streams come
 * from src\chipsets\ati\vi\vi_3d.c and the shaders from vi_shader.c, the
 * translation from the neutral draw from d3d_vi_map.c, all host-tested;
 * V9XCZ3D drew the same streams right on M-XP7053's R7E: TL vertices with
 * D3DCOLORs, Z16 and an ARGB8888 texture copied and modulated
 * (docs\decisions\2026-10-28-carrizo-gouraud.md, -depth.md,
 * -texture-modulate.md). This file resolves DirectDraw surfaces, places
 * textures and Z buffers, copies the vertices into the CP's arena and
 * submits (engines\vi_cp.c).
 *
 * Every batch carries the whole state the TL draw sets, about 200 dwords of
 * ring, over the clear state the CP took once at its start. That spares
 * any tracking of what the CP holds; it is the first thing to trim once a
 * benchmark says the ring is the cost.
 *
 * Emission order is the refusal contract: the mapping and the state are
 * built first, and a batch the map refuses writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_vi_map.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"

/* Triangles a batch takes from one call: the arena holds 2 MiB, this is
 * 48 KiB of vertices. */
#define V9X_D3D_VI_CHUNK_TRIANGLES 512ul
#define V9X_D3D_VI_VERTEX_DWORDS   (V9X_VI_TL_VERTEX_BYTES / 4ul)

/* A batch's stream: the TL draw (about 200 dwords), padding to 16. */
#define V9X_D3D_VI_STREAM_DWORDS 512ul

static v9x_u32 v9x_d3d_vi_stream[V9X_D3D_VI_STREAM_DWORDS];

/* The PS slot of each V9X_D3D_VI_PS_* (vi_cp.c). */
static const DWORD v9x_d3d_vi_ps_slot[V9X_D3D_VI_PS_COUNT] = {
    V9X_VI_CP_SLOT_PS_FIRST + V9X_VI_PS_COLOUR,
    V9X_VI_CP_SLOT_PS_FIRST + V9X_VI_PS_TEXTURE,
    V9X_VI_CP_SLOT_PS_FIRST + V9X_VI_PS_MODULATE,
    V9X_VI_CP_SLOT_PS_SPECULAR_FIRST + V9X_VI_PS_COLOUR,
    V9X_VI_CP_SLOT_PS_SPECULAR_FIRST + V9X_VI_PS_TEXTURE,
    V9X_VI_CP_SLOT_PS_SPECULAR_FIRST + V9X_VI_PS_MODULATE
};

/* 16 and 32 bpp targets, Z16 1D-tiled, textures to 2048; pixel centres at
 * whole coordinates, which the PA is told (PIX_CENTER clear), so the core
 * moves nothing. Triangles go to the PA unclipped within +-8192, the
 * scissor keeping the target's pixels, and DDI 6 programs are told of
 * that band: V9XD32's T10 and three 3DMark 2000 runs drew with it on
 * M-XP7053 (boots 31 and 33). It was tried for 3DMark's lines along
 * triangle edges and did not change them; those were the sampler's level
 * of detail outside whole-quad mode (vi_shader.c). */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_vi_limits = {
    16ul,           /* target_bits_per_pixel */
    32752ul,        /* target_pitch_max */
    V9X_D3D_VI_ROW_ALIGN, /* target_pitch_align: linear-aligned rows */
    8192ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    V9X_D3D_VI_TEXTURE_SIDE_MAX, /* texture_size_max */
    8192.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_VI_SURFACE_ALIGN, /* texture_align */
    0ul,            /* clip_in_core: the PA's guard band */
    1ul,            /* depth_pitch_own: DB_DEPTH_SIZE takes the pitch */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    0ul,            /* pixel_centre_half: the PA samples D3D's centres */
    1ul,            /* depth_tiled: Z16 is 1D tiled */
    65504ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits: Z16 only for now */
    0ul,            /* centre_in_engine */
    1ul,            /* texture_stages */
    1ul             /* guard_band_ddi6 */
};

/* The T# data format of a texture format the sampler reads, or UNKNOWN. */
static DWORD v9x_d3d_vi_format_of(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *bytes_out, DWORD *has_alpha)
{
    const V9X_DDPIXELFORMAT *format;

    if (surface == 0 || surface->lpGbl == 0) {
        return V9X_D3D_VI_TEXTURE_UNKNOWN;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return V9X_D3D_VI_TEXTURE_UNKNOWN;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *bytes_out = 2ul;
        *has_alpha = 0ul;
        return V9X_VI_IMG_FORMAT_5_6_5;
    }
    /* ARGB1555 and ARGB4444, blue in the low bits like every format here:
     * the sampler's x is the low field (vi_3d.h). */
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *bytes_out = 2ul;
        *has_alpha = 1ul;
        return V9X_VI_IMG_FORMAT_1_5_5_5;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *bytes_out = 2ul;
        *has_alpha = 1ul;
        return V9X_VI_IMG_FORMAT_4_4_4_4;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *bytes_out = 4ul;
        *has_alpha = 1ul;
        return V9X_VI_IMG_FORMAT_8_8_8_8;
    }
    /* XRGB8888: the same layout, its top byte not alpha, so the map has
     * the T# read W as 1 (vi_3d.h, texture_opaque). */
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful) {
        *bytes_out = 4ul;
        *has_alpha = 0ul;
        return V9X_VI_IMG_FORMAT_8_8_8_8;
    }
    return V9X_D3D_VI_TEXTURE_UNKNOWN;
}

static int v9x_d3d_vi_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *format_out)
{
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;

    if (format_out == 0) {
        return 0;
    }
    *format_out = v9x_d3d_vi_format_of(surface, &bytes, &has_alpha);
    return *format_out != V9X_D3D_VI_TEXTURE_UNKNOWN;
}

/*
 * What d3d_vi_map.c accepts: Z and alpha tests with every function, the
 * blend factors without destination alpha, flat and Gouraud colour and
 * specular, vertex and table fog by the factor the core leaves in the
 * specular alpha, textures in RGB565, ARGB1555, ARGB4444, ARGB8888 and
 * XRGB8888 with point and bilinear filters, wrap, mirror and clamp, colour
 * keys, DECAL, DECALALPHA, COPY, MODULATE and MODULATEALPHA, and mip chains with every
 * Direct3D mip filter.
 */
static void v9x_d3d_vi_describe_caps(V9X_DD_SHARED *shared)
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
                         V9X_D3DPTEXTURECAPS_ALPHA |
                         V9X_D3DPTEXTURECAPS_TRANSPARENCY;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR |
                               V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
                               V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_vi_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_vi_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_vi_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_vi_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555, ARGB4444, ARGB8888, as Cedar's list, and XRGB8888
     * as the Intel engines offer it. */
    for (index = 0ul; index < 5ul; ++index) {
        V9X_DDPIXELFORMAT *pf = &shared->texture_formats[index].ddpfPixelFormat;

        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
        pf->dwSize = sizeof(V9X_DDPIXELFORMAT);
        pf->dwFlags = V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
        pf->dwRGBBitCount = 16ul;
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
    shared->texture_formats[4].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[4].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[4].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[4].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 5ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

static int v9x_d3d_vi_ready(void)
{
    return v9x_vi_cp_ready();
}

/*
 * A mip chain's levels, counted as far as each sits where
 * v9x_vi_3d_mip_layout puts it - create_surface placed it there in one
 * block - with level 0's pitch the layout's, as Cedar's are. The first
 * attached level that is not ends the chain; a chain of one is no chain.
 */
static void v9x_d3d_vi_resolve_chain(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD bytes, V9X_D3D_VI_TEXTURE *texture)
{
    v9x_u32 offsets[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 pitches[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 total;
    const V9X_DD_SURFACE_LCL *level = surface;
    DWORD longest = texture->width > texture->height ? texture->width
                                                     : texture->height;
    DWORD possible = 1ul;
    DWORD found = 1ul;

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    while (longest > 1ul && possible < V9X_VI_MIP_LEVELS_MAX) {
        longest >>= 1;
        ++possible;
    }
    if (!v9x_vi_3d_mip_layout(bytes, texture->width, texture->height,
                              possible, offsets, pitches, &total) ||
        texture->pitch_texels != pitches[0]) {
        return;
    }
    while (found < possible) {
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
        if (next == 0 || next->lpGbl == 0 ||
            v9x_surface_offset(next) != texture->offset + offsets[found] ||
            (DWORD)next->lpGbl->lPitch != pitches[found] * bytes) {
            /* Where the level was and where the layout wanted it, and its
             * pitch beside the layout's (MipGapExpected, MipGapActual,
             * D3dMipChainDelta in V9XTRACE). */
            if (next != 0) {
                ++v9x_hal->d3d_diagnostics.mip_gap_shape;
                v9x_hal->d3d_diagnostics.mip_gap_expected =
                    texture->offset + offsets[found];
                v9x_hal->d3d_diagnostics.mip_gap_actual =
                    next->lpGbl != 0 ? v9x_surface_offset(next) : 0ul;
                v9x_hal->d3d_diagnostics.mip_chain_delta =
                    (next->lpGbl != 0 ?
                     ((DWORD)next->lpGbl->lPitch << 16) : 0ul) |
                    ((pitches[found] * bytes) & 0xfffful);
            }
            break;
        }
        ++found;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = found;
    texture->levels = found;
}

/* The bound texture's level 0 as the map takes it. */
static void v9x_d3d_vi_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                       V9X_D3D_VI_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface =
        (const V9X_DD_SURFACE_LCL *)sampler->object;
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    texture->has_key = 0ul;
    texture->key = 0ul;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->lPitch <= 0l) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return;
    }
    texture->format = v9x_d3d_vi_format_of(surface, &bytes, &has_alpha);
    if (texture->format == V9X_D3D_VI_TEXTURE_UNKNOWN) {
        return;
    }
    texture->has_alpha = has_alpha;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_texels = (DWORD)surface->lpGbl->lPitch / bytes;
    texture->offset = offset;
    texture->levels = 1ul;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1ul;
        texture->key = surface->ddckCKSrcBltLow;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        v9x_d3d_vi_resolve_chain(surface, bytes, texture);
    }
}

static DWORD v9x_d3d_vi_depth_bytes(const V9X_R3D_DRAW *draw)
{
    const V9X_DD_SURFACE_LCL *depth =
        (const V9X_DD_SURFACE_LCL *)draw->depth.object;

    if (depth == 0) {
        return 0ul;
    }
    return v9x_d3d_depth_bytes_per_pixel(depth);
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the other engines count their own. */
static void v9x_d3d_vi_count_refusal(const V9X_R3D_DRAW *draw, v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_VI_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_VI_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

static v9x_u32 v9x_d3d_vi_map(const V9X_R3D_DRAW *draw,
                              struct v9x_vi_tl_draw *out, v9x_u32 *ps,
                              struct v9x_vi_d3d_ps *built)
{
    V9X_D3D_VI_TEXTURE texture;
    DWORD high;
    DWORD heap_low;

    /* Every address in the batch is the heap's or the window's: without
     * them located the batch would point at the wrong memory. */
    if (!v9x_vi_cp_addresses(&high, &heap_low)) {
        return V9X_D3D_VI_REFUSE_ADDRESS;
    }
    texture.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    texture.levels = 1ul;
    if (draw->texture.object != 0) {
        v9x_d3d_vi_resolve_texture(&draw->texture, &texture);
    }
    return v9x_d3d_vi_map_draw(draw, &texture, high, heap_low,
                               v9x_d3d_vi_depth_bytes(draw), out, ps, built);
}

static int v9x_d3d_vi_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_vi_tl_draw out;
    struct v9x_vi_d3d_ps built;
    v9x_u32 ps;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_vi_map(draw, &out, &ps, &built);
    if (reason != V9X_D3D_VI_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_VI_REFUSE_NONE;
}

/* The built PS of the draw in hand (vi_shader.h). */
static v9x_u32 v9x_d3d_vi_built_ps[V9X_VI_D3D_PS_DWORDS_MAX];

/*
 * A run of triangles, in batches of at most V9X_D3D_VI_CHUNK_TRIANGLES:
 * each batch's vertices copied into the CP's arena as they are, then the
 * TL draw's state, the draw and its fence. A built PS goes into the arena
 * ahead of each batch's vertices, as Cedar's lit PS does: its constants
 * are in its code, and the arena is the only space not reused before the
 * GPU has read it.
 */
static int v9x_d3d_vi_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    struct v9x_vi_tl_draw state;
    struct v9x_vi_d3d_ps built;
    struct v9x_vi_pm4 stream;
    volatile DWORD *linear;
    const DWORD *words;
    DWORD gpu_low;
    DWORD chunk;
    DWORD dwords;
    DWORD index;
    DWORD started;
    DWORD ps_dwords = 0ul;
    v9x_u32 ps;
    v9x_u32 reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_vi_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_vi_map(draw, &state, &ps, &built);
    if (reason != V9X_D3D_VI_REFUSE_NONE) {
        v9x_d3d_vi_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS);
    if (ps == V9X_D3D_VI_PS_BUILT) {
        ps_dwords = v9x_vi_shader_d3d_ps_built(&built, v9x_d3d_vi_built_ps);
    } else {
        state.ps_low = v9x_vi_cp_shader_low(v9x_d3d_vi_ps_slot[ps]);
    }
    state.shader_high = state.target_high;
    state.vertex_high = state.target_high;
    state.fence_high = state.target_high;

    while (triangle_count != 0ul) {
        if (ps_dwords != 0ul) {
            if (!v9x_vi_cp_vertex_space(ps_dwords * 4ul, &linear, &gpu_low)) {
                V9X_TIME_END(V9X_TIME_DECODE, started);
                return 0;
            }
            for (index = 0ul; index < ps_dwords; ++index) {
                linear[index] = v9x_d3d_vi_built_ps[index];
            }
            state.ps_low = gpu_low;
        }
        chunk = triangle_count > V9X_D3D_VI_CHUNK_TRIANGLES ?
                V9X_D3D_VI_CHUNK_TRIANGLES : triangle_count;
        dwords = chunk * 3ul * V9X_D3D_VI_VERTEX_DWORDS;
        if (!v9x_vi_cp_vertex_space(dwords * 4ul, &linear, &gpu_low)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        words = (const DWORD *)vertices;
        for (index = 0ul; index < dwords; ++index) {
            linear[index] = words[index];
        }
        state.vertex_low = gpu_low;
        state.vertex_count = chunk * 3ul;
        state.fence_low = v9x_vi_cp_fence_low();
        state.fence_value = v9x_vi_cp_fence_next();

        v9x_vi_pm4_init(&stream, v9x_d3d_vi_stream, V9X_D3D_VI_STREAM_DWORDS);
        if (v9x_vi_3d_tl_draw(&stream, &state) != V9X_STATUS_OK) {
            /* The target the builder would not take, in the trace's
             * DrawsTargetLast, DrawsPitchLast and DrawsExtentLast. */
            v9x_hal->d3d_diagnostics.draws_target_last = state.target_low;
            v9x_hal->d3d_diagnostics.draws_pitch_last = state.pitch_pixels;
            v9x_hal->d3d_diagnostics.draws_extent_last =
                state.pitch_pixels | (state.height << 16);
            v9x_d3d_vi_count_refusal(draw, V9X_D3D_VI_REFUSE_STATE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        v9x_vi_pm4_pad(&stream);
        if (stream.overflow != 0ul) {
            v9x_d3d_vi_count_refusal(draw, V9X_D3D_VI_REFUSE_OVERFLOW);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        V9X_TIME_END(V9X_TIME_DECODE, started);
        if (!v9x_vi_cp_submit(stream.dwords, stream.count,
                              state.fence_value)) {
            return 0;
        }
        v9x_present_note_submission();
        started = V9X_TIME_BEGIN();
        vertices += chunk * 3ul;
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return 1;
}

/* One corner of a depth fill's rectangle: no colour, no texture. */
static void v9x_d3d_vi_fill_vertex(V9X_R3D_VERTEX *vertex, LONG x, LONG y,
                                   float z)
{
    vertex->sx = (float)x;
    vertex->sy = (float)y;
    vertex->sz = z;
    vertex->rhw = 1.0f;
    vertex->color = 0ul;
    vertex->specular = 0ul;
    vertex->tu = 0.0f;
    vertex->tv = 0.0f;
}

/*
 * A rectangle of a Z16 at value, drawn on the CP: eng_vi.c's SDMA fill
 * hands over every depth fill short of the whole surface, which it cannot
 * write tiled (Cedar's d3d_eg.c the same way). Two triangles over it,
 * ZFUNC ALWAYS with writes on; the colour target is the Z buffer's own
 * memory named as RGB565, which only sizes the draw: no channel is
 * written. The core's centres are whole coordinates, so the rectangle's
 * edges cover pixels left to right - 1. The batch waits for queued SDMA
 * work itself (vi_cp.c).
 */
int v9x_d3d_vi_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                          DWORD value, int wait)
{
    struct v9x_vi_tl_draw state;
    struct v9x_vi_pm4 stream;
    V9X_R3D_VERTEX vertices[6];
    volatile DWORD *linear;
    const DWORD *words;
    DWORD high;
    DWORD heap_low;
    DWORD offset;
    DWORD gpu_low;
    DWORD rows;
    DWORD index;
    float z;

    (void)wait;
    if (surface == 0 || rect == 0 || surface->lpGbl == 0 ||
        surface->lpGbl->lPitch <= 0l || !v9x_d3d_vi_ready() ||
        v9x_d3d_depth_bytes_per_pixel(surface) != 2ul ||
        rect[0] < 0l || rect[1] < 0l || rect[2] <= rect[0] ||
        rect[3] <= rect[1] ||
        rect[2] > (LONG)surface->lpGbl->wWidth ||
        rect[3] > (LONG)surface->lpGbl->wHeight ||
        !v9x_vi_cp_addresses(&high, &heap_low)) {
        return V9X_BLT_DECLINED;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || heap_low + offset < heap_low) {
        return V9X_BLT_DECLINED;
    }
    rows = ((DWORD)surface->lpGbl->wHeight + V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul) &
           ~(V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul);

    for (index = 0ul; index < sizeof(state); ++index) {
        ((unsigned char *)&state)[index] = 0u;
    }
    state.target_low = heap_low + offset;
    state.target_high = high;
    state.pitch_pixels = (DWORD)surface->lpGbl->lPitch / 2ul;
    state.height = rows;
    state.format = V9X_VI_CB_FORMAT_5_6_5;
    state.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    state.target_mask_none = 1ul;
    state.d3d_pixel_centres = 1ul;
    state.depth_low = state.target_low;
    state.depth_high = high;
    state.depth_func = V9X_VI_ZFUNC_ALWAYS;
    state.depth_write = 1ul;
    state.depth_pitch = state.pitch_pixels;
    state.depth_height = rows;
    state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS);
    state.ps_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_PS_FIRST +
                                        V9X_VI_PS_COLOUR);
    state.shader_high = high;
    state.vertex_high = high;
    state.fence_high = high;

    /* On the 16-bit full scale. */
    z = (float)(long)(value & 0xfffful) / 65535.0f;
    v9x_d3d_vi_fill_vertex(&vertices[0], rect[0], rect[1], z);
    v9x_d3d_vi_fill_vertex(&vertices[1], rect[2], rect[1], z);
    v9x_d3d_vi_fill_vertex(&vertices[2], rect[0], rect[3], z);
    v9x_d3d_vi_fill_vertex(&vertices[3], rect[2], rect[1], z);
    v9x_d3d_vi_fill_vertex(&vertices[4], rect[2], rect[3], z);
    v9x_d3d_vi_fill_vertex(&vertices[5], rect[0], rect[3], z);
    if (!v9x_vi_cp_vertex_space(sizeof(vertices), &linear, &gpu_low)) {
        return V9X_BLT_DECLINED;
    }
    words = (const DWORD *)vertices;
    for (index = 0ul; index < sizeof(vertices) / 4ul; ++index) {
        linear[index] = words[index];
    }
    state.vertex_low = gpu_low;
    state.vertex_count = 6ul;
    state.fence_low = v9x_vi_cp_fence_low();
    state.fence_value = v9x_vi_cp_fence_next();

    v9x_vi_pm4_init(&stream, v9x_d3d_vi_stream, V9X_D3D_VI_STREAM_DWORDS);
    if (v9x_vi_3d_tl_draw(&stream, &state) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    v9x_vi_pm4_pad(&stream);
    if (stream.overflow != 0ul ||
        !v9x_vi_cp_submit(stream.dwords, stream.count, state.fence_value)) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

/*
 * Placement, as Cedar's (d3d_eg.c), on GFX8's rules (d3d_vi_map.h). An
 * offscreen render target alone gets its rows whole 64 bytes and whole
 * 8-pixel tiles, 256-aligned. A Z16 buffer gets its rows whole 64 bytes
 * (whole 8-pixel tiles too) and padded to 8, the DB's 1D tile,
 * 256-aligned. A
 * video-memory texture of a format this engine samples gets its rows whole
 * 64 bytes, which a linear-aligned T# needs. A mip chain goes in one
 * block, each level where the sampler finds it from the base
 * (v9x_vi_3d_mip_layout); a chain that layout does not take is left to
 * DirectDraw, and only its level 0 is sampled.
 */
static DWORD v9x_d3d_vi_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 offsets[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 rows[1];
    v9x_u32 total;
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD width;
    DWORD height;
    DWORD level;
    DWORD base;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0) {
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
    if (width == 0ul || height == 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    if ((surface->ddsCaps & V9X_DDSCAPS_3DDEVICE) != 0ul &&
        (surface->ddsCaps & (V9X_DDSCAPS_PRIMARYSURFACE |
                             V9X_DDSCAPS_FLIP)) == 0ul) {
        if (data->dwSCnt != 1ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        bytes = (surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul ?
                surface->lpGbl->ddpfSurface.dwRGBBitCount / 8ul :
                v9x_hal->info.vmiData.ddpfDisplay.dwRGBBitCount / 8ul;
        if (bytes != 2ul && bytes != 4ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = (((width + 7ul) & ~7ul) * bytes +
                      V9X_D3D_VI_ROW_ALIGN - 1ul) & ~(V9X_D3D_VI_ROW_ALIGN - 1ul);
        rows[0] = height;
    } else if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (data->dwSCnt != 1ul ||
            v9x_d3d_depth_bytes_per_pixel(surface) != 2ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        /* Rows of whole 64 bytes, not just the DB's 8-pixel tiles: the
         * core binds a depth buffer only at the engine's target row
         * alignment (v9x_d3d_set_target), and an 800-byte Z for a 400-wide
         * OpenGL window was refused, every batch with it (V9XGLP, boot 37). */
        pitches[0] = (width * 2ul + V9X_D3D_VI_ROW_ALIGN - 1ul) &
                     ~(V9X_D3D_VI_ROW_ALIGN - 1ul);
        rows[0] = (height + V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul) &
                  ~(V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul);
    } else {
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            width > v9x_d3d_vi_limits.texture_size_max ||
            height > v9x_d3d_vi_limits.texture_size_max ||
            v9x_d3d_vi_format_of(surface, &bytes, &has_alpha) ==
                V9X_D3D_VI_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if (data->dwSCnt > 1ul) {
            /* Each level half the last, as v9x_vi_3d_mip_layout lays
             * them out, and in video memory with the top. */
            for (level = 0ul; level < data->dwSCnt; ++level) {
                const V9X_DD_SURFACE_LCL *member = list[level];

                if (member == 0 || member->lpGbl == 0 ||
                    (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
                    (DWORD)member->lpGbl->wWidth != width ||
                    (DWORD)member->lpGbl->wHeight != height) {
                    return V9X_DDHAL_DRIVER_NOTHANDLED;
                }
                width = width > 1ul ? width >> 1 : 1ul;
                height = height > 1ul ? height >> 1 : 1ul;
            }
            if (!v9x_vi_3d_mip_layout(bytes, (DWORD)surface->lpGbl->wWidth,
                                      (DWORD)surface->lpGbl->wHeight,
                                      data->dwSCnt, offsets, pitches,
                                      &total)) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            for (level = 0ul; level < data->dwSCnt; ++level) {
                pitches[level] *= bytes;
            }
            if (v9x_d3d_place_chain(data, V9X_D3D_VI_SURFACE_ALIGN, total,
                                    offsets, pitches, &base) != 0ul) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            ++v9x_hal->d3d_diagnostics.texture_placed;
            v9x_hal->d3d_diagnostics.texture_placed_bytes += total;
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        pitches[0] = (width * bytes + V9X_D3D_VI_ROW_ALIGN - 1ul) &
                     ~(V9X_D3D_VI_ROW_ALIGN - 1ul);
        rows[0] = height;
    }
    if (v9x_d3d_place_each(data, V9X_D3D_VI_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * A CPU Lock of a Z16 buffer. The DB writes it 1D-tiled and writes no
 * linear depth (V9XCZ3D /zlinear, boot 35), while DirectDraw hands the CPU
 * a linear pointer: V9XDDP /mixed read 0xFFFF where the DB had written 0.5,
 * and the DB ignored the CPU's 0x4000. So a Z16 this engine placed is
 * tiled at rest and linear only while locked: detiled at Lock and retiled
 * at Unlock, eight rows - one row of tiles, the same bytes either way - at
 * a time through a band buffer (vi_3d.h). The band is the locking
 * process's own memory for the call (LocalAlloc): a static one would be
 * 32 KB in this DLL's file, which put the S3 floppy over. Placed means
 * rows padded to whole tiles, so the last band is the surface's own;
 * recognised by v9x_d3d_place_release's signature. A nested Lock converts
 * nothing, and a surface past the table, the band's pitch or the
 * allocation is left as it is.
 */
#define V9X_D3D_VI_LOCKED_MAX     4ul
#define V9X_D3D_VI_BAND_PITCH_MAX 2048ul

static DWORD v9x_d3d_vi_locked[V9X_D3D_VI_LOCKED_MAX];

static int v9x_d3d_vi_tiled_depth(const V9X_DD_SURFACE_LCL *surface)
{
    const V9X_DD_SURFACE_GBL *global;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        v9x_d3d_depth_bytes_per_pixel(surface) != 2ul) {
        return 0;
    }
    global = surface->lpGbl;
    return global->fpVidMem != 0ul && global->dwReserved1 != 0ul &&
           global->dwBlockSizeX == 0ul &&
           global->fpVidMem >= global->dwReserved1 &&
           global->fpVidMem - global->dwReserved1 < V9X_D3D_VI_SURFACE_ALIGN &&
           global->lPitch > 0l &&
           ((DWORD)global->lPitch % V9X_D3D_VI_DEPTH_PITCH_UNIT) == 0ul &&
           (DWORD)global->lPitch / 2ul <= V9X_D3D_VI_BAND_PITCH_MAX;
}

/* The table slot holding the surface's memory, or LOCKED_MAX. */
static DWORD v9x_d3d_vi_locked_slot(DWORD memory)
{
    DWORD slot;

    for (slot = 0ul; slot < V9X_D3D_VI_LOCKED_MAX; ++slot) {
        if (v9x_d3d_vi_locked[slot] == memory) {
            return slot;
        }
    }
    return V9X_D3D_VI_LOCKED_MAX;
}

/* Nonzero when converted; zero, touching nothing, when the band could not
 * be allocated. */
static int v9x_d3d_vi_convert_depth(const V9X_DD_SURFACE_LCL *surface,
                                    int to_linear)
{
    const V9X_DD_SURFACE_GBL *global = surface->lpGbl;
    DWORD pitch_pixels = (DWORD)global->lPitch / 2ul;
    DWORD band_bytes = (DWORD)global->lPitch * V9X_VI_Z16_TILE_ROWS;
    DWORD bands = ((DWORD)global->wHeight + V9X_VI_Z16_TILE_ROWS - 1ul) /
                  V9X_VI_Z16_TILE_ROWS;
    DWORD *band_words;
    DWORD band;
    DWORD index;

    band_words = (DWORD *)LocalAlloc(LMEM_FIXED, band_bytes);
    if (band_words == 0) {
        return 0;
    }
    for (band = 0ul; band < bands; ++band) {
        volatile DWORD *memory =
            (volatile DWORD *)(global->fpVidMem + band * band_bytes);

        for (index = 0ul; index < band_bytes / 4ul; ++index) {
            band_words[index] = memory[index];
        }
        if (to_linear) {
            v9x_vi_z16_detile_band((const v9x_u16 *)band_words,
                                   (v9x_u16 *)memory, pitch_pixels);
        } else {
            v9x_vi_z16_retile_band((const v9x_u16 *)band_words,
                                   (v9x_u16 *)memory, pitch_pixels);
        }
    }
    LocalFree(band_words);
    return 1;
}

static void v9x_d3d_vi_lock_surface(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD slot;

    if (!v9x_d3d_vi_tiled_depth(surface) ||
        v9x_d3d_vi_locked_slot(surface->lpGbl->fpVidMem) !=
            V9X_D3D_VI_LOCKED_MAX) {
        return;
    }
    slot = v9x_d3d_vi_locked_slot(0ul);
    if (slot == V9X_D3D_VI_LOCKED_MAX) {
        return;
    }
    if (v9x_d3d_vi_convert_depth(surface, 1)) {
        v9x_d3d_vi_locked[slot] = surface->lpGbl->fpVidMem;
    }
}

static void v9x_d3d_vi_unlock_surface(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD slot;

    if (surface == 0 || surface->lpGbl == 0 ||
        surface->lpGbl->fpVidMem == 0ul) {
        return;
    }
    slot = v9x_d3d_vi_locked_slot(surface->lpGbl->fpVidMem);
    if (slot == V9X_D3D_VI_LOCKED_MAX) {
        return;
    }
    /* A failed allocation here leaves the buffer linear: the DB then reads
     * it misaddressed, as every Lock did before this. */
    (void)v9x_d3d_vi_convert_depth(surface, 0);
    v9x_d3d_vi_locked[slot] = 0ul;
}

static void v9x_d3d_vi_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    const V9X_DD_SURFACE_LCL *surface =
        data != 0 ? (const V9X_DD_SURFACE_LCL *)data->lpDDSurface : 0;
    DWORD slot;

    /* Destroyed while locked: its memory is no longer a lock to undo. */
    if (surface != 0 && surface->lpGbl != 0 &&
        surface->lpGbl->fpVidMem != 0ul) {
        slot = v9x_d3d_vi_locked_slot(surface->lpGbl->fpVidMem);
        if (slot != V9X_D3D_VI_LOCKED_MAX) {
            v9x_d3d_vi_locked[slot] = 0ul;
        }
    }
    (void)v9x_d3d_place_release(data, V9X_D3D_VI_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_vi = {
    &v9x_d3d_vi_limits,
    v9x_d3d_vi_texture_format,
    v9x_d3d_vi_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_vi_ready,
    v9x_d3d_vi_create_surface,
    v9x_d3d_vi_destroy_surface,
    v9x_d3d_vi_draw,
    v9x_d3d_vi_accepts,
    0,                                  /* draw_indexed */
    0,                                  /* draw_tnl */
    0,                                  /* tnl_available */
    v9x_d3d_vi_lock_surface,
    v9x_d3d_vi_unlock_surface
};
