/*
 * ATI R6xx (Radeon HD 2600 XT, RV630) Direct3D engine, through the command
 * processor.
 *
 * d3d_eg.c's shape on the R6xx pieces: the streams from
 * src\chipsets\ati\r600\r6_3d.c in Direct3D's channel order (d3d_order),
 * the shaders from r6_shader.c, the translation from the neutral draw from
 * d3d_r6_map.c, all host-tested; the CP from engines\r6_cp.c. V9XR63D drew
 * the builder's Gouraud, textured-modulate and Z16 streams exactly on
 * M-NZHWBR (docs\decisions\2026-11-13-rv630-gouraud-texture-depth.md), in
 * xf86's ARGB order on an ARGB8888 target; Direct3D's order, the 565
 * target, the 16-bit textures and the TL-vertex shaders are not yet run on
 * the card.
 *
 * Every batch carries the whole state: the default state, then its own,
 * as Evergreen's does.
 *
 * Mip chains are placed and sampled in Evergreen's layout
 * (v9x_eg_3d_mip_layout). A draw with specular, fog or a source colour key
 * takes the lit shaders, its PS built per draw into the arena as
 * Evergreen's. Not yet: OpenGL and Glide (no r3d engine id).
 */
#include "d3d_internal.h"
#include "d3d_r6_map.h"
#include "velocity9x/r6_3d.h"
#include "velocity9x/r6_shader.h"

/* Triangles a batch takes from one call: 48 KiB of vertices in the 2 MiB
 * arena, as Evergreen's. */
#define V9X_D3D_R6_CHUNK_TRIANGLES 512ul
#define V9X_D3D_R6_VERTEX_DWORDS   (V9X_R6_D3D_STRIDE / 4ul)
/* Levels of a 2048-texel chain, each placed; only level 0 is sampled. */
#define V9X_D3D_R6_LEVELS_MAX      12ul

/* A batch's stream: the default state, the batch's state, the draw, the
 * flush and padding, with room to spare; overflow is checked. */
#define V9X_D3D_R6_STREAM_DWORDS   1024ul

static v9x_u32 v9x_d3d_r6_stream[V9X_D3D_R6_STREAM_DWORDS];

/* 16 and 32 bpp targets, Z16 1D-tiled, textures to 2048; the core clips
 * every triangle to the target; pixel centres at whole coordinates, which
 * the PA is told (PIX_CENTER clear). Evergreen's limits. */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_r6_limits = {
    16ul,           /* target_bits_per_pixel */
    32752ul,        /* target_pitch_max */
    32ul,           /* target_pitch_align: 8 pixels at 32 bpp */
    8192ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    8192.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_EG_SURFACE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own: DB_DEPTH_SIZE takes the pitch */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    0ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: Z16 is 1D tiled */
    65504ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits: Z16 only */
    0ul,            /* centre_in_engine */
    1ul             /* texture_stages */
};

/* RGB565, ARGB1555, ARGB4444, ARGB8888 and XRGB8888: the V9X_R6_COLOR_*
 * the sampler takes, or UNKNOWN, with the bytes a texel and whether its
 * fourth field is not alpha. */
static DWORD v9x_d3d_r6_format_opaque(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *bytes_out, DWORD *opaque_out)
{
    const V9X_DDPIXELFORMAT *format;

    *opaque_out = 0ul;
    if (surface == 0 || surface->lpGbl == 0) {
        return V9X_D3D_R6_TEXTURE_UNKNOWN;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return V9X_D3D_R6_TEXTURE_UNKNOWN;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *bytes_out = 2ul;
        return V9X_R6_COLOR_5_6_5;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *bytes_out = 2ul;
        return V9X_R6_COLOR_1_5_5_5;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *bytes_out = 2ul;
        return V9X_R6_COLOR_4_4_4_4;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *bytes_out = 4ul;
        return V9X_R6_COLOR_8_8_8_8;
    }
    /* XRGB8888: ARGB8888's layout, its top byte read as 1 (V9XD32 T22X
     * drew black while it was not offered, M-NZHWBR boot 7). */
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful) {
        *bytes_out = 4ul;
        *opaque_out = 1ul;
        return V9X_R6_COLOR_8_8_8_8;
    }
    return V9X_D3D_R6_TEXTURE_UNKNOWN;
}

static DWORD v9x_d3d_r6_format_of(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *bytes_out)
{
    DWORD opaque;

    return v9x_d3d_r6_format_opaque(surface, bytes_out, &opaque);
}

static int v9x_d3d_r6_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *format_out)
{
    DWORD bytes = 0ul;

    if (format_out == 0) {
        return 0;
    }
    *format_out = v9x_d3d_r6_format_of(surface, &bytes);
    return *format_out != V9X_D3D_R6_TEXTURE_UNKNOWN;
}

/*
 * What d3d_r6_map.c accepts: Evergreen's caps - specular, vertex and
 * table fog by the factor the core leaves in the specular alpha, and the
 * source colour key (TRANSPARENCY).
 */
static void v9x_d3d_r6_describe_caps(V9X_DD_SHARED *shared)
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
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_r6_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_r6_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_r6_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_r6_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555, ARGB4444, ARGB8888, then XRGB8888 below. */
    for (index = 0ul; index < 4ul; ++index) {
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
    /* XRGB8888, the fifth and last entry the table holds. */
    shared->texture_formats[4] = shared->texture_formats[3];
    shared->texture_formats[4].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 5ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

static int v9x_d3d_r6_ready(void)
{
    return v9x_r6_cp_ready();
}

/*
 * A mip chain's levels, counted as far as each sits where Evergreen's
 * layout puts it (v9x_eg_3d_mip_layout, libdrm's R6xx linear-aligned
 * layout too; its V9X_EG_COLOR_* numbers are R6xx's) - create_surface
 * placed it there in one block - with level 0's pitch the layout's. The
 * first attached level that is not ends the chain; a chain of one is no
 * chain. d3d_eg.c's resolve, counted in the same diagnostics.
 */
static void v9x_d3d_r6_resolve_chain(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD bytes, V9X_D3D_R6_TEXTURE *texture)
{
    v9x_u32 offsets[V9X_D3D_R6_LEVELS_MAX];
    v9x_u32 pitches[V9X_D3D_R6_LEVELS_MAX];
    v9x_u32 total;
    const V9X_DD_SURFACE_LCL *level = surface;
    DWORD longest = texture->width > texture->height ? texture->width
                                                     : texture->height;
    DWORD possible = 1ul;
    DWORD found = 1ul;

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    while (longest > 1ul && possible < V9X_D3D_R6_LEVELS_MAX) {
        longest >>= 1;
        ++possible;
    }
    if (!v9x_eg_3d_mip_layout(texture->format, texture->width,
                              texture->height, possible, offsets, pitches,
                              &total) ||
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
            if (next != 0) {
                /* The gap, for V9XTRACE: where the level was expected and
                 * where it is, the top's size, and the level's pitch with
                 * its index in the top byte. */
                ++v9x_hal->d3d_diagnostics.mip_gap_shape;
                v9x_hal->d3d_diagnostics.mip_gap_expected =
                    texture->offset + offsets[found];
                v9x_hal->d3d_diagnostics.mip_gap_actual =
                    next->lpGbl != 0 ? v9x_surface_offset(next) : 0ul;
                v9x_hal->d3d_diagnostics.mip_tree_last_shape =
                    texture->width | (texture->height << 16);
                v9x_hal->d3d_diagnostics.mip_tree_last_offset =
                    (next->lpGbl != 0 ? (DWORD)next->lpGbl->lPitch : 0ul) |
                    (found << 24);
            }
            break;
        }
        ++found;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = found;
    if (found > 1ul) {
        texture->levels = found;
        texture->mip_offset = texture->offset + offsets[1];
    }
}

/* The bound texture's level 0 as the map takes it. */
static void v9x_d3d_r6_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                       V9X_D3D_R6_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface =
        (const V9X_DD_SURFACE_LCL *)sampler->object;
    DWORD bytes = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_R6_TEXTURE_UNKNOWN;
    texture->has_key = 0ul;
    texture->opaque = 0ul;
    texture->levels = 1ul;
    texture->mip_offset = 0ul;
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
    texture->format = v9x_d3d_r6_format_opaque(surface, &bytes,
                                               &texture->opaque);
    if (texture->format == V9X_D3D_R6_TEXTURE_UNKNOWN) {
        return;
    }
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_texels = (DWORD)surface->lpGbl->lPitch / bytes;
    texture->offset = offset;
    texture->mip_offset = offset;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1ul;
        texture->key = surface->ddckCKSrcBltLow;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        v9x_d3d_r6_resolve_chain(surface, bytes, texture);
    }
}

static DWORD v9x_d3d_r6_depth_bytes(const V9X_R3D_DRAW *draw)
{
    const V9X_DD_SURFACE_LCL *depth =
        (const V9X_DD_SURFACE_LCL *)draw->depth.object;

    if (depth == 0) {
        return 0ul;
    }
    return v9x_d3d_depth_bytes_per_pixel(depth);
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as Evergreen counts its own. */
static void v9x_d3d_r6_count_refusal(const V9X_R3D_DRAW *draw, v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_EG_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_EG_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

static v9x_u32 v9x_d3d_r6_map(const V9X_R3D_DRAW *draw,
                              struct v9x_r6_3d_state *state,
                              v9x_u32 *vs_slot, v9x_u32 *ps_slot,
                              DWORD *pipe_config, struct v9x_r6_d3d_ps *lit)
{
    V9X_D3D_R6_TEXTURE texture;
    DWORD heap_gpu;

    /* Every address in the batch is the heap's or the window's: without
     * them located the batch would point at the wrong memory. */
    if (!v9x_r6_cp_addresses(&heap_gpu, pipe_config)) {
        return V9X_D3D_EG_REFUSE_ADDRESS;
    }
    texture.format = V9X_D3D_R6_TEXTURE_UNKNOWN;
    if (draw->texture.object != 0) {
        v9x_d3d_r6_resolve_texture(&draw->texture, &texture);
    }
    return v9x_d3d_r6_map_draw(draw, &texture, heap_gpu,
                               v9x_d3d_r6_depth_bytes(draw), state, vs_slot,
                               ps_slot, lit);
}

static int v9x_d3d_r6_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_r6_3d_state state;
    v9x_u32 vs_slot;
    v9x_u32 ps_slot;
    v9x_u32 reason;
    DWORD pipe_config;
    struct v9x_r6_d3d_ps lit;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_r6_map(draw, &state, &vs_slot, &ps_slot, &pipe_config,
                            &lit);
    if (reason != V9X_D3D_EG_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_EG_REFUSE_NONE;
}

/* The lit PS of the draw in hand (r6_shader.h, v9x_r6_shader_d3d_ps_lit). */
static v9x_u32 v9x_d3d_r6_lit_ps[V9X_R6_LIT_PS_DWORDS_MAX];

/*
 * A run of triangles, in batches of at most V9X_D3D_R6_CHUNK_TRIANGLES:
 * each batch's vertices copied into the CP's arena as they are, then the
 * default state, the batch's state, the draw and the cache flush. A lit
 * draw's PS goes into the arena ahead of each batch's vertices, as
 * Evergreen's: it holds the fog colour and the key, and the arena is the
 * only space not reused before the GPU has read it.
 */
static int v9x_d3d_r6_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    struct v9x_r6_3d_state state;
    struct v9x_r6_pm4 stream;
    volatile DWORD *linear;
    const DWORD *words;
    DWORD pipe_config;
    DWORD gpu;
    DWORD chunk;
    DWORD dwords;
    DWORD index;
    DWORD started;
    v9x_u32 vs_slot;
    v9x_u32 ps_slot;
    v9x_u32 reason;
    DWORD ps_dwords = 0ul;
    struct v9x_r6_d3d_ps lit;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_r6_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_r6_map(draw, &state, &vs_slot, &ps_slot, &pipe_config,
                            &lit);
    if (reason != V9X_D3D_EG_REFUSE_NONE) {
        v9x_d3d_r6_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    state.vs_gpu = v9x_r6_cp_shader_gpu(vs_slot);
    if (ps_slot == V9X_D3D_R6_PS_LIT) {
        ps_dwords = v9x_r6_shader_d3d_ps_lit(&lit, v9x_d3d_r6_lit_ps);
    } else {
        state.ps_gpu = v9x_r6_cp_shader_gpu(ps_slot);
    }

    while (triangle_count != 0ul) {
        if (ps_dwords != 0ul) {
            if (!v9x_r6_cp_vertex_space(ps_dwords * 4ul, &linear, &gpu)) {
                V9X_TIME_END(V9X_TIME_DECODE, started);
                return 0;
            }
            for (index = 0ul; index < ps_dwords; ++index) {
                linear[index] = v9x_d3d_r6_lit_ps[index];
            }
            state.ps_gpu = gpu;
        }
        chunk = triangle_count > V9X_D3D_R6_CHUNK_TRIANGLES ?
                V9X_D3D_R6_CHUNK_TRIANGLES : triangle_count;
        dwords = chunk * 3ul * V9X_D3D_R6_VERTEX_DWORDS;
        if (!v9x_r6_cp_vertex_space(dwords * 4ul, &linear, &gpu)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        words = (const DWORD *)vertices;
        for (index = 0ul; index < dwords; ++index) {
            linear[index] = words[index];
        }
        state.vertex_gpu = gpu;
        state.vertex_bytes = dwords * 4ul;

        /* The fetch shader is unused (the VS fetches for itself); the
         * default state points it at the VS's slot, as xf86's relocation
         * points it at the solid VS. */
        v9x_r6_pm4_begin(&stream, v9x_d3d_r6_stream,
                         V9X_D3D_R6_STREAM_DWORDS);
        v9x_r6_3d_default_state(&stream, pipe_config, state.vs_gpu);
        if (v9x_r6_3d_build_state(&state, &stream) != V9X_STATUS_OK) {
            v9x_hal->d3d_diagnostics.draws_target_last = state.target_gpu;
            v9x_hal->d3d_diagnostics.draws_pitch_last = state.target_pitch;
            v9x_hal->d3d_diagnostics.draws_extent_last =
                state.scissor_right | (state.target_height << 16);
            v9x_d3d_r6_count_refusal(draw, V9X_D3D_EG_REFUSE_STATE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        v9x_r6_3d_draw(&stream, chunk * 3ul);
        v9x_r6_3d_flush(&stream);
        v9x_r6_pm4_pad(&stream);
        if (stream.overflow != 0ul) {
            v9x_d3d_r6_count_refusal(draw, V9X_D3D_EG_REFUSE_OVERFLOW);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        V9X_TIME_END(V9X_TIME_DECODE, started);
        if (!v9x_r6_cp_submit(stream.dwords, stream.count)) {
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

/* A rectangle of a Z16 at value, drawn on the CP: Evergreen's depth-fill
 * draw (d3d_eg_map.h, v9x_d3d_eg_depth_fill_draw), which eng_r6.c hands
 * every depth fill short of the whole surface. */
int v9x_d3d_r6_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                          DWORD value, int wait)
{
    V9X_R3D_DRAW draw;
    V9X_R3D_VERTEX vertices[6];
    DWORD offset;

    (void)wait;
    if (surface == 0 || rect == 0 || surface->lpGbl == 0 ||
        surface->lpGbl->lPitch <= 0l || !v9x_d3d_r6_ready() ||
        v9x_d3d_depth_bytes_per_pixel(surface) != 2ul ||
        rect[0] < 0l || rect[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful ||
        !v9x_d3d_eg_depth_fill_draw(offset, (DWORD)surface->lpGbl->lPitch,
                                    (DWORD)surface->lpGbl->wWidth,
                                    (DWORD)surface->lpGbl->wHeight,
                                    (DWORD)rect[0], (DWORD)rect[1],
                                    (DWORD)rect[2], (DWORD)rect[3], value,
                                    surface, &draw, vertices)) {
        return V9X_BLT_DECLINED;
    }
    return v9x_d3d_r6_draw(&draw, vertices, 2ul) ? V9X_BLT_DONE
                                                  : V9X_BLT_DECLINED;
}

/*
 * Placement, Evergreen's with R6xx's texture pitch. An offscreen render
 * target gets its pitch rounded to 8 pixels and 32 bytes, 256-aligned. A
 * Z16 buffer gets its pitch rounded to 16 pixels (the 1D tile's pitch
 * alignment, r6_3d.h) and its rows to 8, 256-aligned. A video-memory
 * texture of a format this engine samples gets each level in a block of
 * its own, 256-aligned, at the linear-aligned pitch
 * (v9x_r6_3d_texture_pitch: 64 texels at 32 bits, 128 at 16).
 */
static DWORD v9x_d3d_r6_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_D3D_R6_LEVELS_MAX];
    v9x_u32 rows[V9X_D3D_R6_LEVELS_MAX];
    v9x_u32 offsets[V9X_D3D_R6_LEVELS_MAX];
    v9x_u32 total = 0ul;
    DWORD base = 0ul;
    DWORD bytes = 0ul;
    DWORD format;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_R6_LEVELS_MAX || data->lplpSList == 0) {
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
        pitches[0] = ((width + 7ul) & ~7ul) * bytes;
        pitches[0] = (pitches[0] + v9x_d3d_r6_limits.target_pitch_align -
                      1ul) & ~(v9x_d3d_r6_limits.target_pitch_align - 1ul);
        rows[0] = height;
    } else if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (data->dwSCnt != 1ul ||
            v9x_d3d_depth_bytes_per_pixel(surface) != 2ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = (width * 2ul + V9X_D3D_EG_DEPTH_ROW_ALIGN - 1ul) &
                     ~(V9X_D3D_EG_DEPTH_ROW_ALIGN - 1ul);
        rows[0] = (height + V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul) &
                  ~(V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul);
    } else {
        format = v9x_d3d_r6_format_of(surface, &bytes);
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            width > v9x_d3d_r6_limits.texture_size_max ||
            height > v9x_d3d_r6_limits.texture_size_max ||
            format == V9X_D3D_R6_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        for (level = 0ul; level < data->dwSCnt; ++level) {
            const V9X_DD_SURFACE_LCL *member = list[level];

            if (level != 0ul) {
                width = width > 1ul ? width >> 1 : 1ul;
                height = height > 1ul ? height >> 1 : 1ul;
            }
            if (member == 0 || member->lpGbl == 0 ||
                (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
                (DWORD)member->lpGbl->wWidth != width ||
                (DWORD)member->lpGbl->wHeight != height) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            pitches[level] = v9x_r6_3d_texture_pitch(format, width) * bytes;
            rows[level] = height;
        }
        /* A chain in one block, where the sampler finds its levels from
         * the base and MIP_ADDRESS (v9x_eg_3d_mip_layout, as d3d_eg.c). */
        if (data->dwSCnt > 1ul) {
            if (!v9x_eg_3d_mip_layout(format,
                                      (DWORD)surface->lpGbl->wWidth,
                                      (DWORD)surface->lpGbl->wHeight,
                                      data->dwSCnt, offsets, pitches,
                                      &total)) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            for (level = 0ul; level < data->dwSCnt; ++level) {
                pitches[level] *= bytes;
            }
            level = v9x_d3d_place_chain(data, V9X_D3D_EG_SURFACE_ALIGN, total,
                                        offsets, pitches, &base);
            if (level != 0ul) {
                /* DirectDraw then places the levels itself, apart: the
                 * chain is drawn from level 0 alone. Counted with the
                 * reason (d3d_internal.h, V9X_D3D_PLACE_*). */
                ++v9x_hal->d3d_diagnostics.mip_tree_declined;
                v9x_hal->d3d_diagnostics.mip_tree_declined_last = level;
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            ++v9x_hal->d3d_diagnostics.texture_placed;
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    if (v9x_d3d_place_each(data, V9X_D3D_EG_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_r6_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_EG_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_r6 = {
    &v9x_d3d_r6_limits,
    v9x_d3d_r6_texture_format,
    v9x_d3d_r6_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_r6_ready,
    v9x_d3d_r6_create_surface,
    v9x_d3d_r6_destroy_surface,
    v9x_d3d_r6_draw,
    v9x_d3d_r6_accepts
};
