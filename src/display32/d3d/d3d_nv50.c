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
#include "d3d_caps8.h"
#include "d3d_nv50_map.h"
#include "velocity9x/nv50_d3d.h"
#include "velocity9x/nv50_ps11.h"
#include "velocity9x/nv50_rblock.h"
#include "velocity9x/gt218_reserve.h"
#include "velocity9x/nvidia.h"

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
 * every triangle and moved its vertices for one stage. DXT1/3/5 from the
 * pool's tiled copies (step 7). */
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
    1ul,            /* guard_band_ddi6 */
    1ul             /* dxt_textures */
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

/* DDPF_BUMPDUDV (ddraw.h), DirectX 7's flag for a du/dv bump format. */
#define V9X_D3D_NV50_DDPF_BUMPDUDV   0x00080000ul

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
    /* Q8W8V8U8 and V8U8, which the core publishes where pixel shaders run
     * and only a shader stage reads: known by D3DFORMAT under
     * DDPF_D3DFORMAT or DDPF_FOURCC, and V8U8 also by DirectX 7's
     * DDPF_BUMPDUDV with U in the low byte (v9x_d3d_vi_signed_format's
     * rules). DirectDraw cannot size Q8W8V8U8, which the runtime describes
     * with no bit count, so the engine places both. */
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        DWORD d3d = 0ul;

        if ((format->dwFlags & (V9X_DDPF_D3DFORMAT | V9X_DDPF_FOURCC)) !=
            0ul) {
            d3d = format->dwFourCC;
        } else if ((format->dwFlags & V9X_D3D_NV50_DDPF_BUMPDUDV) != 0ul &&
                   format->dwRGBBitCount == 16ul &&
                   format->dwRBitMask == 0x00fful &&
                   format->dwGBitMask == 0xff00ul &&
                   format->dwBBitMask == 0ul) {
            d3d = V9X_D3DFMT_V8U8;
        }
        if (d3d == V9X_D3DFMT_Q8W8V8U8) {
            *bytes_out = 4ul;
            return V9X_NV50_D3D_TEX_Q8W8V8U8;
        }
        if (d3d == V9X_D3DFMT_V8U8) {
            *bytes_out = 2ul;
            return V9X_NV50_D3D_TEX_V8U8;
        }
        /* DXT, bytes a 4 x 4 block's. The premultiplied DXT2 and DXT4 are
         * not published. */
        if (d3d == V9X_D3DFMT_DXT1) {
            *bytes_out = 8ul;
            return V9X_NV50_D3D_TEX_DXT1;
        }
        if (d3d == V9X_D3DFMT_DXT3) {
            *bytes_out = 16ul;
            return V9X_NV50_D3D_TEX_DXT3;
        }
        if (d3d == V9X_D3DFMT_DXT5) {
            *bytes_out = 16ul;
            return V9X_NV50_D3D_TEX_DXT5;
        }
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

/* A mipmapped texture's pooled chain, brought up to date, into texture's
 * mip fields (the mip section below). */
static void v9x_d3d_nv50_resolve_mip(DWORD offset,
                                     V9X_D3D_NV50_TEXTURE *texture);

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
    texture->cube_address = 0ul;
    texture->cube_levels = 0ul;
    texture->cube_tile_mode = 0ul;
    texture->mip_address = 0ul;
    texture->mip_levels = 0ul;
    texture->mip_tile_mode = 0ul;
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
    if (texture->format != V9X_D3D_NV50_TEXTURE_UNKNOWN) {
        v9x_d3d_nv50_resolve_mip(offset, texture);
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

/*
 * Z16 needs the NVA0 3D class or later: Mesa's nv50_screen.c refuses
 * PIPE_FORMAT_Z16_UNORM below it, and the G96's NV84_3D raised a PGRAPH
 * ERROR on its first Z16 depth setup (ZETA_HORIZ, M-Z2T02D boot 12). On
 * that chip the Z is kept as Z24S8, four bytes a pixel, through the
 * kind-28h window the channel already maps. The application still sees
 * a 16-bit Z buffer; it is tiled, so not readable as one either way.
 */
static int v9x_d3d_nv50_z24(void)
{
    return v9x_hal->engine.pci_device ==
           (DWORD)V9X_PCI_DEVICE_G96_GEFORCE9500GT;
}

/* The shadow as the map and the depth fill take it: the colour through
 * the kind-70h window, the Z through the Z16 one (Z24S8 on the G96),
 * both tiled. */
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
    view->zeta.address = (v9x_d3d_nv50_z24() ? V9X_NV50_VM_Z24S8 :
                                               V9X_NV50_VM_Z16) +
                         shadow->z_offset;
    view->zeta.pitch = 0ul;
    view->zeta.width = shadow->width;
    view->zeta.height = shadow->height;
    view->zeta.tile_mode = V9X_D3D_NV50_TILE_MODE;
    view->zeta.tiled = 1ul;
    view->zeta_format = v9x_d3d_nv50_z24() ? V9X_NV50_3D_ZETA_Z24S8 :
                                             V9X_NV50_3D_ZETA_Z16;
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

/*
 * Cube maps (step 6 of docs\plans\nvidia-gt218-ps11.md). The sampler
 * reads a cube only block-linear, so a cube's surfaces are placed linear,
 * as every texture here, for the runtime's Locks and Blts, and beside them
 * in the same heap block a tiled copy holds the six faces as the layers
 * the cube TIC samples, each layer a face's whole chain (nv50_d3d.h). Each
 * part is in row-block units of its own (nv50_rblock.h), as a Z buffer and
 * its colour shadow are. The copy is brought up to date before a draw
 * samples it, after the cube was created or any of its surfaces written
 * (surface_written). DXT cubes go the same way in 4 x 4 blocks, as the
 * pool's DXT copies do.
 *
 * The cube is known by its +X top's offset, in a table of
 * V9X_D3D_NV50_CUBES; HAL statics are shared by every process, as video
 * memory is.
 */
#define V9X_D3D_NV50_CUBES           64ul
#define V9X_D3D_NV50_COPY_CELL       4ul    /* A8R8G8B8's bytes */

typedef struct v9x_d3d_nv50_cube {
    DWORD base;                 /* +X top's offset; 0 for a free slot */
    DWORD linear_end;           /* past the linear part's last byte */
    DWORD side;
    DWORD bytes;                /* a cell's: a texel's, or a DXT block's */
    DWORD edge;                 /* a cell's side in texels: 1, or 4 */
    DWORD levels;
    /* Per level: +X's linear offset, the step to the next face, its row
     * pitch; its offset in a tiled layer and its tile mode. */
    DWORD linear_level[V9X_NV50_D3D_MIP_LEVELS_MAX];
    DWORD linear_slice[V9X_NV50_D3D_MIP_LEVELS_MAX];
    DWORD linear_pitch[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 tiled_level[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 modes[V9X_NV50_D3D_MIP_LEVELS_MAX];
    DWORD tiled_offset;
    DWORD layer_bytes;
    DWORD dirty;
    DWORD release_window;       /* v9x_d3d_place_release's, as a Z's */
} V9X_D3D_NV50_CUBE;

/* A level of width x height texels in the 2D engine's copy pixels: its
 * texels, or for DXT (edge 4) its rows of blocks, each row's bytes as
 * A8R8G8B8 pixels - tiling being a matter of bytes and rows. */
static void v9x_d3d_nv50_copy_cells(DWORD width, DWORD height, DWORD edge,
                                    DWORD bytes, DWORD *across, DWORD *down)
{
    *across = width;
    *down = height;
    if (edge > 1ul) {
        *across = (width + edge - 1ul) / edge * bytes /
                  V9X_D3D_NV50_COPY_CELL;
        *down = (height + edge - 1ul) / edge;
    }
}

/* The 502D format a copy of bytes-sized cells runs in: R5G6B5 for 16-bit
 * texels, A8R8G8B8 for 32-bit ones and DXT blocks, the bytes kept
 * whatever the texture's own format. */
static DWORD v9x_d3d_nv50_copy_format(DWORD bytes)
{
    return bytes == 2ul ? V9X_502D_FORMAT_R5G6B5 : V9X_502D_FORMAT_A8R8G8B8;
}

static V9X_D3D_NV50_CUBE v9x_d3d_nv50_cubes[V9X_D3D_NV50_CUBES];

/* The placed cube whose +X top is at offset, or null. */
static V9X_D3D_NV50_CUBE *v9x_d3d_nv50_cube_at(DWORD offset)
{
    DWORD n;

    for (n = 0ul; offset != 0ul && n < V9X_D3D_NV50_CUBES; ++n) {
        if (v9x_d3d_nv50_cubes[n].base == offset) {
            return &v9x_d3d_nv50_cubes[n];
        }
    }
    return 0;
}

/* A write at offset: the cube whose linear part holds it needs a new
 * tiled copy before its next draw. */
static void v9x_d3d_nv50_cube_written(DWORD offset)
{
    DWORD n;

    for (n = 0ul; n < V9X_D3D_NV50_CUBES; ++n) {
        V9X_D3D_NV50_CUBE *cube = &v9x_d3d_nv50_cubes[n];

        if (cube->base != 0ul && offset >= cube->base &&
            offset < cube->linear_end) {
            cube->dirty = 1ul;
        }
    }
}

/*
 * Every face's every level from its linear surface into its place in the
 * tiled layers, one 502D copy each, in one kick after everything before it
 * (as a shadow's load).
 */
static int v9x_d3d_nv50_cube_copy(V9X_D3D_NV50_CUBE *cube)
{
    struct v9x_nv50_surface linear;
    struct v9x_nv50_surface tiled;
    struct v9x_nv50_push push;
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD face;
    DWORD level;
    DWORD edge;
    DWORD across;
    DWORD down;
    DWORD format = v9x_d3d_nv50_copy_format(cube->bytes);
    int ok;

    if (cube->dirty == 0ul) {
        return 1;
    }
    if (!v9x_nv50_3d_begin(&area, &area_words, &scratch, &scratch_base,
                           &generation)) {
        return 0;
    }
    v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
    for (face = 0ul; face < V9X_NV50_D3D_CUBE_FACES; ++face) {
        edge = cube->side;
        for (level = 0ul; level < cube->levels; ++level) {
            v9x_d3d_nv50_copy_cells(edge, edge, cube->edge, cube->bytes,
                                    &across, &down);
            linear.address = cube->linear_level[level] +
                             face * cube->linear_slice[level];
            linear.pitch = cube->linear_pitch[level];
            linear.width = across;
            linear.height = down;
            linear.tile_mode = 0ul;
            linear.tiled = 0ul;
            tiled.address = V9X_NV50_VM_TILED + cube->tiled_offset +
                            face * cube->layer_bytes +
                            cube->tiled_level[level];
            tiled.pitch = 0ul;
            tiled.width = across;
            tiled.height = down;
            tiled.tile_mode = cube->modes[level];
            tiled.tiled = 1ul;
            v9x_nv50_push_2d_surface_blit(&push, &linear, &tiled, format,
                                          0ul, 0ul, 0ul, 0ul, across, down);
            edge = edge > 1ul ? edge >> 1 : 1ul;
        }
    }
    if (push.overflow != 0ul) {
        return 0;
    }
    v9x_d3d_nv50_resolving = 1;
    ok = v9x_nv50_3d_submit(push.count);
    v9x_d3d_nv50_resolving = 0;
    if (ok) {
        cube->dirty = 0ul;
    }
    return ok;
}

/*
 * Mipmaps (docs\plans\nvidia-gt218-mipmaps.md). A pitch TIC has no
 * levels, so a mipmapped texture's whole chain is copied into a tiled
 * layout (v9x_nv50_d3d_mip_layout) in the texture pool, and the sampler
 * reads that. The pool is the 32 MiB the 16-bit side leaves between the
 * heap's end and the engine reserve (gt218_reserve.h). Only its whole
 * row-block units are used, so the tiled kind shares no row block with
 * the linear heap below or the channel above. Blocks are taken first-fit
 * on the 512-byte tile and given back when the texture goes. A texture
 * that finds no room keeps sampling level 0, as before.
 *
 * Each record is known by its level 0's offset; its levels' linear
 * surfaces stay where DirectDraw's runtime writes them, and any write to
 * one of them marks the copy stale (surface_written).
 *
 * DXT (step 7 of docs\plans\nvidia-gt218-ps11.md) goes the same way, one
 * level or a chain: Mesa's nv50 lays compressed textures out only
 * block-linear, so the sampler is never pointed at a linear DXT level.
 * Whether it could read one pitch-linear was not measured. Its cells are
 * 4 x 4 blocks; the 2D engine copies a row of blocks as A8R8G8B8 pixels
 * of the same bytes, tiling being a matter of bytes and rows. A DXT
 * texture with no record is not drawn.
 */
#define V9X_D3D_NV50_MIPS            512ul
#define V9X_D3D_NV50_POOL_ALIGN      512ul

typedef struct v9x_d3d_nv50_mip {
    DWORD base;                 /* level 0's offset; 0 for a free slot */
    DWORD levels;
    DWORD width;
    DWORD height;
    DWORD bytes;                /* a cell's: a texel's, or a DXT block's */
    DWORD edge;                 /* a cell's side in texels: 1, or 4 */
    DWORD linear[V9X_NV50_D3D_MIP_LEVELS_MAX];      /* each level's offset */
    DWORD linear_pitch[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 offsets[V9X_NV50_D3D_MIP_LEVELS_MAX];   /* in the tiled chain */
    v9x_u32 modes[V9X_NV50_D3D_MIP_LEVELS_MAX];
    DWORD pool_offset;          /* the chain's offset in video memory */
    DWORD pool_bytes;
    DWORD dirty;
} V9X_D3D_NV50_MIP;

static V9X_D3D_NV50_MIP v9x_d3d_nv50_mips[V9X_D3D_NV50_MIPS];
/* The pool's usable range as last worked out; a different heap end or
 * reserve (a new mapping) forgets every record. */
static DWORD v9x_d3d_nv50_pool_start;
static DWORD v9x_d3d_nv50_pool_end;

/* The pool's whole row-block units between the heap's end and the
 * reserve, or 0 when there is no pool. */
static int v9x_d3d_nv50_pool_range(DWORD *start, DWORD *end)
{
    DWORD unit = v9x_nv50_rblock_unit_now();
    DWORD heap_end = v9x_hal->fb.vram_bytes;
    DWORD reserve = v9x_hal->engine.reserve_offset;
    DWORD index;

    if (unit == 0ul || reserve <= heap_end ||
        reserve - heap_end < V9X_GT218_POOL_BYTES) {
        return 0;
    }
    *start = (heap_end + unit - 1ul) / unit * unit;
    *end = reserve / unit * unit;
    if (*end <= *start) {
        return 0;
    }
    if (*start != v9x_d3d_nv50_pool_start ||
        *end != v9x_d3d_nv50_pool_end) {
        for (index = 0ul; index < V9X_D3D_NV50_MIPS; ++index) {
            v9x_d3d_nv50_mips[index].base = 0ul;
        }
        v9x_d3d_nv50_pool_start = *start;
        v9x_d3d_nv50_pool_end = *end;
        v9x_hal->d3d_diagnostics.nv50_pool_size = *end - *start;
        v9x_hal->d3d_diagnostics.nv50_pool_used = 0ul;
        v9x_hal->d3d_diagnostics.nv50_pool_peak = 0ul;
    }
    return 1;
}

/* bytes from the pool, first fit on the tile: the lowest gap between the
 * blocks the records hold. 0 when nothing fits. */
static DWORD v9x_d3d_nv50_pool_take(DWORD bytes)
{
    DWORD start;
    DWORD end;
    DWORD candidate;
    DWORD index;
    int moved;

    if (!v9x_d3d_nv50_pool_range(&start, &end)) {
        return 0ul;
    }
    bytes = (bytes + V9X_D3D_NV50_POOL_ALIGN - 1ul) &
            ~(V9X_D3D_NV50_POOL_ALIGN - 1ul);
    candidate = start;
    do {
        moved = 0;
        for (index = 0ul; index < V9X_D3D_NV50_MIPS; ++index) {
            const V9X_D3D_NV50_MIP *mip = &v9x_d3d_nv50_mips[index];

            if (mip->base != 0ul && candidate < mip->pool_offset +
                                                    mip->pool_bytes &&
                mip->pool_offset < candidate + bytes) {
                candidate = mip->pool_offset + mip->pool_bytes;
                moved = 1;
            }
        }
    } while (moved && candidate + bytes <= end);
    if (candidate + bytes > end) {
        return 0ul;
    }
    return candidate;
}

/* The record whose level 0 is at offset, or null. */
static V9X_D3D_NV50_MIP *v9x_d3d_nv50_mip_at(DWORD offset)
{
    DWORD n;

    for (n = 0ul; offset != 0ul && n < V9X_D3D_NV50_MIPS; ++n) {
        if (v9x_d3d_nv50_mips[n].base == offset) {
            return &v9x_d3d_nv50_mips[n];
        }
    }
    return 0;
}

/* A write at offset: the record holding a level there needs a new copy. */
static void v9x_d3d_nv50_mip_written(DWORD offset)
{
    DWORD n;
    DWORD level;

    for (n = 0ul; n < V9X_D3D_NV50_MIPS; ++n) {
        V9X_D3D_NV50_MIP *mip = &v9x_d3d_nv50_mips[n];

        if (mip->base == 0ul) {
            continue;
        }
        for (level = 0ul; level < mip->levels; ++level) {
            if (mip->linear[level] == offset) {
                mip->dirty = 1ul;
            }
        }
    }
}

/* Each level from its linear surface into its place in the chain, one
 * 502D copy each in its own tile mode, in one kick (as the cubes). */
static int v9x_d3d_nv50_mip_copy(V9X_D3D_NV50_MIP *mip)
{
    struct v9x_nv50_surface linear;
    struct v9x_nv50_surface tiled;
    struct v9x_nv50_push push;
    volatile DWORD *area;
    volatile DWORD *scratch;
    DWORD area_words;
    DWORD scratch_base;
    DWORD generation;
    DWORD level;
    DWORD width = mip->width;
    DWORD height = mip->height;
    DWORD across;
    DWORD down;
    DWORD format = v9x_d3d_nv50_copy_format(mip->bytes);
    int ok;

    if (mip->dirty == 0ul) {
        return 1;
    }
    if (!v9x_nv50_3d_begin(&area, &area_words, &scratch, &scratch_base,
                           &generation)) {
        return 0;
    }
    v9x_nv50_push_init(&push, (v9x_u32 *)area, area_words);
    for (level = 0ul; level < mip->levels; ++level) {
        v9x_d3d_nv50_copy_cells(width, height, mip->edge, mip->bytes,
                                &across, &down);
        linear.address = mip->linear[level];
        linear.pitch = mip->linear_pitch[level];
        linear.width = across;
        linear.height = down;
        linear.tile_mode = 0ul;
        linear.tiled = 0ul;
        tiled.address = V9X_NV50_VM_TILED + mip->pool_offset +
                        mip->offsets[level];
        tiled.pitch = 0ul;
        tiled.width = across;
        tiled.height = down;
        tiled.tile_mode = mip->modes[level];
        tiled.tiled = 1ul;
        v9x_nv50_push_2d_surface_blit(&push, &linear, &tiled, format, 0ul,
                                      0ul, 0ul, 0ul, across, down);
        width = width > 1ul ? width >> 1 : 1ul;
        height = height > 1ul ? height >> 1 : 1ul;
    }
    if (push.overflow != 0ul) {
        return 0;
    }
    v9x_d3d_nv50_resolving = 1;
    ok = v9x_nv50_3d_submit(push.count);
    v9x_d3d_nv50_resolving = 0;
    if (ok) {
        mip->dirty = 0ul;
    }
    return ok;
}

static void v9x_d3d_nv50_resolve_mip(DWORD offset,
                                     V9X_D3D_NV50_TEXTURE *texture)
{
    V9X_D3D_NV50_MIP *mip = v9x_d3d_nv50_mip_at(offset);

    /* A copy that cannot be made leaves the texture on level 0. */
    if (mip == 0 || !v9x_d3d_nv50_mip_copy(mip)) {
        return;
    }
    texture->mip_address = V9X_NV50_VM_TILED + mip->pool_offset;
    texture->mip_levels = mip->levels;
    texture->mip_tile_mode = mip->modes[0];
}

/*
 * A mipmapped chain, or any DXT texture, just placed level by level (each
 * level's surface at its own offset and pitch): a record and a block of
 * the pool for its tiled copy, stale until the first draw copies it.
 * Nothing when a texel chain has one level, there is no pool or no room,
 * or every record is taken: the texture then samples level 0, or for DXT
 * is not drawn. bytes and edge are a cell's (the record's).
 */
static void v9x_d3d_nv50_mip_create(V9X_DDHAL_CREATESURFACEDATA *data,
                                    DWORD bytes, DWORD edge)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    V9X_D3D_NV50_MIP *slot = 0;
    v9x_u32 pitches[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 rows[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 total;
    DWORD levels = data->dwSCnt;
    DWORD offset;
    DWORD index;
    int laid;

    if (levels < (edge > 1ul ? 1ul : 2ul) ||
        levels > V9X_NV50_D3D_MIP_LEVELS_MAX) {
        return;
    }
    for (index = 0ul; index < V9X_D3D_NV50_MIPS; ++index) {
        if (v9x_d3d_nv50_mips[index].base == 0ul) {
            slot = &v9x_d3d_nv50_mips[index];
            break;
        }
    }
    if (slot == 0) {
        ++v9x_hal->d3d_diagnostics.nv50_pool_refused;
        return;
    }
    if (edge > 1ul) {
        laid = v9x_nv50_d3d_dxt_layout((v9x_u32)list[0]->lpGbl->wWidth,
                                       (v9x_u32)list[0]->lpGbl->wHeight,
                                       levels, bytes, slot->offsets, pitches,
                                       rows, slot->modes, &total);
    } else {
        laid = v9x_nv50_d3d_mip_layout((v9x_u32)list[0]->lpGbl->wWidth,
                                       (v9x_u32)list[0]->lpGbl->wHeight,
                                       levels, bytes, slot->offsets, pitches,
                                       rows, slot->modes, &total);
    }
    if (!laid) {
        ++v9x_hal->d3d_diagnostics.nv50_pool_refused;
        return;
    }
    offset = v9x_d3d_nv50_pool_take(total);
    if (offset == 0ul) {
        ++v9x_hal->d3d_diagnostics.nv50_pool_refused;
        return;
    }
    for (index = 0ul; index < levels; ++index) {
        slot->linear[index] = v9x_surface_offset(list[index]);
        slot->linear_pitch[index] = (DWORD)list[index]->lpGbl->lPitch;
    }
    slot->levels = levels;
    slot->width = (DWORD)list[0]->lpGbl->wWidth;
    slot->height = (DWORD)list[0]->lpGbl->wHeight;
    slot->bytes = bytes;
    slot->edge = edge;
    slot->pool_offset = offset;
    slot->pool_bytes = (total + V9X_D3D_NV50_POOL_ALIGN - 1ul) &
                       ~(V9X_D3D_NV50_POOL_ALIGN - 1ul);
    slot->dirty = 1ul;
    slot->base = slot->linear[0];
    v9x_hal->d3d_diagnostics.nv50_pool_used += slot->pool_bytes;
    if (v9x_hal->d3d_diagnostics.nv50_pool_used >
        v9x_hal->d3d_diagnostics.nv50_pool_peak) {
        v9x_hal->d3d_diagnostics.nv50_pool_peak =
            v9x_hal->d3d_diagnostics.nv50_pool_used;
    }
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
 * the fixed-function ones are. Cube maps sample the tiled copy the HAL
 * keeps of each cube (v9x_d3d_nv50_cube_copy); a texm3x3 lookup into
 * anything but a cube would be a 3D fetch and is refused, counted by the
 * core as dropped.
 */
#define V9X_D3D_NV50_PS_CHUNK        1024ul
#define V9X_D3D_NV50_PS_UNWRITTEN    0xfffffffful

typedef struct v9x_d3d_nv50_ps_entry {
    const V9X_PS11_PROGRAM *program;    /* null: an empty slot */
    DWORD serial;
    DWORD projected;
    DWORD unbound;
    DWORD fog;
    DWORD cubes;        /* the stages sampled as cubes */
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

    desc.cube = entry->cubes;
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
    const V9X_R3D_SHADER *shader, DWORD unbound, DWORD fog, DWORD cubes,
    DWORD *slot)
{
    V9X_D3D_NV50_PS_ENTRY *entry;
    DWORD n;

    for (n = 0ul; n < V9X_NV50_D3D_PS_SLOTS; ++n) {
        entry = &v9x_d3d_nv50_ps_cache[n];
        if (entry->program == shader->program &&
            entry->serial == shader->serial &&
            entry->projected == shader->env.projected &&
            entry->unbound == unbound && entry->fog == fog &&
            entry->cubes == cubes) {
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
    entry->cubes = cubes;
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
    DWORD cubes = 0ul;
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

    sampled = shader->program->samples;
    for (stage = 0ul; stage < V9X_NV50_D3D_SHADER_UNITS; ++stage) {
        V9X_D3D_NV50_CUBE *cube;

        if ((sampled & (1ul << stage)) == 0ul ||
            shader->texture[stage].object == 0) {
            continue;
        }
        v9x_d3d_nv50_resolve_texture(&shader->texture[stage], &resolved);
        /* A cube this engine placed (the core marks none: the engine
         * knows a cube by where it put it) samples its tiled copy, brought
         * up to date first. */
        cube = resolved.format != V9X_D3D_NV50_TEXTURE_UNKNOWN
                   ? v9x_d3d_nv50_cube_at(resolved.offset) : 0;
        if (cube != 0) {
            if (!v9x_d3d_nv50_cube_copy(cube)) {
                v9x_d3d_nv50_count_refusal(draw, V9X_D3D_NV50_REFUSE_STATE);
                V9X_TIME_END(V9X_TIME_DECODE, started);
                return 0;
            }
            resolved.cube_address = V9X_NV50_VM_TILED + cube->tiled_offset;
            resolved.cube_levels = cube->levels;
            resolved.cube_tile_mode = cube->modes[0];
            cubes |= 1ul << stage;
        }
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
    /* A texm3x3 lookup into a cube is the translator's cube fetch; into
     * anything else it would be a 3D one, which has no placement here. */
    for (index = 0ul; index < shader->program->count; ++index) {
        const V9X_PS11_INST *inst = &shader->program->inst[index];

        if ((inst->op == V9X_PS11_TEXM3X3TEX ||
             inst->op == V9X_PS11_TEXM3X3SPEC ||
             inst->op == V9X_PS11_TEXM3X3VSPEC) &&
            (inst->dst_reg >= V9X_NV50_D3D_SHADER_UNITS ||
             (cubes & (1ul << inst->dst_reg)) == 0ul)) {
            v9x_d3d_nv50_count_refusal(draw,
                                       V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
    }
    entry = v9x_d3d_nv50_ps_entry(shader, sampled & ~bound,
                                  draw->fog_enable != 0ul ? 1ul : 0ul,
                                  cubes, &slot);
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
    v9x_d3d_nv50_tiled_layout(width, height, v9x_d3d_nv50_z24() ? 4ul : 2ul,
                              &z_pitch, &rows);
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

/* A surface's DDSCAPS2, from its DDRAWI_DDRAWSURFACE_MORE; the runtime's
 * own halves are live while CreateSurface runs (as d3d_vi.c reads it). */
static DWORD v9x_d3d_nv50_caps2(const V9X_DD_SURFACE_LCL *surface)
{
    const BYTE *more;

    if (surface->lpSurfMore == 0ul) {
        return 0ul;
    }
    more = (const BYTE *)surface->lpSurfMore;
    if (IsBadReadPtr(more, V9X_DD_SURFACE_MORE_CAPS2 + 4ul)) {
        return 0ul;
    }
    return *(const DWORD *)(more + V9X_DD_SURFACE_MORE_CAPS2);
}

/* The face a surface's DDSCAPS2 names, 0-5 from +X, or 6 for none or
 * more than one. */
static DWORD v9x_d3d_nv50_cube_face(DWORD caps2)
{
    DWORD bits = caps2 & V9X_DDSCAPS2_CUBEMAP_ALLFACES;
    DWORD face;

    for (face = 0ul; face < V9X_NV50_D3D_CUBE_FACES; ++face) {
        if (bits == (0x400ul << face)) {
            return face;
        }
    }
    return V9X_NV50_D3D_CUBE_FACES;
}

/*
 * A video-memory cube: the runtime's list of six faces, each face's top
 * and then its levels, each naming its face in DDSCAPS2 (the HD 4600's
 * boot 131 record), placed as one heap block. The linear part holds every
 * surface level-major (all six faces of a level, then the next level),
 * each level's rows on 64 bytes and each face on 256 as every texture
 * here; the tiled part holds level 0's six layers. Each part starts on a
 * row-block unit of its own (v9x_nv50_rblock_place). Declined, touching
 * nothing, for a list it cannot account for, every slot taken or the unit
 * unknown; DirectDraw then places it and its draws sample nothing usable,
 * as before this step.
 */
static DWORD v9x_d3d_nv50_create_cube(V9X_DDHAL_CREATESURFACEDATA *data,
                                      DWORD bytes, DWORD cell_edge)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    v9x_u32 level_offsets[V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 slices[V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 level_pitches[V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 offsets[V9X_NV50_D3D_CUBE_FACES * V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 pitches[V9X_NV50_D3D_CUBE_FACES * V9X_D3D_NV50_LEVELS_MAX];
    v9x_u32 tiled_offsets[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 tiled_pitches[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 tiled_rows[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 tiled_modes[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 layer_bytes;
    v9x_u32 linear_offset;
    v9x_u32 tiled_offset;
    DWORD seen[V9X_NV50_D3D_CUBE_FACES];
    V9X_D3D_NV50_CUBE *slot = 0;
    DWORD unit = v9x_nv50_rblock_unit_now();
    DWORD side = (DWORD)list[0]->lpGbl->wWidth;
    DWORD levels = data->dwSCnt / V9X_NV50_D3D_CUBE_FACES;
    DWORD linear_bytes = 0ul;
    DWORD tiled_bytes;
    DWORD span;
    DWORD base;
    DWORD index;
    DWORD face;
    DWORD level;

    if (data->dwSCnt % V9X_NV50_D3D_CUBE_FACES != 0ul || levels == 0ul ||
        levels > V9X_D3D_NV50_LEVELS_MAX ||
        levels > V9X_NV50_D3D_MIP_LEVELS_MAX || unit == 0ul ||
        side != (DWORD)list[0]->lpGbl->wHeight ||
        side > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
        !v9x_nv50_d3d_cube_layout(side, levels, bytes, cell_edge,
                                  tiled_offsets, tiled_pitches, tiled_rows,
                                  tiled_modes, &layer_bytes)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* Each level's rows in cells (texels, or DXT blocks), on 64 bytes;
     * each face on 256. */
    for (level = 0ul; level < levels; ++level) {
        DWORD edge = (side >> level) != 0ul ? side >> level : 1ul;
        DWORD across = (edge + cell_edge - 1ul) / cell_edge;
        DWORD down = across;

        level_pitches[level] = (across * bytes + V9X_D3D_NV50_PITCH_ALIGN -
                                1ul) & ~(V9X_D3D_NV50_PITCH_ALIGN - 1ul);
        slices[level] = (level_pitches[level] * down +
                         V9X_D3D_NV50_SURFACE_ALIGN - 1ul) &
                        ~(V9X_D3D_NV50_SURFACE_ALIGN - 1ul);
        level_offsets[level] = linear_bytes;
        linear_bytes += slices[level] * V9X_NV50_D3D_CUBE_FACES;
    }
    tiled_bytes = layer_bytes * V9X_NV50_D3D_CUBE_FACES;
    for (index = 0ul; index < V9X_D3D_NV50_CUBES; ++index) {
        if (v9x_d3d_nv50_cubes[index].base == 0ul) {
            slot = &v9x_d3d_nv50_cubes[index];
            break;
        }
    }
    if (slot == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    /* Each surface's face from its DDSCAPS2 and level from its edge, each
     * pair once; +X's top must lead the list, the linear part's first
     * byte. */
    for (face = 0ul; face < V9X_NV50_D3D_CUBE_FACES; ++face) {
        seen[face] = 0ul;
    }
    for (index = 0ul; index < data->dwSCnt; ++index) {
        const V9X_DD_SURFACE_LCL *surface = list[index];
        DWORD edge;

        if (surface == 0 || surface->lpGbl == 0 ||
            (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        face = v9x_d3d_nv50_cube_face(v9x_d3d_nv50_caps2(surface));
        edge = (DWORD)surface->lpGbl->wWidth;
        for (level = 0ul; level < levels; ++level) {
            if (edge == ((side >> level) != 0ul ? side >> level : 1ul)) {
                break;
            }
        }
        if (face >= V9X_NV50_D3D_CUBE_FACES || level == levels ||
            (DWORD)surface->lpGbl->wHeight != edge ||
            (seen[face] & (1ul << level)) != 0ul ||
            (index == 0ul && (face != 0ul || level != 0ul))) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        seen[face] |= 1ul << level;
        offsets[index] = level_offsets[level] + face * slices[level];
        pitches[index] = level_pitches[level];
    }

    /* As a Z buffer: a block on 4 KiB with a unit to spare, then each part
     * moved to its own unit boundary (v9x_d3d_nv50_create_depth). */
    span = v9x_nv50_rblock_span(unit, V9X_D3D_NV50_HEAP_ALIGN, linear_bytes,
                                tiled_bytes);
    if (span == 0ul ||
        v9x_d3d_place_chain(data, V9X_D3D_NV50_HEAP_ALIGN, span, offsets,
                            pitches, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    (void)v9x_nv50_rblock_place(base, span, unit, linear_bytes, tiled_bytes,
                                &linear_offset, &tiled_offset);
    for (index = 0ul; index < data->dwSCnt; ++index) {
        list[index]->lpGbl->fpVidMem += linear_offset - base;
    }
    slot->base = linear_offset;
    slot->linear_end = linear_offset + linear_bytes;
    slot->side = side;
    slot->bytes = bytes;
    slot->edge = cell_edge;
    slot->levels = levels;
    for (level = 0ul; level < levels; ++level) {
        slot->linear_level[level] = linear_offset + level_offsets[level];
        slot->linear_slice[level] = slices[level];
        slot->linear_pitch[level] = level_pitches[level];
        slot->tiled_level[level] = tiled_offsets[level];
        slot->modes[level] = tiled_modes[level];
    }
    slot->tiled_offset = tiled_offset;
    slot->layer_bytes = layer_bytes;
    slot->dirty = 1ul;
    slot->release_window =
        list[0]->lpGbl->fpVidMem - list[0]->lpGbl->dwReserved1 + 1ul;
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += span;
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
    DWORD format;
    DWORD edge = 1ul;
    DWORD width;
    DWORD height;
    DWORD level;

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
    /* A cube's list is six faces' chains, longer than one chain may be. */
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) != 0ul &&
        (v9x_d3d_nv50_caps2(surface) & V9X_DDSCAPS2_CUBEMAP) != 0ul) {
        format = v9x_d3d_nv50_format_of(surface, &bytes);
        if (format == V9X_D3D_NV50_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        return v9x_d3d_nv50_create_cube(
            data, bytes,
            V9X_NV50_D3D_TEX_IS_DXT(format) ? V9X_NV50_D3D_DXT_EDGE : 1ul);
    }
    if (data->dwSCnt > V9X_D3D_NV50_LEVELS_MAX) {
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
        format = v9x_d3d_nv50_format_of(surface, &bytes);
        if (width > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
            height > V9X_D3D_NV50_TEXTURE_SIDE_MAX ||
            format == V9X_D3D_NV50_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        /* DXT in rows of blocks, a part block whole: the rows the CPU
         * writes, here on 64 bytes for the 2D engine's copy (the Lock
         * below hands the CPU them packed). */
        if (V9X_NV50_D3D_TEX_IS_DXT(format)) {
            edge = V9X_NV50_D3D_DXT_EDGE;
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
            pitches[level] = ((width + edge - 1ul) / edge * bytes +
                              V9X_D3D_NV50_PITCH_ALIGN - 1ul) &
                             ~(V9X_D3D_NV50_PITCH_ALIGN - 1ul);
            rows[level] = (height + edge - 1ul) / edge;
        }
    } else {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (v9x_d3d_place_each(data, V9X_D3D_NV50_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    /* A mipmapped or DXT texture also gets its tiled copy in the pool. */
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) != 0ul &&
        (data->dwSCnt > 1ul || edge > 1ul)) {
        v9x_d3d_nv50_mip_create(data, bytes, edge);
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * DXT levels and the CPU, as the HD 4600's (d3d_hsw.c). Whatever writes a
 * DXT level - the Direct3D 8 runtime uploading a managed texture, or
 * DirectDraw's own Blt - writes its rows of blocks back to back from the
 * address Lock hands out, whatever lPitch says (the 4570, boot 156). This
 * engine keeps those rows on 64 bytes for the 2D engine's copy, so a level
 * whose rows are narrower is pointed, for the lock's length, at a packed
 * copy in the locking process's memory; Unlock writes it back at the
 * level's pitch and points the level home. The pool's copy is then stale
 * (surface_written). Locks come and go under the Win16 lock, a few at a
 * time; HAL statics are shared by every process, the copy is the locking
 * process's own.
 */
#define V9X_D3D_NV50_DXT_LOCKS       8ul

typedef struct v9x_d3d_nv50_dxt_lock {
    const V9X_DD_SURFACE_LCL *surface;
    DWORD process;
    DWORD home;
    BYTE *packed;
} V9X_D3D_NV50_DXT_LOCK;

static V9X_D3D_NV50_DXT_LOCK v9x_d3d_nv50_dxt_locks[V9X_D3D_NV50_DXT_LOCKS];

/* An engine-placed DXT level whose packed rows are narrower than its
 * pitch: its rows of blocks and their bytes; 0 for any other surface. */
static int v9x_d3d_nv50_dxt_level(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *rows, DWORD *row_bytes)
{
    DWORD bytes = 0ul;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->fpVidMem == 0ul ||
        surface->lpGbl->dwBlockSizeX != 0ul ||
        surface->lpGbl->lPitch <= 0l ||
        !V9X_NV50_D3D_TEX_IS_DXT(v9x_d3d_nv50_format_of(surface, &bytes))) {
        return 0;
    }
    *rows = ((DWORD)surface->lpGbl->wHeight + V9X_NV50_D3D_DXT_EDGE - 1ul) /
            V9X_NV50_D3D_DXT_EDGE;
    *row_bytes = ((DWORD)surface->lpGbl->wWidth + V9X_NV50_D3D_DXT_EDGE -
                  1ul) / V9X_NV50_D3D_DXT_EDGE * bytes;
    return *rows != 0ul && *row_bytes < (DWORD)surface->lpGbl->lPitch;
}

static void v9x_d3d_nv50_lock_surface(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_NV50_DXT_LOCK *slot = 0;
    const BYTE *from;
    DWORD rows;
    DWORD row_bytes;
    DWORD pitch;
    DWORD n;
    DWORD row;
    DWORD i;

    if (!v9x_d3d_nv50_dxt_level(surface, &rows, &row_bytes)) {
        return;
    }
    for (n = 0ul; n < V9X_D3D_NV50_DXT_LOCKS; ++n) {
        if (v9x_d3d_nv50_dxt_locks[n].surface == surface) {
            return;     /* already pointed at its packed copy */
        }
        if (slot == 0 && v9x_d3d_nv50_dxt_locks[n].surface == 0) {
            slot = &v9x_d3d_nv50_dxt_locks[n];
        }
    }
    if (slot == 0) {
        return;
    }
    slot->packed = (BYTE *)VirtualAlloc(0, rows * row_bytes,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    if (slot->packed == 0) {
        return;
    }
    pitch = (DWORD)surface->lpGbl->lPitch;
    from = (const BYTE *)surface->lpGbl->fpVidMem;
    for (row = 0ul; row < rows; ++row) {
        for (i = 0ul; i < row_bytes; ++i) {
            slot->packed[row * row_bytes + i] = from[row * pitch + i];
        }
    }
    slot->surface = surface;
    slot->process = GetCurrentProcessId();
    slot->home = surface->lpGbl->fpVidMem;
    surface->lpGbl->fpVidMem = (DWORD)slot->packed;
}

/* Puts a locked DXT level home, its rows written back at its pitch when
 * write_back; nonzero when it was one. */
static int v9x_d3d_nv50_dxt_unlock(const V9X_DD_SURFACE_LCL *surface,
                                   int write_back)
{
    V9X_D3D_NV50_DXT_LOCK *slot = 0;
    BYTE *to;
    DWORD rows;
    DWORD row_bytes;
    DWORD pitch;
    DWORD n;
    DWORD row;
    DWORD i;

    for (n = 0ul; n < V9X_D3D_NV50_DXT_LOCKS; ++n) {
        if (surface != 0 && v9x_d3d_nv50_dxt_locks[n].surface == surface) {
            slot = &v9x_d3d_nv50_dxt_locks[n];
            break;
        }
    }
    if (slot == 0) {
        return 0;
    }
    surface->lpGbl->fpVidMem = slot->home;
    /* The copy lives in the process that locked; from another one only
     * the address can be put back. */
    if (slot->process == GetCurrentProcessId()) {
        if (write_back &&
            v9x_d3d_nv50_dxt_level(surface, &rows, &row_bytes)) {
            pitch = (DWORD)surface->lpGbl->lPitch;
            to = (BYTE *)slot->home;
            for (row = 0ul; row < rows; ++row) {
                for (i = 0ul; i < row_bytes; ++i) {
                    to[row * pitch + i] = slot->packed[row * row_bytes + i];
                }
            }
        }
        (void)VirtualFree(slot->packed, 0ul, MEM_RELEASE);
    }
    slot->surface = 0;
    slot->packed = 0;
    return 1;
}

static void v9x_d3d_nv50_unlock_surface(const V9X_DD_SURFACE_LCL *surface)
{
    (void)v9x_d3d_nv50_dxt_unlock(surface, 1);
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
    V9X_D3D_NV50_CUBE *cube;
    V9X_D3D_NV50_MIP *mip;
    DWORD offset;

    /* Destroyed while locked: a DXT level is pointed home first, so the
     * release below finds the block it placed. */
    (void)v9x_d3d_nv50_dxt_unlock(surface, 0);
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
            /* A mipmapped texture's level 0: its pool block is given back
             * (each level frees its own heap block below). */
            mip = v9x_d3d_nv50_mip_at(offset);
            if (mip != 0) {
                mip->base = 0ul;
                if (v9x_hal->d3d_diagnostics.nv50_pool_used >=
                    mip->pool_bytes) {
                    v9x_hal->d3d_diagnostics.nv50_pool_used -=
                        mip->pool_bytes;
                }
            }
            /* A cube's +X top holds the block: forgotten with it. Its other
             * surfaces carry no block and free nothing. */
            cube = v9x_d3d_nv50_cube_at(offset);
            if (cube != 0) {
                if (v9x_d3d_place_release(data, cube->release_window)) {
                    cube->base = 0ul;
                }
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
        v9x_d3d_nv50_cube_written(offset);
        v9x_d3d_nv50_mip_written(offset);
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
    v9x_d3d_nv50_lock_surface,          /* lock_surface: DXT levels */
    v9x_d3d_nv50_unlock_surface,        /* unlock_surface */
    v9x_d3d_nv50_draw_shader,           /* draw_shader: step 4 */
    v9x_d3d_nv50_surface_written
};
