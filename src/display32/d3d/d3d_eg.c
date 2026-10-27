/*
 * AMD Evergreen (Radeon HD 5450, Cedar) Direct3D engine, through the
 * command processor.
 *
 * Item 5 of docs\plans\ati-evergreen-cedar-3d.md. The streams come from
 * src\chipsets\ati\evergreen\eg_3d.c and the shaders from eg_shader.c,
 * the translation from the neutral draw from d3d_eg_map.c, all
 * host-tested; V9XEG3D drew the same streams right on M-BXMFA8's card:
 * TL vertices with D3DCOLORs, a normalised texture, perspective, Z16 and a
 * 565 target (docs\decisions\2026-10-27-cedar-d3d-streams.md). This file
 * resolves DirectDraw surfaces, places textures and Z buffers, copies the
 * vertices into the CP's arena and submits (engines\eg_cp.c).
 *
 * Every batch carries the whole state: set_default_state, then its own.
 * That costs about 400 dwords of ring a batch and spares any tracking of
 * what the CP holds; it is the first thing to trim once a benchmark says
 * the ring is the cost.
 *
 * Emission order is the refusal contract: the mapping and the state are
 * built first, and a batch the map refuses writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_eg_map.h"
#include "velocity9x/eg_3d.h"
#include "velocity9x/eg_shader.h"

/* Triangles a batch takes from one call: the arena holds 2 MiB, this is
 * 48 KiB of vertices. */
#define V9X_D3D_EG_CHUNK_TRIANGLES 512ul
#define V9X_D3D_EG_VERTEX_DWORDS   (V9X_EG_D3D_STRIDE / 4ul)
/* Levels of a 2048-texel chain, each placed; only level 0 is sampled. */
#define V9X_D3D_EG_LEVELS_MAX      12ul

/* A batch's stream: set_default_state (262), the state (under 160), the
 * draw (10), the flush (5), padding to 16. */
#define V9X_D3D_EG_STREAM_DWORDS 512ul

static v9x_u32 v9x_d3d_eg_stream[V9X_D3D_EG_STREAM_DWORDS];

/* 16 and 32 bpp targets, Z16 1D-tiled, textures to 2048; the core clips
 * every triangle to the target (the guard band is not measured); pixel
 * centres at whole coordinates, which the PA is told (PIX_CENTER_HALF
 * clear), so the core moves nothing. */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_eg_limits = {
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
    0ul,            /* pixel_centre_half: the PA samples D3D's centres */
    1ul,            /* depth_tiled: Z16 is 1D tiled */
    65504ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits: Z16 only for now */
    0ul,            /* centre_in_engine */
    1ul             /* texture_stages */
};

/* RGB565 at 16 bits and ARGB8888 at 32: the colour-buffer format number
 * the sampler takes, or UNKNOWN. */
static DWORD v9x_d3d_eg_format_of(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *bytes_out, DWORD *has_alpha)
{
    const V9X_DDPIXELFORMAT *format;

    if (surface == 0 || surface->lpGbl == 0) {
        return V9X_D3D_EG_TEXTURE_UNKNOWN;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return V9X_D3D_EG_TEXTURE_UNKNOWN;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *bytes_out = 2ul;
        *has_alpha = 0ul;
        return V9X_EG_COLOR_5_6_5;
    }
    /* ARGB1555 and ARGB4444, blue in the low bits like every format here:
     * the sampler's x is the low field, which the shaders take as blue. */
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *bytes_out = 2ul;
        *has_alpha = 1ul;
        return V9X_EG_COLOR_1_5_5_5;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *bytes_out = 2ul;
        *has_alpha = 1ul;
        return V9X_EG_COLOR_4_4_4_4;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *bytes_out = 4ul;
        *has_alpha = 1ul;
        return V9X_EG_COLOR_8_8_8_8;
    }
    return V9X_D3D_EG_TEXTURE_UNKNOWN;
}

static int v9x_d3d_eg_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *format_out)
{
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;

    if (format_out == 0) {
        return 0;
    }
    *format_out = v9x_d3d_eg_format_of(surface, &bytes, &has_alpha);
    return *format_out != V9X_D3D_EG_TEXTURE_UNKNOWN;
}

/*
 * What d3d_eg_map.c accepts: Z and alpha tests with every function, the
 * blend factors without destination alpha, flat and Gouraud colour and
 * alpha, textures in RGB565 and ARGB8888 with point and bilinear filters,
 * wrap, mirror and clamp, DECAL, COPY, MODULATE and MODULATEALPHA, and mip
 * chains with every Direct3D mip filter. No specular or fog: the shaders
 * do not draw them, so they are not offered.
 */
static void v9x_d3d_eg_describe_caps(V9X_DD_SHARED *shared)
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
                        V9X_D3DPRASTERCAPS_SUBPIXEL;
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
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_ALPHA;
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
        v9x_d3d_eg_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_eg_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_eg_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_eg_limits.texture_size_max;
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
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 4ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

static int v9x_d3d_eg_ready(void)
{
    return v9x_eg_cp_ready();
}

/*
 * A mip chain's levels, counted as far as each sits where
 * v9x_eg_3d_mip_layout puts it - create_surface placed it there in one
 * block - with level 0's pitch the layout's. The first attached level that
 * is not ends the chain; a chain of one is no chain. The diagnostics count
 * as the Rage 128's do.
 */
static void v9x_d3d_eg_resolve_chain(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD bytes, V9X_D3D_EG_TEXTURE *texture)
{
    v9x_u32 offsets[V9X_D3D_EG_LEVELS_MAX];
    v9x_u32 pitches[V9X_D3D_EG_LEVELS_MAX];
    v9x_u32 total;
    const V9X_DD_SURFACE_LCL *level = surface;
    DWORD longest = texture->width > texture->height ? texture->width
                                                     : texture->height;
    DWORD possible = 1ul;
    DWORD found = 1ul;

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    while (longest > 1ul && possible < V9X_D3D_EG_LEVELS_MAX) {
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
                ++v9x_hal->d3d_diagnostics.mip_gap_shape;
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
static void v9x_d3d_eg_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                       V9X_D3D_EG_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface =
        (const V9X_DD_SURFACE_LCL *)sampler->object;
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_EG_TEXTURE_UNKNOWN;
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
    texture->format = v9x_d3d_eg_format_of(surface, &bytes, &has_alpha);
    if (texture->format == V9X_D3D_EG_TEXTURE_UNKNOWN) {
        return;
    }
    texture->has_alpha = has_alpha;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_texels = (DWORD)surface->lpGbl->lPitch / bytes;
    texture->offset = offset;
    texture->levels = 1ul;
    texture->mip_offset = offset;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        v9x_d3d_eg_resolve_chain(surface, bytes, texture);
    }
}

static DWORD v9x_d3d_eg_depth_bytes(const V9X_R3D_DRAW *draw)
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
static void v9x_d3d_eg_count_refusal(const V9X_R3D_DRAW *draw, v9x_u32 reason)
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

static v9x_u32 v9x_d3d_eg_map(const V9X_R3D_DRAW *draw,
                              struct v9x_eg_3d_state *state,
                              v9x_u32 *vs_slot, v9x_u32 *ps_slot)
{
    V9X_D3D_EG_TEXTURE texture;
    DWORD heap_gpu;

    /* Every address in the batch is the heap's or the window's: without
     * them located the batch would point at the wrong memory. */
    if (!v9x_eg_cp_addresses(&heap_gpu)) {
        return V9X_D3D_EG_REFUSE_ADDRESS;
    }
    texture.format = V9X_D3D_EG_TEXTURE_UNKNOWN;
    if (draw->texture.object != 0) {
        v9x_d3d_eg_resolve_texture(&draw->texture, &texture);
    }
    return v9x_d3d_eg_map_draw(draw, &texture, heap_gpu,
                               v9x_d3d_eg_depth_bytes(draw), state, vs_slot,
                               ps_slot);
}

static int v9x_d3d_eg_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_eg_3d_state state;
    v9x_u32 vs_slot;
    v9x_u32 ps_slot;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_eg_map(draw, &state, &vs_slot, &ps_slot);
    if (reason != V9X_D3D_EG_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_EG_REFUSE_NONE;
}

/*
 * A run of triangles, in batches of at most V9X_D3D_EG_CHUNK_TRIANGLES:
 * each batch's vertices copied into the CP's arena as they are, then the
 * default state, the batch's state, the draw and the cache flush.
 */
static int v9x_d3d_eg_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    struct v9x_eg_3d_state state;
    struct v9x_eg_pm4 stream;
    volatile DWORD *linear;
    const DWORD *words;
    DWORD gpu;
    DWORD chunk;
    DWORD dwords;
    DWORD index;
    DWORD started;
    v9x_u32 vs_slot;
    v9x_u32 ps_slot;
    v9x_u32 reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_eg_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_eg_map(draw, &state, &vs_slot, &ps_slot);
    if (reason != V9X_D3D_EG_REFUSE_NONE) {
        v9x_d3d_eg_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    state.vs_gpu = v9x_eg_cp_shader_gpu(vs_slot);
    state.ps_gpu = v9x_eg_cp_shader_gpu(ps_slot);
    state.vertex_stride = V9X_EG_D3D_STRIDE;

    while (triangle_count != 0ul) {
        chunk = triangle_count > V9X_D3D_EG_CHUNK_TRIANGLES ?
                V9X_D3D_EG_CHUNK_TRIANGLES : triangle_count;
        dwords = chunk * 3ul * V9X_D3D_EG_VERTEX_DWORDS;
        if (!v9x_eg_cp_vertex_space(dwords * 4ul, &linear, &gpu)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        words = (const DWORD *)vertices;
        for (index = 0ul; index < dwords; ++index) {
            linear[index] = words[index];
        }
        state.vertex_gpu = gpu;
        state.vertex_bytes = dwords * 4ul;

        v9x_eg_pm4_begin(&stream, v9x_d3d_eg_stream,
                         V9X_D3D_EG_STREAM_DWORDS);
        v9x_eg_3d_default_state(&stream);
        if (v9x_eg_3d_build_state(&state, &stream) != V9X_STATUS_OK) {
            /* The target the builder would not take, in the trace's
             * DrawsTargetLast, DrawsPitchLast and DrawsExtentLast. */
            v9x_hal->d3d_diagnostics.draws_target_last = state.target_gpu;
            v9x_hal->d3d_diagnostics.draws_pitch_last = state.target_pitch;
            v9x_hal->d3d_diagnostics.draws_extent_last =
                state.scissor_right | (state.target_height << 16);
            v9x_d3d_eg_count_refusal(draw, V9X_D3D_EG_REFUSE_STATE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        v9x_eg_3d_draw(&stream, chunk * 3ul);
        v9x_eg_3d_flush(&stream);
        v9x_eg_pm4_pad(&stream);
        if (stream.overflow != 0ul) {
            v9x_d3d_eg_count_refusal(draw, V9X_D3D_EG_REFUSE_OVERFLOW);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        V9X_TIME_END(V9X_TIME_DECODE, started);
        if (!v9x_eg_cp_submit(stream.dwords, stream.count)) {
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

/*
 * Placement. An offscreen render target gets its pitch rounded to 8
 * pixels, 256-aligned. A Z16 buffer gets its pitch rounded to 8 pixels
 * and its rows to 8, the DB's 1D tile, 256-aligned. A video-memory
 * texture of a format this engine samples gets each level in a block of
 * its own, 256-aligned, its rows whole multiples of 64 bytes, which the
 * sampler needs (eg_3d.h, v9x_eg_3d_texture_pitch): V9XDDP's 16x16 1555
 * TexPhase texture at 32 bytes a row read every odd row wrong.
 */
static DWORD v9x_d3d_eg_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_D3D_EG_LEVELS_MAX];
    v9x_u32 rows[V9X_D3D_EG_LEVELS_MAX];
    v9x_u32 offsets[V9X_D3D_EG_LEVELS_MAX];
    v9x_u32 total = 0ul;
    DWORD base = 0ul;
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD format = V9X_D3D_EG_TEXTURE_UNKNOWN;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_EG_LEVELS_MAX || data->lplpSList == 0) {
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
        /* An offscreen render target alone: its pitch whole 8-pixel
         * tiles and its base on 256 bytes, which CB_COLOR0_PITCH and
         * _BASE need and DirectDraw's 8-byte placement does not give.
         * V9XD32's T4 target was refused for it once the textures before
         * it were placed (boot 18). A flip chain's back buffers come with
         * the primary, which this does not place; the heap puts them
         * after it, on whole screens. */
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
        rows[0] = height;
    } else if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (data->dwSCnt != 1ul ||
            v9x_d3d_depth_bytes_per_pixel(surface) != 2ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = (width * 2ul + V9X_D3D_EG_DEPTH_PITCH_UNIT - 1ul) &
                     ~(V9X_D3D_EG_DEPTH_PITCH_UNIT - 1ul);
        rows[0] = (height + V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul) &
                  ~(V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul);
    } else {
        format = v9x_d3d_eg_format_of(surface, &bytes, &has_alpha);
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            width > v9x_d3d_eg_limits.texture_size_max ||
            height > v9x_d3d_eg_limits.texture_size_max ||
            format == V9X_D3D_EG_TEXTURE_UNKNOWN) {
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
            pitches[level] = v9x_eg_3d_texture_pitch(format, width) * bytes;
            rows[level] = height;
        }
        /* A chain in one block, where the sampler finds its levels from
         * the base and MIP_ADDRESS (eg_3d.h, v9x_eg_3d_mip_layout). */
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
            if (v9x_d3d_place_chain(data, V9X_D3D_EG_SURFACE_ALIGN, total,
                                    offsets, pitches, &base) != 0ul) {
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

static void v9x_d3d_eg_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_EG_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_eg = {
    &v9x_d3d_eg_limits,
    v9x_d3d_eg_texture_format,
    v9x_d3d_eg_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_eg_ready,
    v9x_d3d_eg_create_surface,
    v9x_d3d_eg_destroy_surface,
    v9x_d3d_eg_draw,
    v9x_d3d_eg_accepts
};
