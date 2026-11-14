/*
 * NVIDIA GT218 (GeForce 210) Direct3D engine, on the Tesla 3D class
 * (8597h) through the 2D engine's one PFIFO channel.
 *
 * Step 5a of docs\plans\nvidia-gt218-3d-z.md: pitch-linear targets, no
 * depth buffer yet (step 5b adds the shadow targets Michael chose). The
 * programs, state and vertices come from src\chipsets\nvidia\gt218\
 * nv50_d3d.c, the translation from the neutral draw from d3d_nv50_map.c,
 * both host-tested; the channel probe's /3d:d3d stage drew the same
 * builders right on M-BXMFA8 (docs\decisions\2026-11-14-gt218-3d-d3d-
 * builders.md). This file resolves DirectDraw surfaces, places targets
 * and textures, writes the programs and texture tables into the reserve's
 * 3D scratch and submits through eng_nv50.c.
 *
 * Every batch carries the draw's whole state; the 3D object's bind and
 * EXA's setup go with the first batch on a channel, and again whenever
 * the channel was rebuilt (its build count changed). The vertices go
 * inline, one method run per attribute.
 *
 * Emission order is the refusal contract: the mapping is made first, and
 * a draw the map refuses writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_nv50_map.h"
#include "velocity9x/nv50_d3d.h"

/* Triangles one batch takes: at the widest vertex (18 words) 2,048 fill
 * 110,592 of the push area's 196,608 words, with room for the setup and
 * the state. */
#define V9X_D3D_NV50_CHUNK_TRIANGLES 2048ul
#define V9X_D3D_NV50_LEVELS_MAX      12ul

/* 16 and 32 bpp targets, textures to 2048, the core clipping every
 * triangle to the target; the engine samples at x + 0.5 (the probe drew
 * Direct3D's rectangles exactly with half a pixel added), so the core
 * moves the vertices; one stage. */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_nv50_limits = {
    16ul,           /* target_bits_per_pixel */
    32704ul,        /* target_pitch_max */
    V9X_D3D_NV50_PITCH_ALIGN, /* target_pitch_align */
    8192ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    V9X_D3D_NV50_TEXTURE_SIDE_MAX, /* texture_size_max */
    8192.0f,        /* coordinate_limit: EXA's scissor bounds */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_NV50_SURFACE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    1ul,            /* pixel_centre_half */
    0ul,            /* depth_tiled: no Z on the engine yet */
    65472ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits */
    0ul,            /* centre_in_engine: the core moves, as it clips */
    1ul             /* texture_stages */
};

/* The programs' register counts, filled when the programs are built. */
static v9x_u32 v9x_d3d_nv50_fp_registers[V9X_NV50_D3D_FP_VARIANTS];
/* The channel build the bind and setup were last sent on; none yet. */
static DWORD v9x_d3d_nv50_setup_generation = 0xfffffffful;
static int v9x_d3d_nv50_setup_sent = 0;
static v9x_u32 v9x_d3d_nv50_program[V9X_NV50_D3D_FP_WORDS_MAX];

/* The surface's texture format (V9X_NV50_D3D_TEX_*) and bytes a texel,
 * or UNKNOWN. */
static DWORD v9x_d3d_nv50_format_of(const V9X_DD_SURFACE_LCL *surface,
                                    DWORD *bytes_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (surface == 0 || surface->lpGbl == 0) {
        return V9X_D3D_NV50_TEXTURE_UNKNOWN;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return V9X_D3D_NV50_TEXTURE_UNKNOWN;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *bytes_out = 2ul;
        return V9X_NV50_D3D_TEX_B5G6R5;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *bytes_out = 2ul;
        return V9X_NV50_D3D_TEX_B5G5R5A1;
    }
    if (format->dwRGBBitCount == 16ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *bytes_out = 2ul;
        return V9X_NV50_D3D_TEX_B4G4R4A4;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *bytes_out = 4ul;
        return V9X_NV50_D3D_TEX_B8G8R8A8;
    }
    return V9X_D3D_NV50_TEXTURE_UNKNOWN;
}

static int v9x_d3d_nv50_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    DWORD bytes = 0ul;

    if (format_out == 0) {
        return 0;
    }
    *format_out = v9x_d3d_nv50_format_of(surface, &bytes);
    return *format_out != V9X_D3D_NV50_TEXTURE_UNKNOWN;
}

/*
 * What d3d_nv50_map.c accepts: the alpha test with every function, the
 * blend factors without destination alpha, flat and Gouraud colour and
 * alpha, textures in RGB565, ARGB1555, ARGB4444 and ARGB8888, point and
 * bilinear, wrap, mirror and clamp, DECAL, COPY, MODULATE and
 * MODULATEALPHA, the mip filters read on level 0; specular, and vertex
 * and table fog. Z16 is published for the shadow targets of step 5b;
 * until then a Z-tested draw is refused and counted.
 */
static void v9x_d3d_nv50_describe_caps(V9X_DD_SHARED *shared)
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
        v9x_d3d_nv50_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_nv50_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_nv50_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_nv50_limits.texture_size_max;
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

static int v9x_d3d_nv50_ready(void)
{
    return v9x_hal != 0 &&
           (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
           v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_NV50 &&
           v9x_hal->engine.reserve_offset != 0ul;
}

/* The bound texture's level 0 as the map takes it. */
static void v9x_d3d_nv50_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                         V9X_D3D_NV50_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface =
        (const V9X_DD_SURFACE_LCL *)sampler->object;
    DWORD bytes = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
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
    texture->format = v9x_d3d_nv50_format_of(surface, &bytes);
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch = (DWORD)surface->lpGbl->lPitch;
    texture->offset = offset;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1ul;
        texture->key = surface->ddckCKSrcBltLow;
    }
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the other engines count their own. */
static void v9x_d3d_nv50_count_refusal(const V9X_R3D_DRAW *draw,
                                       v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_NV50_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_NV50_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/* The register counts, from the programs themselves, once. */
static int v9x_d3d_nv50_count_registers(void)
{
    static int counted = 0;
    v9x_u32 variant;

    if (counted) {
        return 1;
    }
    for (variant = 0ul; variant < V9X_NV50_D3D_FP_VARIANTS; ++variant) {
        if (v9x_nv50_d3d_fp(variant, v9x_d3d_nv50_program,
                            V9X_NV50_D3D_FP_WORDS_MAX,
                            &v9x_d3d_nv50_fp_registers[variant]) == 0ul) {
            return 0;
        }
    }
    counted = 1;
    return 1;
}

static v9x_u32 v9x_d3d_nv50_map(const V9X_R3D_DRAW *draw,
                                struct v9x_nv50_d3d_state *state,
                                v9x_u32 *tic, v9x_u32 *tsc,
                                v9x_u32 *with_specular)
{
    V9X_D3D_NV50_TEXTURE texture;

    if (!v9x_d3d_nv50_count_registers()) {
        return V9X_D3D_NV50_REFUSE_STATE;
    }
    texture.format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    if (draw->texture.object != 0) {
        v9x_d3d_nv50_resolve_texture(&draw->texture, &texture);
    }
    return v9x_d3d_nv50_map_draw(draw, &texture, v9x_d3d_nv50_fp_registers,
                                 state, tic, tsc, with_specular);
}

static int v9x_d3d_nv50_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_nv50_d3d_state state;
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 with_specular;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_nv50_map(draw, &state, tic, tsc, &with_specular);
    if (reason != V9X_D3D_NV50_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_NV50_REFUSE_NONE;
}

/* The vertex program and every fragment variant into the scratch. */
static int v9x_d3d_nv50_write_programs(volatile DWORD *scratch)
{
    v9x_u32 count;
    v9x_u32 variant;
    v9x_u32 index;
    v9x_u32 registers;
    volatile DWORD *at;

    count = v9x_nv50_d3d_vp(v9x_d3d_nv50_program, V9X_NV50_D3D_FP_WORDS_MAX);
    if (count == 0ul) {
        return 0;
    }
    at = scratch + (V9X_NV50_3D_SCRATCH_VP + V9X_NV50_D3D_VP_START) / 4ul;
    for (index = 0ul; index < count; ++index) {
        at[index] = v9x_d3d_nv50_program[index];
    }
    for (variant = 0ul; variant < V9X_NV50_D3D_FP_VARIANTS; ++variant) {
        count = v9x_nv50_d3d_fp(variant, v9x_d3d_nv50_program,
                                V9X_NV50_D3D_FP_WORDS_MAX, &registers);
        if (count == 0ul) {
            return 0;
        }
        at = scratch + (V9X_NV50_3D_SCRATCH_FP +
                        v9x_nv50_d3d_fp_start(variant)) / 4ul;
        for (index = 0ul; index < count; ++index) {
            at[index] = v9x_d3d_nv50_program[index];
        }
    }
    return 1;
}

/*
 * A run of triangles, in batches of at most V9X_D3D_NV50_CHUNK_TRIANGLES:
 * each batch waits for the engine to drain (eng_nv50.c), writes the
 * programs and setup when the channel is new to it and the texture's
 * tables when the draw is textured, then the state, the vertices inline,
 * and the kick.
 */
static int v9x_d3d_nv50_draw(const V9X_R3D_DRAW *draw,
                             const V9X_R3D_VERTEX *vertices,
                             DWORD triangle_count)
{
    struct v9x_nv50_d3d_state state;
    struct v9x_nv50_3d_scratch scratch_gpu;
    struct v9x_nv50_push push;
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD chunk;
    DWORD index;
    DWORD started;
    v9x_u32 with_specular = 0ul;
    v9x_u32 reason;
    int setup;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_nv50_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_nv50_map(draw, &state, tic, tsc, &with_specular);
    if (reason != V9X_D3D_NV50_REFUSE_NONE) {
        v9x_d3d_nv50_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }

    while (triangle_count != 0ul) {
        chunk = triangle_count > V9X_D3D_NV50_CHUNK_TRIANGLES ?
                V9X_D3D_NV50_CHUNK_TRIANGLES : triangle_count;
        if (!v9x_nv50_3d_begin(&area, &area_words, &scratch, &scratch_base,
                               &generation)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        setup = !v9x_d3d_nv50_setup_sent ||
                generation != v9x_d3d_nv50_setup_generation;
        if (setup && !v9x_d3d_nv50_write_programs(scratch)) {
            v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_REFUSE_STATE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        if (state.textured != 0ul) {
            for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
                scratch[V9X_NV50_3D_SCRATCH_TIC / 4ul + index] = tic[index];
                scratch[V9X_NV50_3D_SCRATCH_TSC / 4ul + index] = tsc[index];
            }
        }

        /* The push is built in place in the reserve, which nothing reads
         * until the kick. */
        v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
        if (setup) {
            v9x_nv50_3d_scratch_at(scratch_base, &scratch_gpu);
            v9x_nv50_push_bind_3d(&push);
            v9x_nv50_push_3d_setup(&push, &scratch_gpu);
        }
        v9x_nv50_push_d3d_state(&push, &state);
        v9x_nv50_push_d3d_begin(&push);
        for (index = 0ul; index < chunk * 3ul; ++index) {
            const V9X_R3D_VERTEX *vertex = &vertices[index];

            v9x_nv50_push_d3d_vertex(&push, vertex->sx, vertex->sy,
                                     vertex->sz, vertex->rhw, vertex->color,
                                     vertex->specular, vertex->tu,
                                     vertex->tv, draw->centre_offset,
                                     with_specular, state.textured);
        }
        v9x_nv50_push_d3d_end(&push);
        if (push.overflow != 0ul) {
            v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_REFUSE_OVERFLOW);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        V9X_TIME_END(V9X_TIME_DECODE, started);
        if (!v9x_nv50_3d_submit(push.count)) {
            return 0;
        }
        if (setup) {
            v9x_d3d_nv50_setup_generation = generation;
            v9x_d3d_nv50_setup_sent = 1;
        }
        started = V9X_TIME_BEGIN();
        vertices += chunk * 3ul;
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return 1;
}

/*
 * Placement. An offscreen render target alone gets its rows on 64 bytes
 * and its base on 256, the rule every surface the engine has drawn into
 * kept (d3d_nv50_map.h); a flip chain's buffers come with the primary,
 * which this does not place. A video-memory texture of a format this
 * engine samples gets each level in a block of its own on the same
 * rule. Z buffers are DirectDraw's until step 5b.
 */
static DWORD v9x_d3d_nv50_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 rows[V9X_D3D_NV50_LEVELS_MAX];
    DWORD bytes = 0ul;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_NV50_LEVELS_MAX || data->lplpSList == 0) {
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
        pitches[0] = (width * bytes + V9X_D3D_NV50_PITCH_ALIGN - 1ul) &
                     ~(V9X_D3D_NV50_PITCH_ALIGN - 1ul);
        rows[0] = height;
    } else if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) != 0ul &&
               (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul) {
        if (width > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
            height > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
            v9x_d3d_nv50_format_of(surface, &bytes) ==
                V9X_D3D_NV50_TEXTURE_UNKNOWN) {
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
            pitches[level] = (width * bytes + V9X_D3D_NV50_PITCH_ALIGN - 1ul) &
                             ~(V9X_D3D_NV50_PITCH_ALIGN - 1ul);
            rows[level] = height;
        }
    } else {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (v9x_d3d_place_each(data, V9X_D3D_NV50_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_nv50_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_NV50_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_nv50 = {
    &v9x_d3d_nv50_limits,
    v9x_d3d_nv50_texture_format,
    v9x_d3d_nv50_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_nv50_ready,
    v9x_d3d_nv50_create_surface,
    v9x_d3d_nv50_destroy_surface,
    v9x_d3d_nv50_draw,
    v9x_d3d_nv50_accepts
};
