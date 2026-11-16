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
#include "velocity9x/nv50_ps11.h"
#include "velocity9x/nv50_rblock.h"

/* Triangles one batch takes: at the widest vertex (18 words) 2,048 fill
 * 110,592 of the push area's 196,608 words, with room for the setup and
 * the state. */
#define V9X_D3D_NV50_CHUNK_TRIANGLES 2048ul
#define V9X_D3D_NV50_LEVELS_MAX      12ul

/* 16 and 32 bpp targets, textures to 2048; the engine samples at x + 0.5
 * (the probe drew Direct3D's rectangles exactly with half a pixel added),
 * and adds that half itself as it sends each vertex. Two stages, so the
 * core neither clips nor moves the vertices: the engine draws inside its
 * guard band (V9X_D3D_NV50_GUARD) and the scissor keeps every pixel on
 * the target (step 5 of docs\plans\nvidia-gt218-ps11.md, docs\decisions\
 * 2026-11-15-gt218-guard-band-two-stages.md). Until then the core clipped
 * every triangle and moved its vertices for one stage. */
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
    0ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    1ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: Z16 is block-linear, kind 6Ch */
    65472ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits */
    1ul,            /* centre_in_engine: v9x_nv50_push_d3d_vertex adds it */
    2ul,            /* texture_stages */
    /* DDI 6 programs are told the band (coordinate_limit less a pixel),
     * far inside the measured one, so the runtime stops clipping their
     * triangles on the CPU, as on Gen3 and the HD 4600. */
    1ul             /* guard_band_ddi6 */
};

/*
 * The guard band, in coordinates: a triangle with every vertex inside
 * +/- this is drawn as given, the scissor holding it to the target; one
 * with a vertex beyond or not finite is dropped, as the core's clipper
 * refuses such a vertex. MEASURED on W9QI0DAD (V9XN5CH /3d:guard, boot
 * 22): no pixel left an 8-pixel-inset scissor and colour held within one
 * step with vertices 32,768 pixels off the target, coordinates to 65,836,
 * the furthest drawn. Halved for margin, as Gen3's band was.
 */
#define V9X_D3D_NV50_GUARD           16384.0f
/* Finite, from the bits: Open Watcom's x87 compares do not see NaN
 * (docs\decisions\2026-09-25-nan-detection-in-the-z-conversion.md). */
#define V9X_D3D_NV50_EXPONENT_MASK   0x7f800000ul

/*
 * Step 5b's shadow targets (d3d_nv50_map.h). Each Z buffer this engine
 * places carries a tiled colour shadow of its size, in the same block.
 * A shadow holds one linear target at a time (owner, its offset): loaded
 * from it by a 502D copy before the first draw that needs it, drawn into
 * since (dirty), and copied back before anything reads or writes the
 * linear target. That is eng_nv50.c's wait_idle, which Flip, Lock and
 * every Blt pass through, and the surface_written hook for a Blt's
 * destination and an Unlock. A target being destroyed drops its shadow
 * without a copy.
 */
#define V9X_D3D_NV50_SHADOWS_MAX     8ul
#define V9X_D3D_NV50_NONE            0xfffffffful
/* The Z and its colour shadow each fill whole row-block units of their
 * own (nv50_rblock.h): a tiled kind spreads a surface over the row block
 * it lies in, so on 64 KiB alone Quake 3's 800x600 Z put its top rows in
 * the back buffer below it (W9QI0DAD, V9XN5CH /z:map, 2026-11-16). The
 * heap block is asked on 4 KiB, its mapping's alignment. */
#define V9X_D3D_NV50_HEAP_ALIGN      0x1000ul

typedef struct v9x_d3d_nv50_shadow {
    DWORD used;
    DWORD z_offset;
    DWORD width;
    DWORD height;
    DWORD colour_offset;
    DWORD owner;
    DWORD owner_pitch;
    DWORD owner_width;
    DWORD owner_height;
    DWORD owner_bytes;
    DWORD dirty;
    /* How far past its heap block the Z was moved, plus one: what
     * v9x_d3d_place_release must allow to recognise the block. */
    DWORD release_window;
} V9X_D3D_NV50_SHADOW;

static V9X_D3D_NV50_SHADOW v9x_d3d_nv50_shadows[V9X_D3D_NV50_SHADOWS_MAX];
/* Set while a resolve's own copies run, which pass through the engine. */
static int v9x_d3d_nv50_resolving = 0;

/* The programs' register counts, filled when the programs are built. */
static v9x_u32 v9x_d3d_nv50_fp_registers[V9X_NV50_D3D_FP_VARIANTS];
/* The channel build the bind and setup were last sent on; none yet. */
static DWORD v9x_d3d_nv50_setup_generation = 0xfffffffful;
static int v9x_d3d_nv50_setup_sent = 0;
static v9x_u32 v9x_d3d_nv50_program[V9X_NV50_D3D_FP_WORDS_MAX];

/*
 * Two-unit programs, built on first use into the slots after the
 * scratch (nv50_d3d.h, V9X_NV50_D3D_FP2_*): the combination key and
 * register count of each slot filled, in order. A slot is written once
 * per channel build and never while a draw might run it, since it was
 * empty; the cache starts over when the channel is rebuilt. Full, a new
 * combination is refused and counted (V9X_D3D_NV50_REFUSE_PROGRAMS).
 */
static v9x_u32 v9x_d3d_nv50_fp2_keys[V9X_NV50_D3D_FP2_SLOTS];
static v9x_u32 v9x_d3d_nv50_fp2_registers[V9X_NV50_D3D_FP2_SLOTS];
static DWORD v9x_d3d_nv50_fp2_count = 0ul;
static DWORD v9x_d3d_nv50_fp2_generation = 0xfffffffful;
static v9x_u32 v9x_d3d_nv50_program2[V9X_NV50_D3D_FP2_WORDS_MAX];

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
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful) {
        *bytes_out = 4ul;
        return V9X_NV50_D3D_TEX_B8G8R8X8;
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
 * alpha, textures in RGB565, ARGB1555, ARGB4444, ARGB8888 and XRGB8888,
 * point and
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

    /* RGB565, ARGB1555, ARGB4444, ARGB8888, XRGB8888. */
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

/* The shadow placed with the Z buffer at z_offset, or the one holding the
 * linear target at owner; null for none. */
static V9X_D3D_NV50_SHADOW *v9x_d3d_nv50_shadow_of_z(DWORD z_offset)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_NV50_SHADOWS_MAX; ++index) {
        if (v9x_d3d_nv50_shadows[index].used != 0ul &&
            v9x_d3d_nv50_shadows[index].z_offset == z_offset) {
            return &v9x_d3d_nv50_shadows[index];
        }
    }
    return 0;
}

static V9X_D3D_NV50_SHADOW *v9x_d3d_nv50_shadow_owning(DWORD owner)
{
    DWORD index;

    if (owner == V9X_D3D_NV50_NONE) {
        return 0;
    }
    for (index = 0ul; index < V9X_D3D_NV50_SHADOWS_MAX; ++index) {
        if (v9x_d3d_nv50_shadows[index].used != 0ul &&
            v9x_d3d_nv50_shadows[index].owner == owner) {
            return &v9x_d3d_nv50_shadows[index];
        }
    }
    return 0;
}

/* The shadow as the map and the depth fill take it: the colour through
 * the kind-70h window, the Z through the Z16 one, both tiled. */
static void v9x_d3d_nv50_view_of(const V9X_D3D_NV50_SHADOW *shadow,
                                 DWORD with_zeta,
                                 V9X_D3D_NV50_SHADOW_VIEW *view)
{
    view->colour.address = V9X_NV50_VM_TILED + shadow->colour_offset;
    view->colour.pitch = 0ul;
    view->colour.width = shadow->width;
    view->colour.height = shadow->height;
    view->colour.tile_mode = V9X_D3D_NV50_TILE_MODE;
    view->colour.tiled = 1ul;
    view->zeta.address = V9X_NV50_VM_Z16 + shadow->z_offset;
    view->zeta.pitch = 0ul;
    view->zeta.width = shadow->width;
    view->zeta.height = shadow->height;
    view->zeta.tile_mode = V9X_D3D_NV50_TILE_MODE;
    view->zeta.tiled = 1ul;
    view->zeta_format = V9X_NV50_3D_ZETA_Z16;
    view->with_zeta = with_zeta;
}

/*
 * One 502D copy between a shadow and the linear target it holds, the
 * owner's size: to the linear target (a resolve) or from it (a load).
 * Its own kick, after everything before it (eng_nv50.c drains first).
 */
static int v9x_d3d_nv50_shadow_copy(const V9X_D3D_NV50_SHADOW *shadow,
                                    int to_linear)
{
    struct v9x_nv50_surface linear;
    struct v9x_nv50_surface tiled;
    struct v9x_nv50_push push;
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD format = v9x_nv50_2d_format(shadow->owner_bytes);

    if (format == 0ul ||
        !v9x_nv50_3d_begin(&area, &area_words, &scratch, &scratch_base,
                           &generation)) {
        return 0;
    }
    linear.address = shadow->owner;
    linear.pitch = shadow->owner_pitch;
    linear.width = shadow->owner_width;
    linear.height = shadow->owner_height;
    linear.tile_mode = 0ul;
    linear.tiled = 0ul;
    tiled.address = V9X_NV50_VM_TILED + shadow->colour_offset;
    tiled.pitch = 0ul;
    tiled.width = shadow->width;
    tiled.height = shadow->height;
    tiled.tile_mode = V9X_D3D_NV50_TILE_MODE;
    tiled.tiled = 1ul;
    v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
    if (to_linear) {
        v9x_nv50_push_2d_surface_blit(&push, &tiled, &linear, format, 0ul,
                                      0ul, 0ul, 0ul, shadow->owner_width,
                                      shadow->owner_height);
    } else {
        v9x_nv50_push_2d_surface_blit(&push, &linear, &tiled, format, 0ul,
                                      0ul, 0ul, 0ul, shadow->owner_width,
                                      shadow->owner_height);
    }
    if (push.overflow != 0ul) {
        return 0;
    }
    return v9x_nv50_3d_submit(push.count);
}

/* A dirty shadow back to its linear target; it then still holds it. */
static int v9x_d3d_nv50_shadow_flush(V9X_D3D_NV50_SHADOW *shadow)
{
    int ok = 1;

    if (shadow->dirty == 0ul || shadow->owner == V9X_D3D_NV50_NONE) {
        return 1;
    }
    v9x_d3d_nv50_resolving = 1;
    ok = v9x_d3d_nv50_shadow_copy(shadow, 1);
    v9x_d3d_nv50_resolving = 0;
    if (ok) {
        shadow->dirty = 0ul;
    }
    return ok;
}

/* ddhal_internal.h: every dirty shadow back to its target. */
int v9x_d3d_nv50_resolve(void)
{
    DWORD index;
    int ok = 1;

    if (v9x_d3d_nv50_resolving) {
        return 1;
    }
    for (index = 0ul; index < V9X_D3D_NV50_SHADOWS_MAX; ++index) {
        if (v9x_d3d_nv50_shadows[index].used != 0ul &&
            !v9x_d3d_nv50_shadow_flush(&v9x_d3d_nv50_shadows[index])) {
            ok = 0;
        }
    }
    return ok;
}

/* The linear target at offset changes outside the engine: flushed first
 * if a shadow has drawing for it, then no shadow holds it. */
static void v9x_d3d_nv50_release_target(DWORD offset, int flush)
{
    V9X_D3D_NV50_SHADOW *shadow = v9x_d3d_nv50_shadow_owning(offset);

    if (shadow == 0) {
        return;
    }
    if (flush) {
        (void)v9x_d3d_nv50_shadow_flush(shadow);
    }
    shadow->owner = V9X_D3D_NV50_NONE;
    shadow->dirty = 0ul;
}

/*
 * The shadow a draw goes to, if any: its Z buffer's when it tests Z
 * against one this engine placed, else the one holding its target. view
 * is filled for the map.
 */
static V9X_D3D_NV50_SHADOW *v9x_d3d_nv50_shadow_for(
    const V9X_R3D_DRAW *draw, V9X_D3D_NV50_SHADOW_VIEW *view)
{
    V9X_D3D_NV50_SHADOW *shadow = 0;
    DWORD with_zeta = 0ul;

    if (draw->depth_enable != 0ul && draw->depth.object != 0) {
        shadow = v9x_d3d_nv50_shadow_of_z(draw->depth.offset);
        with_zeta = 1ul;
    }
    if (shadow == 0) {
        shadow = v9x_d3d_nv50_shadow_owning(draw->target.offset);
        with_zeta = 0ul;
    }
    if (shadow != 0) {
        v9x_d3d_nv50_view_of(shadow, with_zeta, view);
    }
    return shadow;
}

/* Make shadow hold the draw's target: another shadow holding it is
 * flushed and let go, this one's own drawing flushed, then the load. */
static int v9x_d3d_nv50_shadow_take(V9X_D3D_NV50_SHADOW *shadow,
                                    const V9X_R3D_DRAW *draw)
{
    if (shadow->owner == draw->target.offset) {
        return 1;
    }
    v9x_d3d_nv50_release_target(draw->target.offset, 1);
    if (!v9x_d3d_nv50_shadow_flush(shadow)) {
        return 0;
    }
    shadow->owner = draw->target.offset;
    shadow->owner_pitch = draw->target.pitch;
    shadow->owner_width = draw->target.width;
    shadow->owner_height = draw->target.height;
    shadow->owner_bytes =
        draw->target.format == V9X_R3D_FORMAT_XRGB8888 ? 4ul : 2ul;
    shadow->dirty = 0ul;
    v9x_d3d_nv50_resolving = 1;
    if (!v9x_d3d_nv50_shadow_copy(shadow, 0)) {
        v9x_d3d_nv50_resolving = 0;
        shadow->owner = V9X_D3D_NV50_NONE;
        return 0;
    }
    v9x_d3d_nv50_resolving = 0;
    return 1;
}

static v9x_u32 v9x_d3d_nv50_map(const V9X_R3D_DRAW *draw,
                                const V9X_D3D_NV50_SHADOW_VIEW *view,
                                struct v9x_nv50_d3d_state *state,
                                v9x_u32 *tic, v9x_u32 *tsc,
                                v9x_u32 *with_specular)
{
    V9X_D3D_NV50_TEXTURE texture[2];

    if (!v9x_d3d_nv50_count_registers()) {
        return V9X_D3D_NV50_REFUSE_STATE;
    }
    texture[0].format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    texture[1].format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    if (draw->texture.object != 0) {
        v9x_d3d_nv50_resolve_texture(&draw->texture, &texture[0]);
    }
    if (draw->texcoords1 != 0 && draw->texture1.object != 0) {
        v9x_d3d_nv50_resolve_texture(&draw->texture1, &texture[1]);
    }
    return v9x_d3d_nv50_map_draw(draw, texture, view,
                                 v9x_d3d_nv50_fp_registers, state, tic, tsc,
                                 with_specular);
}

static int v9x_d3d_nv50_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_nv50_d3d_state state;
    V9X_D3D_NV50_SHADOW_VIEW view;
    v9x_u32 tic[2ul * V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[2ul * V9X_NV50_3D_TSC_WORDS];
    v9x_u32 with_specular;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_nv50_map(draw,
                              v9x_d3d_nv50_shadow_for(draw, &view) != 0 ?
                                  &view : 0,
                              &state, tic, tsc, &with_specular);
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
    count = v9x_nv50_d3d_shader_vp(v9x_d3d_nv50_program,
                                   V9X_NV50_D3D_FP_WORDS_MAX);
    if (count == 0ul) {
        return 0;
    }
    at = scratch +
         (V9X_NV50_3D_SCRATCH_VP + V9X_NV50_D3D_SHADER_VP_START) / 4ul;
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
 * Whether a triangle is drawn as given: every coordinate finite and inside
 * the guard band (V9X_D3D_NV50_GUARD).
 */
static int v9x_d3d_nv50_guard(const V9X_R3D_VERTEX *triangle)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;
    DWORD index;
    DWORD axis;

    for (index = 0ul; index < 3ul; ++index) {
        for (axis = 0ul; axis < 2ul; ++axis) {
            convert.value = axis == 0ul ? triangle[index].sx
                                        : triangle[index].sy;
            if ((convert.bits & V9X_D3D_NV50_EXPONENT_MASK) ==
                    V9X_D3D_NV50_EXPONENT_MASK ||
                !(convert.value >= -V9X_D3D_NV50_GUARD &&
                  convert.value < V9X_D3D_NV50_GUARD)) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * A two-unit draw's program: found in the cache by its combine, or built
 * into the next empty slot through scratch, the reserve's CPU view, which
 * then asks the push for CODE_CB_FLUSH. The cache starts over on a new
 * channel generation. 0 when the cache is full or the program does not
 * build.
 */
static int v9x_d3d_nv50_fp2_program(volatile DWORD *scratch,
                                    DWORD generation,
                                    struct v9x_nv50_d3d_state *state)
{
    v9x_u32 key = v9x_nv50_d3d_combine_key(&state->combine);
    v9x_u32 count;
    v9x_u32 registers;
    volatile DWORD *at;
    DWORD slot;
    DWORD index;

    state->code_flush = 0ul;
    if (key == V9X_NV50_D3D_COMBINE_NONE) {
        return 0;
    }
    if (generation != v9x_d3d_nv50_fp2_generation) {
        v9x_d3d_nv50_fp2_count = 0ul;
        v9x_d3d_nv50_fp2_generation = generation;
    }
    for (slot = 0ul; slot < v9x_d3d_nv50_fp2_count; ++slot) {
        if (v9x_d3d_nv50_fp2_keys[slot] == key) {
            state->fp_start = v9x_nv50_d3d_fp2_start(slot);
            state->fp_registers = v9x_d3d_nv50_fp2_registers[slot];
            return 1;
        }
    }
    if (slot >= V9X_NV50_D3D_FP2_SLOTS) {
        return 0;
    }
    count = v9x_nv50_d3d_fp2(&state->combine, v9x_d3d_nv50_program2,
                             V9X_NV50_D3D_FP2_WORDS_MAX, &registers);
    if (count == 0ul) {
        return 0;
    }
    at = scratch + (V9X_NV50_3D_SCRATCH_FP + v9x_nv50_d3d_fp2_start(slot)) /
                       4ul;
    for (index = 0ul; index < count; ++index) {
        at[index] = v9x_d3d_nv50_program2[index];
    }
    v9x_d3d_nv50_fp2_keys[slot] = key;
    v9x_d3d_nv50_fp2_registers[slot] = registers;
    v9x_d3d_nv50_fp2_count = slot + 1ul;
    state->fp_start = v9x_nv50_d3d_fp2_start(slot);
    state->fp_registers = registers;
    state->code_flush = 1ul;
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
    v9x_u32 tic[2ul * V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[2ul * V9X_NV50_3D_TSC_WORDS];
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD chunk;
    DWORD index;
    DWORD corner;
    DWORD tables;
    DWORD started;
    const float *uv1 = draw != 0 ? draw->texcoords1 : 0;
    v9x_u32 with_specular = 0ul;
    v9x_u32 reason;
    int setup;
    V9X_D3D_NV50_SHADOW *shadow;
    V9X_D3D_NV50_SHADOW_VIEW view;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_nv50_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    shadow = v9x_d3d_nv50_shadow_for(draw, &view);
    reason = v9x_d3d_nv50_map(draw, shadow != 0 ? &view : 0, &state, tic,
                              tsc, &with_specular);
    if (reason != V9X_D3D_NV50_REFUSE_NONE) {
        v9x_d3d_nv50_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    /* The shadow loaded with the target before the first draw into it;
     * a draw straight into the linear target flushes and lets go of any
     * shadow holding it, so the two never both have drawing. */
    if (shadow != 0) {
        if (!v9x_d3d_nv50_shadow_take(shadow, draw)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        shadow->dirty = 1ul;
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
        if (state.two_units != 0ul &&
            !v9x_d3d_nv50_fp2_program(scratch, generation, &state)) {
            v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_REFUSE_PROGRAMS);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        if (state.textured != 0ul) {
            /* Entry 0, and entry 1 for unit 1 (TSC 1 by LINKED_TSC). */
            tables = state.two_units != 0ul ? 2ul : 1ul;
            for (index = 0ul; index < tables * V9X_NV50_3D_TIC_WORDS;
                 ++index) {
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
        for (index = 0ul; index < chunk * 3ul; index += 3ul) {
            /* The core no longer clips (clip_in_core 0): a triangle past
             * the guard band, or with a coordinate that is not finite, is
             * dropped and counted, as the clipper refused it. */
            if (!v9x_d3d_nv50_guard(&vertices[index])) {
                v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_DROP_GUARD);
                continue;
            }
            for (corner = index; corner < index + 3ul; ++corner) {
                const V9X_R3D_VERTEX *vertex = &vertices[corner];

                if (state.two_units != 0ul) {
                    v9x_nv50_push_d3d_uv1(&push, uv1[2ul * corner],
                                          uv1[2ul * corner + 1ul]);
                }
                v9x_nv50_push_d3d_vertex(&push, vertex->sx, vertex->sy,
                                         vertex->sz, vertex->rhw,
                                         vertex->color, vertex->specular,
                                         vertex->tu, vertex->tv,
                                         draw->centre_offset, with_specular,
                                         state.textured);
            }
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
        if (uv1 != 0) {
            uv1 += chunk * 3ul * 2ul;
        }
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return 1;
}

/*
 * PIXEL SHADERS, step 4 of docs\plans\nvidia-gt218-ps11.md: a list of
 * triangles under a DirectX 8 pixel shader (d3d_internal.h, draw_shader).
 * The program is v9x_nv50_ps11_build's (the translator V9XN5CH /3d:ps11
 * drew on the card), cached by the shader and what of the draw it depends
 * on in V9X_NV50_D3D_PS_SLOTS slots past the scratch. Every batch waits
 * for the engine to drain (v9x_nv50_3d_begin), so a slot may be reused
 * and rewritten in place; a slot's code is written once per channel
 * build, with CODE_CB_FLUSH in the push after.
 *
 * The target, depth (through the shadow target, as every Z draw), blend
 * and alpha test are the fixed-function map's; fog is the program's. The
 * wide vertices come unclipped and are drawn inside the guard band as
 * the fixed-function ones are. Not drawn yet: cube maps and the texm3x3
 * lookups (cube placement and 3D sampling are step 6), refused and
 * counted by the core as dropped.
 */
#define V9X_D3D_NV50_PS_CHUNK        1024ul
#define V9X_D3D_NV50_PS_UNWRITTEN    0xfffffffful

typedef struct v9x_d3d_nv50_ps_entry {
    const V9X_PS11_PROGRAM *program;    /* null: an empty slot */
    DWORD serial;
    DWORD projected;
    DWORD unbound;
    DWORD fog;
    DWORD registers;
    DWORD kills;
    DWORD generation;   /* the channel build its code was written in */
} V9X_D3D_NV50_PS_ENTRY;

static V9X_D3D_NV50_PS_ENTRY v9x_d3d_nv50_ps_cache[V9X_NV50_D3D_PS_SLOTS];
static DWORD v9x_d3d_nv50_ps_next = 0ul;
static v9x_u32 v9x_d3d_nv50_ps_words[V9X_NV50_D3D_PS_WORDS_MAX];
static v9x_u32 v9x_d3d_nv50_ps_constants[V9X_NV50_PS11_C_FLOATS];
/* The draw with its textures, specular and fog taken off, for the map's
 * target, depth, blend and alpha test: static, as HAL stacks are small. */
static V9X_R3D_DRAW v9x_d3d_nv50_ps_plain;

/* entry's program into v9x_d3d_nv50_ps_words; its word count, 0 when it
 * does not translate. */
static v9x_u32 v9x_d3d_nv50_ps_translate(V9X_D3D_NV50_PS_ENTRY *entry)
{
    struct v9x_nv50_ps11_desc desc;
    struct v9x_nv50_ps11_info info;

    desc.cube = 0ul;
    desc.projected = entry->projected;
    desc.unbound = entry->unbound;
    desc.slot = 0ul;    /* fragment slot 0, buffer 2 (nv50_d3d.c) */
    desc.fog = entry->fog;
    if (v9x_nv50_ps11_build(v9x_d3d_nv50_ps_words, V9X_NV50_D3D_PS_WORDS_MAX,
                            entry->program, &desc, &info) != V9X_STATUS_OK) {
        return 0ul;
    }
    entry->registers = info.registers;
    entry->kills = info.kills;
    return info.words;
}

/* The slot for this shader and draw: found, or the next one taken and its
 * program translated to prove it builds (written later, per batch). */
static V9X_D3D_NV50_PS_ENTRY *v9x_d3d_nv50_ps_entry(
    const V9X_R3D_SHADER *shader, DWORD unbound, DWORD fog, DWORD *slot)
{
    V9X_D3D_NV50_PS_ENTRY *entry;
    DWORD n;

    for (n = 0ul; n < V9X_NV50_D3D_PS_SLOTS; ++n) {
        entry = &v9x_d3d_nv50_ps_cache[n];
        if (entry->program == shader->program &&
            entry->serial == shader->serial &&
            entry->projected == shader->env.projected &&
            entry->unbound == unbound && entry->fog == fog) {
            *slot = n;
            return entry;
        }
    }
    n = v9x_d3d_nv50_ps_next;
    v9x_d3d_nv50_ps_next = (n + 1ul) % V9X_NV50_D3D_PS_SLOTS;
    entry = &v9x_d3d_nv50_ps_cache[n];
    entry->program = shader->program;
    entry->serial = shader->serial;
    entry->projected = shader->env.projected;
    entry->unbound = unbound;
    entry->fog = fog;
    entry->generation = V9X_D3D_NV50_PS_UNWRITTEN;
    if (v9x_d3d_nv50_ps_translate(entry) == 0ul) {
        entry->program = 0;
        return 0;
    }
    *slot = n;
    return entry;
}

/* A wide triangle drawn as given: as v9x_d3d_nv50_guard for its x and y. */
static int v9x_d3d_nv50_guard_wide(const float *triangle)
{
    V9X_R3D_VERTEX corners[3];
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        corners[index].sx = triangle[index * V9X_R3D_SHADER_FLOATS];
        corners[index].sy = triangle[index * V9X_R3D_SHADER_FLOATS + 1ul];
    }
    return v9x_d3d_nv50_guard(corners);
}

static int v9x_d3d_nv50_draw_shader(const V9X_R3D_DRAW *draw,
                                    const V9X_R3D_SHADER *shader,
                                    const float *vertices,
                                    DWORD triangle_count)
{
    struct v9x_nv50_d3d_state state;
    struct v9x_nv50_3d_scratch scratch_gpu;
    struct v9x_nv50_push push;
    V9X_D3D_NV50_TEXTURE none[2];
    V9X_D3D_NV50_TEXTURE resolved;
    V9X_D3D_NV50_PS_ENTRY *entry;
    V9X_D3D_NV50_SHADOW *shadow;
    V9X_D3D_NV50_SHADOW_VIEW view;
    v9x_u32 tic[V9X_NV50_D3D_SHADER_UNITS * V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_D3D_SHADER_UNITS * V9X_NV50_3D_TSC_WORDS];
    v9x_u32 with_specular;
    v9x_u32 reason;
    v9x_u32 count;
    volatile DWORD *area;
    volatile DWORD *scratch;
    volatile DWORD *at;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD sampled;
    DWORD bound = 0ul;
    DWORD stage;
    DWORD slot = 0ul;
    DWORD chunk;
    DWORD index;
    DWORD corner;
    DWORD started;
    int setup;

    if (draw == 0 || shader == 0 || shader->program == 0 || vertices == 0 ||
        triangle_count == 0ul || !v9x_d3d_nv50_ready() ||
        !v9x_d3d_nv50_count_registers()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();

    /* No cube maps and no texm3x3 lookups yet (step 6). */
    for (index = 0ul; index < shader->program->count; ++index) {
        DWORD op = shader->program->inst[index].op;

        if (op == V9X_PS11_TEXM3X3TEX || op == V9X_PS11_TEXM3X3SPEC ||
            op == V9X_PS11_TEXM3X3VSPEC) {
            v9x_d3d_nv50_count_refusal(draw,
                                       V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
    }
    sampled = shader->program->samples;
    for (stage = 0ul; stage < V9X_NV50_D3D_SHADER_UNITS; ++stage) {
        if ((sampled & (1ul << stage)) == 0ul ||
            shader->texture[stage].object == 0) {
            continue;
        }
        if ((shader->env.cube & (1ul << stage)) != 0ul) {
            v9x_d3d_nv50_count_refusal(draw,
                                       V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        v9x_d3d_nv50_resolve_texture(&shader->texture[stage], &resolved);
        reason = v9x_d3d_nv50_map_unit(&shader->texture[stage], &resolved,
                                       tic + stage * V9X_NV50_3D_TIC_WORDS,
                                       tsc + stage * V9X_NV50_3D_TSC_WORDS);
        if (reason != V9X_D3D_NV50_REFUSE_NONE) {
            v9x_d3d_nv50_count_refusal(draw, reason);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        bound |= 1ul << stage;
    }
    entry = v9x_d3d_nv50_ps_entry(shader, sampled & ~bound,
                                  draw->fog_enable != 0ul ? 1ul : 0ul,
                                  &slot);
    if (entry == 0) {
        v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_REFUSE_PROGRAMS);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }

    /* The constant block, the fog colour in its last floats. */
    v9x_nv50_ps11_constants(shader->program, &shader->env,
                            v9x_d3d_nv50_ps_constants);
    for (index = 0ul; index < 3ul; ++index) {
        union {
            float value;
            v9x_u32 bits;
        } convert;

        convert.value =
            (float)(long)((draw->fog_color >> (16u - 8u * (unsigned)index)) &
                          0xfful) / 255.0f;
        v9x_d3d_nv50_ps_constants[V9X_NV50_PS11_C_FOG + index] = convert.bits;
    }

    /* The fixed-function map for target, depth, blend and alpha test. */
    v9x_d3d_nv50_ps_plain = *draw;
    v9x_d3d_nv50_ps_plain.texture.object = 0;
    v9x_d3d_nv50_ps_plain.texture.levels = 0;
    v9x_d3d_nv50_ps_plain.texture1.object = 0;
    v9x_d3d_nv50_ps_plain.texture1.levels = 0;
    v9x_d3d_nv50_ps_plain.texcoords1 = 0;
    v9x_d3d_nv50_ps_plain.specular_enable = 0ul;
    v9x_d3d_nv50_ps_plain.fog_enable = 0ul;
    v9x_d3d_nv50_ps_plain.color_key_enable = 0ul;
    none[0].format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    none[1].format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    shadow = v9x_d3d_nv50_shadow_for(draw, &view);
    reason = v9x_d3d_nv50_map_draw(&v9x_d3d_nv50_ps_plain, none,
                                   shadow != 0 ? &view : 0,
                                   v9x_d3d_nv50_fp_registers, &state, tic,
                                   tsc, &with_specular);
    if (reason != V9X_D3D_NV50_REFUSE_NONE) {
        v9x_d3d_nv50_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    state.shader = 1ul;
    state.kills = entry->kills;
    state.fp_start = v9x_nv50_d3d_ps_start(slot);
    state.fp_registers = entry->registers;
    if (shadow != 0) {
        if (!v9x_d3d_nv50_shadow_take(shadow, draw)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        shadow->dirty = 1ul;
    }

    while (triangle_count != 0ul) {
        chunk = triangle_count > V9X_D3D_NV50_PS_CHUNK
                    ? V9X_D3D_NV50_PS_CHUNK : triangle_count;
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
        /* The program, once per channel build: the engine has drained,
         * so nothing runs the slot while it changes. */
        state.code_flush = 0ul;
        if (entry->generation != generation) {
            count = v9x_d3d_nv50_ps_translate(entry);
            if (count == 0ul) {
                v9x_d3d_nv50_count_refusal(draw,
                                           V9X_D3D_NV50_REFUSE_PROGRAMS);
                V9X_TIME_END(V9X_TIME_DECODE, started);
                return 0;
            }
            at = scratch + (V9X_NV50_3D_SCRATCH_FP +
                            v9x_nv50_d3d_ps_start(slot)) / 4ul;
            for (index = 0ul; index < count; ++index) {
                at[index] = v9x_d3d_nv50_ps_words[index];
            }
            entry->generation = generation;
            state.code_flush = 1ul;
        }
        for (stage = 0ul; stage < V9X_NV50_D3D_SHADER_UNITS; ++stage) {
            if ((bound & (1ul << stage)) == 0ul) {
                continue;
            }
            for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
                scratch[V9X_NV50_3D_SCRATCH_TIC / 4ul +
                        stage * V9X_NV50_3D_TIC_WORDS + index] =
                    tic[stage * V9X_NV50_3D_TIC_WORDS + index];
                scratch[V9X_NV50_3D_SCRATCH_TSC / 4ul +
                        stage * V9X_NV50_3D_TSC_WORDS + index] =
                    tsc[stage * V9X_NV50_3D_TSC_WORDS + index];
            }
        }

        v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
        if (setup) {
            v9x_nv50_3d_scratch_at(scratch_base, &scratch_gpu);
            v9x_nv50_push_bind_3d(&push);
            v9x_nv50_push_3d_setup(&push, &scratch_gpu);
        }
        v9x_nv50_push_d3d_state(&push, &state);
        v9x_nv50_push_d3d_constants(&push, v9x_d3d_nv50_ps_constants,
                                    V9X_NV50_PS11_C_FLOATS);
        v9x_nv50_push_d3d_begin(&push);
        for (index = 0ul; index < chunk; ++index) {
            const float *triangle =
                vertices + index * 3ul * V9X_R3D_SHADER_FLOATS;

            if (!v9x_d3d_nv50_guard_wide(triangle)) {
                v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_DROP_GUARD);
                continue;
            }
            for (corner = 0ul; corner < 3ul; ++corner) {
                v9x_nv50_push_d3d_wide(
                    &push, triangle + corner * V9X_R3D_SHADER_FLOATS,
                    draw->centre_offset);
            }
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
        vertices += chunk * 3ul * V9X_R3D_SHADER_FLOATS;
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return 1;
}

/*
 * ddhal_internal.h: a rectangle of a Z buffer this engine placed, filled
 * at value (16-bit full scale) by a draw (d3d_nv50_map.h,
 * v9x_d3d_nv50_depth_fill_draw). eng_nv50.c's fill hands every depth fill
 * here: the buffer is tiled, so a linear fill would put the value in the
 * wrong pixels. Declined for a Z buffer DirectDraw placed.
 */
int v9x_d3d_nv50_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                            DWORD value, int wait)
{
    struct v9x_nv50_d3d_state state;
    struct v9x_nv50_3d_scratch scratch_gpu;
    struct v9x_nv50_push push;
    V9X_D3D_NV50_SHADOW *shadow;
    V9X_D3D_NV50_SHADOW_VIEW view;
    V9X_R3D_VERTEX vertices[6];
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD offset;
    DWORD index;
    int setup;

    (void)wait;
    if (surface == 0 || rect == 0 || surface->lpGbl == 0 ||
        rect[0] < 0l || rect[1] < 0l || !v9x_d3d_nv50_ready() ||
        !v9x_d3d_nv50_count_registers()) {
        return V9X_BLT_DECLINED;
    }
    offset = v9x_surface_offset(surface);
    shadow = offset != 0xfffffffful ? v9x_d3d_nv50_shadow_of_z(offset) : 0;
    if (shadow == 0) {
        return V9X_BLT_DECLINED;
    }
    v9x_d3d_nv50_view_of(shadow, 1ul, &view);
    if (!v9x_d3d_nv50_depth_fill_draw(&view, (v9x_u32)rect[0],
                                      (v9x_u32)rect[1], (v9x_u32)rect[2],
                                      (v9x_u32)rect[3], value,
                                      v9x_d3d_nv50_fp_registers, &state,
                                      vertices)) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_nv50_3d_begin(&area, &area_words, &scratch, &scratch_base,
                           &generation)) {
        return V9X_BLT_DECLINED;
    }
    setup = !v9x_d3d_nv50_setup_sent ||
            generation != v9x_d3d_nv50_setup_generation;
    if (setup && !v9x_d3d_nv50_write_programs(scratch)) {
        return V9X_BLT_DECLINED;
    }
    v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
    if (setup) {
        v9x_nv50_3d_scratch_at(scratch_base, &scratch_gpu);
        v9x_nv50_push_bind_3d(&push);
        v9x_nv50_push_3d_setup(&push, &scratch_gpu);
    }
    v9x_nv50_push_d3d_state(&push, &state);
    v9x_nv50_push_d3d_begin(&push);
    for (index = 0ul; index < 6ul; ++index) {
        v9x_nv50_push_d3d_vertex(&push, vertices[index].sx,
                                 vertices[index].sy, vertices[index].sz,
                                 vertices[index].rhw, 0ul, 0ul, 0.0f, 0.0f,
                                 0.0f, 0ul, 0ul);
    }
    v9x_nv50_push_d3d_end(&push);
    if (push.overflow != 0ul || !v9x_nv50_3d_submit(push.count)) {
        return V9X_BLT_DECLINED;
    }
    if (setup) {
        v9x_d3d_nv50_setup_generation = generation;
        v9x_d3d_nv50_setup_sent = 1;
    }
    return V9X_BLT_DONE;
}

/*
 * A Z16 buffer, tiled, and its colour shadow in one heap block: the Z,
 * its rows on 64 bytes and 8-row blocks (v9x_d3d_nv50_tiled_layout),
 * from a row-block unit boundary, then from the next unit boundary past
 * it the colour at 4 bytes a pixel, which also holds a 16 bpp target
 * (nv50_rblock.h). Declined, touching nothing, when every shadow is in
 * use or the unit is not known; DirectDraw then places it linear and its
 * Z-tested draws are refused.
 */
static DWORD v9x_d3d_nv50_create_depth(V9X_DDHAL_CREATESURFACEDATA *data,
                                       V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_NV50_SHADOW *shadow = 0;
    v9x_u32 offsets[1];
    v9x_u32 z_pitch;
    v9x_u32 colour_pitch;
    v9x_u32 rows;
    v9x_u32 z_offset;
    v9x_u32 colour_offset;
    DWORD z_bytes;
    DWORD colour_bytes;
    DWORD span;
    DWORD unit = v9x_nv50_rblock_unit_now();
    DWORD base = 0ul;
    DWORD width = (DWORD)surface->lpGbl->wWidth;
    DWORD height = (DWORD)surface->lpGbl->wHeight;
    DWORD index;

    if (data->dwSCnt != 1ul ||
        v9x_d3d_depth_bytes_per_pixel(surface) != 2ul || unit == 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    for (index = 0ul; index < V9X_D3D_NV50_SHADOWS_MAX; ++index) {
        if (v9x_d3d_nv50_shadows[index].used == 0ul) {
            shadow = &v9x_d3d_nv50_shadows[index];
            break;
        }
    }
    if (shadow == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    v9x_d3d_nv50_tiled_layout(width, height, 2ul, &z_pitch, &rows);
    v9x_d3d_nv50_tiled_layout(width, height, 4ul, &colour_pitch, &rows);
    z_bytes = z_pitch * rows;
    colour_bytes = colour_pitch * rows;
    span = v9x_nv50_rblock_span(unit, V9X_D3D_NV50_HEAP_ALIGN, z_bytes,
                                colour_bytes);
    if (span == 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    offsets[0] = 0ul;
    /* The heap places on 4 KiB at most (its mapping's alignment: asked
     * for 64 KiB, every Z was declined and Quake 3's draws refused,
     * W9QI0DAD boot 33), and a row-block unit need not be a power of two
     * (192 KiB on W9QI0DAD), so the block is asked on 4 KiB with a unit
     * to spare and the Z moved up to the next unit boundary. The block's
     * start stays in dwReserved1 for the release. */
    if (v9x_d3d_place_block(data, V9X_D3D_NV50_HEAP_ALIGN,
                            V9X_D3D_NV50_HEAP_ALIGN,
                            span / V9X_D3D_NV50_HEAP_ALIGN, offsets,
                            &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* A block of span bytes on 4 KiB always holds the pair (the span's
     * own definition; test_nv50_init.c places the worst start). */
    (void)v9x_nv50_rblock_place(base, span, unit, z_bytes, colour_bytes,
                                &z_offset, &colour_offset);
    surface->lpGbl->fpVidMem += z_offset - base;
    surface->lpGbl->lPitch = (LONG)z_pitch;
    shadow->used = 1ul;
    shadow->z_offset = z_offset;
    shadow->width = width;
    shadow->height = height;
    shadow->colour_offset = colour_offset;
    shadow->owner = V9X_D3D_NV50_NONE;
    shadow->dirty = 0ul;
    shadow->release_window =
        surface->lpGbl->fpVidMem - surface->lpGbl->dwReserved1 + 1ul;
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
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

    if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        return v9x_d3d_nv50_create_depth(data, surface);
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

/*
 * A surface going. A Z buffer's shadow is flushed to the target it holds
 * and freed with the block; a target a shadow holds is let go without a
 * copy, since its memory is no longer the application's.
 */
static void v9x_d3d_nv50_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    const V9X_DD_SURFACE_LCL *surface =
        data != 0 ? (const V9X_DD_SURFACE_LCL *)data->lpDDSurface : 0;
    V9X_D3D_NV50_SHADOW *shadow;
    DWORD offset;

    if (surface != 0 && surface->lpGbl != 0 &&
        surface->lpGbl->fpVidMem != 0ul) {
        offset = v9x_surface_offset(surface);
        if (offset != 0xfffffffful) {
            v9x_d3d_nv50_release_target(offset, 0);
            shadow = v9x_d3d_nv50_shadow_of_z(offset);
            if (shadow != 0) {
                (void)v9x_d3d_nv50_shadow_flush(shadow);
                shadow->used = 0ul;
                (void)v9x_d3d_place_release(data, shadow->release_window);
                return;
            }
        }
    }
    (void)v9x_d3d_place_release(data, V9X_D3D_NV50_SURFACE_ALIGN);
}

/* A Blt is about to write the surface, or an Unlock has: any drawing a
 * shadow holds for it goes back first, then no shadow holds it. */
static void v9x_d3d_nv50_surface_written(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD offset;

    if (surface == 0 || surface->lpGbl == 0) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset != 0xfffffffful) {
        v9x_d3d_nv50_release_target(offset, 1);
    }
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
    v9x_d3d_nv50_accepts,
    0,                                  /* draw_indexed */
    0,                                  /* draw_tnl */
    0,                                  /* tnl_available */
    0,                                  /* lock_surface */
    0,                                  /* unlock_surface */
    v9x_d3d_nv50_draw_shader,           /* draw_shader: step 4 */
    v9x_d3d_nv50_surface_written
};
