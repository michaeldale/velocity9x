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
#include "r3d/r3d_cull.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/cik_shader.h"
#include "velocity9x/si_shader.h"
#include "velocity9x/vi_ps11.h"

/* Triangles a batch takes from one call: the arena holds 2 MiB, this is
 * 48 KiB of vertices. */
#define V9X_D3D_VI_CHUNK_TRIANGLES 512ul
#define V9X_D3D_VI_VERTEX_DWORDS   (V9X_VI_TL_VERTEX_BYTES / 4ul)

/* A two-unit draw's VS parameters: colour, unit 0's (u, v), specular,
 * unit 1's (u, v) (si_shader.h's TL2 VS). */
#define V9X_D3D_VI_TWO_UNIT_PARAMS 4ul
#define V9X_D3D_VI_FLOAT_ONE       0x3f800000ul

/* A batch's stream: the TL draw (about 200 dwords), padding to 16. */
#define V9X_D3D_VI_STREAM_DWORDS 512ul

/* The DXT FourCCs a surface carries ('DXT1', 'DXT3', 'DXT5'). */
#define V9X_D3D_VI_FOURCC_DXT1 0x31545844ul
#define V9X_D3D_VI_FOURCC_DXT3 0x33545844ul
#define V9X_D3D_VI_FOURCC_DXT5 0x35545844ul

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
    1ul,            /* guard_band_ddi6 */
    0ul,            /* dxt_textures */
    0ul,            /* modulate2x */
    0ul,            /* bump_luminance */
    V9X_D3D_VI_CHUNK_TRIANGLES /* batch_triangles: the CP's chunk */
};

/*
 * The Oland's: the same, with two texture stages in one pass, the
 * two-unit PS and si_shader.h's TL2 VS (step 4 of docs\plans\
 * ati-gcn-ps11.md). Carrizo and Mullins keep one until their machines run
 * the step's gate: the TL2 VS is SI's alone so far. The core's two-stage
 * conditions hold for all three: clip_in_core 0, and no pixel-centre move
 * to make (pixel_centre_half 0).
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_vi_oland_limits = {
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
    2ul,            /* texture_stages */
    1ul,            /* guard_band_ddi6 */
    /* DXT1/3/5 as BC1-3 in 1D-thin tiles, which the sampler reads
     * (V9XSICP /ps11:6-8) and linear-aligned BC it does not (step 7 of
     * docs\plans\ati-gcn-ps11.md). */
    1ul,            /* dxt_textures */
    0ul,            /* modulate2x */
    0ul,            /* bump_luminance */
    V9X_D3D_VI_CHUNK_TRIANGLES, /* batch_triangles: the CP's chunk */
    8192ul,         /* indexed_pool_vertices: v9x_d3d_vi_draw_indexed's */
    1ul             /* indexed_two_units */
};

/* The T# data format of a texture format the sampler reads, or UNKNOWN. */
static DWORD v9x_d3d_vi_format_of(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *bytes_out, DWORD *has_alpha,
                                  DWORD *snorm)
{
    const V9X_DDPIXELFORMAT *format;
    v9x_u32 signed_format;
    v9x_u32 signed_bytes;

    *snorm = 0ul;
    if (surface == 0 || surface->lpGbl == 0) {
        return V9X_D3D_VI_TEXTURE_UNKNOWN;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    /* DXT1, DXT3 and DXT5 as BC1-3 (step 7), bytes a 4 x 4 block's: the
     * core publishes them where the engine says it places and samples
     * them (dxt_textures). */
    if ((format->dwFlags & V9X_DDPF_FOURCC) != 0ul &&
        (format->dwFourCC == V9X_D3D_VI_FOURCC_DXT1 ||
         format->dwFourCC == V9X_D3D_VI_FOURCC_DXT3 ||
         format->dwFourCC == V9X_D3D_VI_FOURCC_DXT5)) {
        *bytes_out = format->dwFourCC == V9X_D3D_VI_FOURCC_DXT1 ? 8ul : 16ul;
        *has_alpha = 1ul;
        return format->dwFourCC == V9X_D3D_VI_FOURCC_DXT1
                   ? V9X_VI_IMG_FORMAT_BC1
                   : format->dwFourCC == V9X_D3D_VI_FOURCC_DXT3
                         ? V9X_VI_IMG_FORMAT_BC2 : V9X_VI_IMG_FORMAT_BC3;
    }
    /* Q8W8V8U8 and V8U8, which the core publishes where pixel shaders run
     * and only a shader stage reads (step 6). */
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        if (!v9x_d3d_vi_signed_format(format->dwFlags, format->dwFourCC,
                                      format->dwRGBBitCount,
                                      format->dwRBitMask, format->dwGBitMask,
                                      format->dwBBitMask, &signed_format,
                                      &signed_bytes)) {
            return V9X_D3D_VI_TEXTURE_UNKNOWN;
        }
        *bytes_out = signed_bytes;
        *has_alpha = signed_bytes == 4ul ? 1ul : 0ul;
        *snorm = 1ul;
        return signed_format;
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
    DWORD snorm;

    if (format_out == 0) {
        return 0;
    }
    *format_out = v9x_d3d_vi_format_of(surface, &bytes, &has_alpha, &snorm);
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

/* Oland (GFX6): every stream in the builders' GFX6 mode, and the built
 * pixel shaders rewritten for SI (vi_3d.h, cik_shader.h). */
static int v9x_d3d_vi_gfx6(void)
{
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_VI_OLAND) != 0ul;
}

static void v9x_d3d_vi_stream_init(struct v9x_vi_pm4 *stream)
{
    v9x_vi_pm4_init(stream, v9x_d3d_vi_stream, V9X_D3D_VI_STREAM_DWORDS);
    stream->gfx6 = v9x_d3d_vi_gfx6() ? 1ul : 0ul;
}

/*
 * A chain's levels as the sampler finds them from the base, for a texture
 * of format (V9X_VI_IMG_FORMAT_*) and bytes (a texel's, or a BC block's):
 * v9x_vi_3d_mip_layout's linear-aligned levels, or for DXT
 * v9x_vi_3d_bc_layout's 1D-thin ones. Offsets and each level's lPitch in
 * bytes (a BC level's is its blocks across times the block's bytes);
 * nonzero on success.
 */
static int v9x_d3d_vi_chain_layout(DWORD format, DWORD bytes, DWORD width,
                                   DWORD height, DWORD levels,
                                   v9x_u32 *offsets, v9x_u32 *pitch_bytes,
                                   v9x_u32 *total)
{
    v9x_u32 sizes[V9X_VI_MIP_LEVELS_MAX];
    DWORD level;

    if (V9X_VI_IMG_FORMAT_IS_BC(format)) {
        if (!v9x_vi_3d_bc_layout(bytes, width, height, levels, offsets, sizes,
                                 pitch_bytes, total)) {
            return 0;
        }
    } else if (!v9x_vi_3d_mip_layout(bytes, width, height, levels, offsets,
                                     pitch_bytes, total)) {
        return 0;
    }
    for (level = 0ul; level < levels; ++level) {
        pitch_bytes[level] *= bytes;
    }
    return 1;
}

/* A level's pitch in texels from its lPitch: a BC level's lPitch is its
 * blocks across times the block's bytes. */
static DWORD v9x_d3d_vi_pitch_texels(DWORD format, DWORD bytes, LONG pitch)
{
    return V9X_VI_IMG_FORMAT_IS_BC(format) ? (DWORD)pitch / bytes * 4ul
                                           : (DWORD)pitch / bytes;
}

/*
 * A mip chain's levels, counted as far as each sits where
 * v9x_d3d_vi_chain_layout puts it - create_surface placed it there in one
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
    if (!v9x_d3d_vi_chain_layout(texture->format, bytes, texture->width,
                                 texture->height, possible, offsets, pitches,
                                 &total) ||
        texture->pitch_texels !=
            v9x_d3d_vi_pitch_texels(texture->format, bytes,
                                    (LONG)pitches[0])) {
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
            (DWORD)next->lpGbl->lPitch != pitches[found]) {
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
                    (pitches[found] & 0xfffful);
            }
            break;
        }
        ++found;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = found;
    texture->levels = found;
}

/*
 * Cubes, step 6 of docs\plans\ati-gcn-ps11.md, as the HD 4600's step 5c
 * (d3d_hsw.c). The Direct3D 8 runtime hands a video-memory cube to
 * CreateSurface whole: six faces in D3DCUBEMAP_FACES order, each face's
 * top and then its levels, each surface naming its face in DDSCAPS2 (the
 * 4570, boot 131). The sampler reads a cube as one surface of six array
 * slices, so create_surface places it as one block in
 * v9x_vi_3d_cube_layout's order, each surface found by its face bit and its
 * edge. The block starts with +X's top, which CreateSurfaceEx names and a
 * texture record carries; the cube is remembered by that offset, and a
 * texture there samples as a cube. HAL statics are shared by every
 * process, as video memory is.
 */
#define V9X_D3D_VI_CUBES 64ul

typedef struct v9x_d3d_vi_cube {
    DWORD base;                 /* +X top's offset; 0 for a free slot */
    DWORD side;
    DWORD levels;
} V9X_D3D_VI_CUBE;

static V9X_D3D_VI_CUBE v9x_d3d_vi_cubes[V9X_D3D_VI_CUBES];

/* The placed cube whose +X top is at offset, or null. */
static const V9X_D3D_VI_CUBE *v9x_d3d_vi_cube_at(DWORD offset)
{
    DWORD n;

    for (n = 0ul; offset != 0ul && n < V9X_D3D_VI_CUBES; ++n) {
        if (v9x_d3d_vi_cubes[n].base == offset) {
            return &v9x_d3d_vi_cubes[n];
        }
    }
    return 0;
}

/* A surface's DDSCAPS2, from its DDRAWI_DDRAWSURFACE_MORE; the runtime's
 * own halves are live while CreateSurface runs. */
static DWORD v9x_d3d_vi_caps2(const V9X_DD_SURFACE_LCL *surface)
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
static DWORD v9x_d3d_vi_cube_face(DWORD caps2)
{
    DWORD bits = caps2 & V9X_DDSCAPS2_CUBEMAP_ALLFACES;
    DWORD face;

    for (face = 0ul; face < V9X_VI_CUBE_FACES; ++face) {
        if (bits == (0x400ul << face)) {
            return face;
        }
    }
    return V9X_VI_CUBE_FACES;
}

/* The bound texture's level 0 as the map takes it. */
static void v9x_d3d_vi_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                       V9X_D3D_VI_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface =
        (const V9X_DD_SURFACE_LCL *)sampler->object;
    const V9X_D3D_VI_CUBE *cube;
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD snorm = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    texture->has_key = 0ul;
    texture->key = 0ul;
    texture->cube = 0ul;
    texture->snorm = 0ul;
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
    texture->format = v9x_d3d_vi_format_of(surface, &bytes, &has_alpha,
                                           &snorm);
    if (texture->format == V9X_D3D_VI_TEXTURE_UNKNOWN) {
        return;
    }
    /* A DXT surface DirectDraw placed (its heap named: the engine's
     * placements leave lpVidMemHeap, dwBlockSizeX, null) is linear, not in
     * the tiles the sampler reads, and its lPitch a linear size. */
    if (V9X_VI_IMG_FORMAT_IS_BC(texture->format) &&
        surface->lpGbl->dwBlockSizeX != 0ul) {
        texture->format = V9X_D3D_VI_TEXTURE_UNKNOWN;
        return;
    }
    texture->snorm = snorm;
    texture->has_alpha = has_alpha;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_texels = v9x_d3d_vi_pitch_texels(texture->format, bytes,
                                                    surface->lpGbl->lPitch);
    texture->offset = offset;
    texture->levels = 1ul;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1ul;
        texture->key = surface->ddckCKSrcBltLow;
    }
    /* A cube's other faces are attached to its +X top like levels, so its
     * levels come from the placement, not a walk of the attachments. */
    cube = v9x_d3d_vi_cube_at(offset);
    if (cube != 0) {
        texture->cube = 1ul;
        texture->levels = cube->levels;
        return;
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

/* The draw's mapping. unit1 takes a two-unit draw's second unit on the
 * Oland, the one chip whose limits claim two stages; elsewhere it is not
 * passed and such a draw is refused. */
static v9x_u32 v9x_d3d_vi_map(const V9X_R3D_DRAW *draw,
                              struct v9x_vi_tl_draw *out,
                              struct v9x_vi_tl_draw *unit1, v9x_u32 *ps,
                              struct v9x_vi_d3d_ps *built)
{
    V9X_D3D_VI_TEXTURE texture;
    V9X_D3D_VI_TEXTURE texture1;
    DWORD high;
    DWORD heap_low;
    int two = v9x_d3d_vi_gfx6();

    /* Every address in the batch is the heap's or the window's: without
     * them located the batch would point at the wrong memory. */
    if (!v9x_vi_cp_addresses(&high, &heap_low)) {
        return V9X_D3D_VI_REFUSE_ADDRESS;
    }
    texture.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    texture.levels = 1ul;
    texture1.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    texture1.levels = 1ul;
    if (draw->texture.object != 0) {
        v9x_d3d_vi_resolve_texture(&draw->texture, &texture);
    }
    if (two && draw->texture1.object != 0) {
        v9x_d3d_vi_resolve_texture(&draw->texture1, &texture1);
    }
    return v9x_d3d_vi_map_draw(draw, &texture, two ? &texture1 : 0, high,
                               heap_low, v9x_d3d_vi_depth_bytes(draw), out,
                               two ? unit1 : 0, ps, built);
}

static int v9x_d3d_vi_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_vi_tl_draw out;
    struct v9x_vi_tl_draw unit1;
    struct v9x_vi_d3d_ps built;
    v9x_u32 ps;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_vi_map(draw, &out, &unit1, &ps, &built);
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
/*
 * A draw into the buffer the last flip is taking off the screen, while
 * that flip is pending, waits for it on the CPU: flips are queued behind
 * their frame (eg_scanout.c), so the panel may still be showing this
 * buffer while the frame after it is being built. A triple-buffered
 * application never draws there; a double-buffered one waits here as it
 * used to wait in Flip. A wait that runs out goes ahead, as elsewhere.
 */
static void v9x_d3d_vi_flip_guard(const V9X_R3D_DRAW *draw)
{
    int waited;

    if (draw->target.offset != v9x_present_retiring_offset() ||
        !v9x_flip_pending()) {
        return;
    }
    waited = v9x_flip_wait_done();
    ++v9x_hal->d3d_diagnostics.draws_flip_waited;
    if (waited == V9X_FLIP_WAIT_TIMEOUT) {
        ++v9x_hal->d3d_diagnostics.draws_flip_wait_timeouts;
    }
}

/* A draw's state once mapped (v9x_d3d_vi_prepare): the TL draw, unit 1's,
 * the two-unit table and the built PS's length (0: a slot's PS). */
typedef struct v9x_d3d_vi_prepared {
    struct v9x_vi_tl_draw state;
    struct v9x_vi_tl_draw unit1;
    DWORD table[V9X_VI_TWO_UNIT_TABLE_DWORDS];
    DWORD ps_dwords;
    int two_units;
} V9X_D3D_VI_PREPARED;

/* The draw mapped to the TL draw's state, its built PS made, the two-unit
 * table filled, and a texture drawn into since the group's last fence
 * flushed first (vi_cp.c). Zero when the draw is refused. */
static int v9x_d3d_vi_prepare(const V9X_R3D_DRAW *draw,
                              V9X_D3D_VI_PREPARED *p)
{
    struct v9x_vi_d3d_ps built;
    DWORD sampled0;
    v9x_u32 ps;
    v9x_u32 reason;

    p->ps_dwords = 0ul;
    p->two_units = 0;
    reason = v9x_d3d_vi_map(draw, &p->state, &p->unit1, &ps, &built);
    if (reason != V9X_D3D_VI_REFUSE_NONE) {
        v9x_d3d_vi_count_refusal(draw, reason);
        return 0;
    }
    p->state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS);
    if (ps == V9X_D3D_VI_PS_BUILT) {
        p->ps_dwords = v9x_vi_shader_d3d_ps_built(&built,
                                                  v9x_d3d_vi_built_ps);
        /* Mullins runs GCN 2: the built shader is rewritten in place, and
         * one the translation refuses goes to the software path. */
        if ((v9x_hal->engine.engine_caps &
             V9X_DD_ENGINE_CAP_VI_MULLINS) != 0ul &&
            v9x_cik_shader_from_vi(v9x_d3d_vi_built_ps, p->ps_dwords, 0) !=
                V9X_STATUS_OK) {
            return 0;
        }
        /* Oland runs GCN 1: the same, with FLAT refused. */
        if (v9x_d3d_vi_gfx6() &&
            v9x_si_shader_from_vi(v9x_d3d_vi_built_ps, p->ps_dwords, 0) !=
                V9X_STATUS_OK) {
            return 0;
        }
    } else {
        p->state.ps_low = v9x_vi_cp_shader_low(v9x_d3d_vi_ps_slot[ps]);
    }
    sampled0 = p->state.texture_low;
    /* Two units (Oland only, v9x_d3d_vi_map): both units' descriptors
     * into the table the PS loads, which goes into the arena with each
     * batch; the TL2 VS, and unit 1's pairs as a second stream. */
    if (ps == V9X_D3D_VI_PS_BUILT && built.two_units != 0ul) {
        p->two_units = 1;
        v9x_vi_3d_texture_sharps(&p->state, p->table + V9X_VI_TWO_UNIT_T0);
        v9x_vi_3d_texture_sharps(&p->unit1, p->table + V9X_VI_TWO_UNIT_T1);
        p->state.texture_low = 0ul;
        p->state.ps_sgprs = V9X_VI_TWO_UNIT_PS_SGPRS;
        p->state.params = V9X_D3D_VI_TWO_UNIT_PARAMS;
        p->state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS2);
        p->state.vs_vgprs = V9X_SI_TL2_VS_VGPRS;
        p->state.vertex1_stride = V9X_SI_TL2_STREAM_BYTES;
    }
    p->state.shader_high = p->state.target_high;
    p->state.vertex_high = p->state.target_high;
    p->state.fence_high = p->state.target_high;
    p->state.no_fence = 1ul;
    return v9x_vi_cp_before_read(sampled0) &&
           (!p->two_units || v9x_vi_cp_before_read(p->unit1.texture_low));
}

/* count dwords into the arena, pad bytes after them left unwritten; its
 * GPU low half, or 0. */
static DWORD v9x_d3d_vi_arena_copy(const v9x_u32 *words, DWORD count,
                                   DWORD pad)
{
    volatile DWORD *linear;
    DWORD gpu_low;
    DWORD index;

    if (!v9x_vi_cp_vertex_space(count * 4ul + pad, &linear, &gpu_low)) {
        return 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        linear[index] = words[index];
    }
    return gpu_low;
}

/* A batch's built PS and two-unit table into the arena, ahead of its
 * vertices. A built PS is followed by the pad (V9X_VI_CP_SHADER_PAD), so
 * the batch may skip the shader-cache sync. */
static int v9x_d3d_vi_put_program(V9X_D3D_VI_PREPARED *p)
{
    if (p->ps_dwords != 0ul) {
        p->state.ps_low = v9x_d3d_vi_arena_copy(v9x_d3d_vi_built_ps,
                                                p->ps_dwords,
                                                V9X_VI_CP_SHADER_PAD);
        if (p->state.ps_low == 0ul) {
            return 0;
        }
    }
    if (p->two_units) {
        p->state.ps_table_low = v9x_d3d_vi_arena_copy(
            p->table, V9X_VI_TWO_UNIT_TABLE_DWORDS, 0ul);
        if (p->state.ps_table_low == 0ul) {
            return 0;
        }
        p->state.ps_table_high = p->state.target_high;
    }
    return 1;
}

/* Unit 1's (tu1, tv1) for count vertices, as the TL2 VS reads them: 0
 * and 1 after each pair, sixteen bytes a vertex. */
static int v9x_d3d_vi_put_pairs(V9X_D3D_VI_PREPARED *p, const DWORD *pairs,
                                DWORD count)
{
    volatile DWORD *linear;
    DWORD gpu_low;
    DWORD index;

    if (!v9x_vi_cp_vertex_space(count * V9X_SI_TL2_STREAM_BYTES, &linear,
                                &gpu_low)) {
        return 0;
    }
    for (index = 0ul; index < count; ++index) {
        linear[4ul * index] = pairs[2ul * index];
        linear[4ul * index + 1ul] = pairs[2ul * index + 1ul];
        linear[4ul * index + 2ul] = 0ul;
        linear[4ul * index + 3ul] = V9X_D3D_VI_FLOAT_ONE;
    }
    p->state.vertex1_low = gpu_low;
    p->state.vertex1_high = p->state.target_high;
    return 1;
}

/* The TL draw's packets for the state as it stands, as an open batch
 * (vi_cp.c), the shader-cache sync first when it is due. Zero on a
 * refusal or a failed submission. */
static int v9x_d3d_vi_submit(const V9X_R3D_DRAW *draw,
                             V9X_D3D_VI_PREPARED *p)
{
    struct v9x_vi_pm4 stream;
    int synced;

    /* After the arena space is taken, which can make the sync due. */
    synced = v9x_vi_cp_sync_due();
    p->state.no_cache_sync = synced ? 0ul : 1ul;
    v9x_d3d_vi_stream_init(&stream);
    if (v9x_vi_3d_tl_draw(&stream, &p->state) != V9X_STATUS_OK) {
        /* The target the builder would not take, in the trace's
         * DrawsTargetLast, DrawsPitchLast and DrawsExtentLast. */
        v9x_hal->d3d_diagnostics.draws_target_last = p->state.target_low;
        v9x_hal->d3d_diagnostics.draws_pitch_last = p->state.pitch_pixels;
        v9x_hal->d3d_diagnostics.draws_extent_last =
            p->state.pitch_pixels | (p->state.height << 16);
        v9x_d3d_vi_count_refusal(draw, V9X_D3D_VI_REFUSE_STATE);
        return 0;
    }
    v9x_vi_pm4_pad(&stream);
    if (stream.overflow != 0ul) {
        v9x_d3d_vi_count_refusal(draw, V9X_D3D_VI_REFUSE_OVERFLOW);
        return 0;
    }
    if (!v9x_vi_cp_submit_open(stream.dwords, stream.count,
                               p->state.target_low, p->state.depth_low,
                               synced)) {
        return 0;
    }
    v9x_present_note_submission();
    return 1;
}

static int v9x_d3d_vi_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    V9X_D3D_VI_PREPARED p;
    const DWORD *pairs;
    DWORD chunk;
    DWORD started;
    int ok = 1;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_vi_ready()) {
        return 0;
    }
    v9x_d3d_vi_flip_guard(draw);
    pairs = (const DWORD *)draw->texcoords1;
    started = V9X_TIME_BEGIN();
    if (!v9x_d3d_vi_prepare(draw, &p)) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    while (ok && triangle_count != 0ul) {
        chunk = triangle_count > V9X_D3D_VI_CHUNK_TRIANGLES ?
                V9X_D3D_VI_CHUNK_TRIANGLES : triangle_count;
        p.state.vertex_low = 0ul;
        ok = v9x_d3d_vi_put_program(&p) &&
             (p.state.vertex_low = v9x_d3d_vi_arena_copy(
                  (const v9x_u32 *)vertices,
                  chunk * 3ul * V9X_D3D_VI_VERTEX_DWORDS, 0ul)) != 0ul &&
             (!p.two_units ||
              v9x_d3d_vi_put_pairs(&p, pairs, chunk * 3ul));
        if (!ok) {
            break;
        }
        p.state.vertex_count = chunk * 3ul;
        V9X_TIME_END(V9X_TIME_DECODE, started);
        ok = v9x_d3d_vi_submit(draw, &p);
        started = V9X_TIME_BEGIN();
        vertices += chunk * 3ul;
        if (pairs != 0) {
            pairs += chunk * 3ul * 2ul;
        }
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return ok;
}

/*
 * A whole indexed list or strip (d3d_internal.h, draw_indexed), on the
 * Oland: the pool's vertices into the arena once, the indices of the
 * triangles the list path would draw - a strip's odd triangles swapped
 * as the core's are, culled by the core's rule on the pool's screen
 * positions - into the arena as 16-bit indices, and one DRAW_INDEX_2.
 * The list path converted, copied and culled every corner of every
 * triangle, so a vertex shared by six triangles went through it six
 * times: 3DMark 2001 SE sent 463 M triangles this way in one run
 * (docs\decisions\2026-11-18-gcn-batch-cost.md). Declined, with nothing
 * submitted, for an index past the pool or a call past the arena's
 * share.
 */
#define V9X_D3D_VI_INDEXED_VERTICES 8192ul
#define V9X_D3D_VI_INDEXED_INDICES  49152ul
static v9x_u16 v9x_d3d_vi_index_scratch[V9X_D3D_VI_INDEXED_INDICES];

static int v9x_d3d_vi_draw_indexed(const V9X_R3D_DRAW *draw,
                                   const V9X_R3D_VERTEX *pool,
                                   DWORD vertex_count,
                                   const v9x_u16 *indices,
                                   DWORD index_count,
                                   int strip,
                                   DWORD cull_mode)
{
    V9X_D3D_VI_PREPARED p;
    const v9x_u16 *word;
    DWORD triangles;
    DWORD kept = 0ul;
    DWORD started;
    DWORD i;

    if (draw == 0 || pool == 0 || indices == 0 || !v9x_d3d_vi_ready() ||
        !v9x_d3d_vi_gfx6() || vertex_count == 0ul ||
        vertex_count > V9X_D3D_VI_INDEXED_VERTICES || index_count < 3ul ||
        (strip ? index_count - 2ul : index_count / 3ul) * 3ul >
            V9X_D3D_VI_INDEXED_INDICES) {
        return -1;
    }
    triangles = strip ? index_count - 2ul : index_count / 3ul;
    for (i = 0ul; i < triangles; ++i) {
        DWORD a;
        DWORD b;
        DWORD c;

        word = strip ? indices + i : indices + i * 3ul;
        a = word[0];
        b = word[1];
        c = word[2];
        if (strip && (i & 1ul) != 0ul) {
            DWORD t = a;

            a = b;
            b = t;
        }
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
            return -1;
        }
        if (cull_mode != V9X_R3D_CULL_NONE &&
            v9x_r3d_cull_triangle(cull_mode, pool[a].sx, pool[a].sy,
                                  pool[b].sx, pool[b].sy, pool[c].sx,
                                  pool[c].sy)) {
            continue;
        }
        v9x_d3d_vi_index_scratch[kept * 3ul] = (v9x_u16)a;
        v9x_d3d_vi_index_scratch[kept * 3ul + 1ul] = (v9x_u16)b;
        v9x_d3d_vi_index_scratch[kept * 3ul + 2ul] = (v9x_u16)c;
        ++kept;
    }
    if (kept == 0ul) {
        return 1;
    }

    v9x_d3d_vi_flip_guard(draw);
    started = V9X_TIME_BEGIN();
    if (!v9x_d3d_vi_prepare(draw, &p)) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    /* Two units with no pairs from the core: the list path's to draw. */
    if (p.two_units && draw->texcoords1 == 0) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return -1;
    }
    if (!v9x_d3d_vi_put_program(&p)) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    p.state.vertex_low = v9x_d3d_vi_arena_copy(
        (const v9x_u32 *)pool, vertex_count * V9X_D3D_VI_VERTEX_DWORDS, 0ul);
    /* Whole dwords of indices: an odd count's last one is padding. */
    p.state.index_low = p.state.vertex_low == 0ul ? 0ul :
        v9x_d3d_vi_arena_copy((const v9x_u32 *)v9x_d3d_vi_index_scratch,
                              (kept * 3ul + 1ul) / 2ul, 0ul);
    if (p.state.index_low == 0ul ||
        (p.two_units &&
         !v9x_d3d_vi_put_pairs(&p, (const DWORD *)draw->texcoords1,
                               vertex_count))) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    p.state.index_high = p.state.target_high;
    p.state.index_count = kept * 3ul;
    p.state.vertex_count = vertex_count;
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return v9x_d3d_vi_submit(draw, &p);
}

/*
 * PIXEL SHADERS, step 5 of docs\plans\ati-gcn-ps11.md, the Oland alone: a
 * list of triangles under a DirectX 8 pixel shader (d3d_internal.h,
 * draw_shader). The program is v9x_vi_ps11_build's, rewritten for SI, as
 * V9XSICP /ps11 drew it right on the card (2026-11-17). Translated
 * programs are kept by shader and what of the draw they depend on, CPU
 * side, and each batch's copy goes into the vertex arena with its table
 * and wide vertices, as the built fixed-function PS goes: nothing on the
 * GPU is rewritten while it may be read.
 *
 * The target, depth and blend are the fixed-function map's, and so are
 * the alpha test's function and reference and the fog colour, which the
 * program applies. Channels are in their PS 1.1 places: the T#s read red
 * into x (texture_rgba) and the CB swaps x back to red's byte (SWAP_ALT
 * for XRGB8888, SWAP_STD_REV for RGB565), the convention V9XSICP /ps11
 * measured. The wide vertices come unclipped and go to the PA's guard
 * band, as the TL vertices do. A stage whose texture is a cube
 * create_surface placed samples it as one (step 6), and so do texm3x3tex
 * and texm3x3spec there; the translator refuses them on a 2D stage. A
 * signed texture reads its channels in memory order (texture_signed).
 */
#define V9X_D3D_VI_PS_CODE_MAX   512ul
#define V9X_D3D_VI_PS_ENTRIES    8ul
#define V9X_D3D_VI_PS_CHUNK      256ul
#define V9X_D3D_VI_WIDE_DWORDS   (V9X_R3D_SHADER_FLOATS)

typedef struct v9x_d3d_vi_ps_entry {
    const V9X_PS11_PROGRAM *program;    /* null: an empty entry */
    DWORD serial;
    DWORD projected;
    DWORD unbound;
    DWORD cubes;
    DWORD fog;
    DWORD alpha_test;
    DWORD alpha_func;
    DWORD words;
    DWORD sgprs;
    DWORD kills;
    v9x_u32 code[V9X_D3D_VI_PS_CODE_MAX];
} V9X_D3D_VI_PS_ENTRY;

static V9X_D3D_VI_PS_ENTRY v9x_d3d_vi_ps_cache[V9X_D3D_VI_PS_ENTRIES];
static DWORD v9x_d3d_vi_ps_next = 0ul;
static v9x_u32 v9x_d3d_vi_ps_table[V9X_VI_PS11_TABLE_DWORDS];
/* The draw with its textures, specular and key taken off, for the map:
 * static, as HAL stacks are small. */
static V9X_R3D_DRAW v9x_d3d_vi_ps_plain;

/* The entry for this shader and draw: found, or the next one taken and
 * its program translated for SI. Null when it does not translate. */
static V9X_D3D_VI_PS_ENTRY *v9x_d3d_vi_ps_entry(const V9X_R3D_SHADER *shader,
                                                DWORD unbound, DWORD cubes,
                                                DWORD fog,
                                                DWORD alpha_test,
                                                DWORD alpha_func)
{
    struct v9x_vi_ps11_desc desc;
    struct v9x_vi_ps11_info info;
    V9X_D3D_VI_PS_ENTRY *entry;
    DWORD n;

    for (n = 0ul; n < V9X_D3D_VI_PS_ENTRIES; ++n) {
        entry = &v9x_d3d_vi_ps_cache[n];
        if (entry->program == shader->program &&
            entry->serial == shader->serial &&
            entry->projected == shader->env.projected &&
            entry->unbound == unbound && entry->cubes == cubes &&
            entry->fog == fog &&
            entry->alpha_test == alpha_test &&
            entry->alpha_func == alpha_func) {
            return entry;
        }
    }
    n = v9x_d3d_vi_ps_next;
    v9x_d3d_vi_ps_next = (n + 1ul) % V9X_D3D_VI_PS_ENTRIES;
    entry = &v9x_d3d_vi_ps_cache[n];
    entry->program = 0;
    desc.cube = cubes;
    desc.projected = shader->env.projected;
    desc.unbound = unbound;
    desc.fog = fog;
    desc.alpha_test = alpha_test;
    desc.alpha_func = alpha_func;
    if (v9x_vi_ps11_build(entry->code, V9X_D3D_VI_PS_CODE_MAX,
                          shader->program, &desc, &info) != V9X_STATUS_OK ||
        v9x_si_shader_from_vi(entry->code, info.words, 0) != V9X_STATUS_OK) {
        return 0;
    }
    entry->program = shader->program;
    entry->serial = shader->serial;
    entry->projected = shader->env.projected;
    entry->unbound = unbound;
    entry->cubes = cubes;
    entry->fog = fog;
    entry->alpha_test = alpha_test;
    entry->alpha_func = alpha_func;
    entry->words = info.words;
    entry->sgprs = info.sgprs;
    entry->kills = info.kills;
    return entry;
}

static int v9x_d3d_vi_draw_shader(const V9X_R3D_DRAW *draw,
                                  const V9X_R3D_SHADER *shader,
                                  const float *vertices,
                                  DWORD triangle_count)
{
    struct v9x_vi_tl_draw state;
    struct v9x_vi_tl_draw unit;
    struct v9x_vi_d3d_ps built;
    struct v9x_vi_pm4 stream;
    V9X_D3D_VI_TEXTURE none;
    V9X_D3D_VI_TEXTURE resolved;
    V9X_D3D_VI_PS_ENTRY *entry;
    v9x_u32 sharps[V9X_VI_TEXTURE_SHARP_DWORDS];
    v9x_u32 ps;
    v9x_u32 reason;
    DWORD high;
    DWORD heap_low;
    DWORD sampled;
    DWORD bound = 0ul;
    DWORD cubes = 0ul;
    DWORD stage;
    DWORD index;
    DWORD chunk;
    DWORD started;
    DWORD ps_low;
    DWORD table_low;
    DWORD gpu_low;
    int synced;

    if (draw == 0 || shader == 0 || shader->program == 0 || vertices == 0 ||
        triangle_count == 0ul || !v9x_d3d_vi_ready() || !v9x_d3d_vi_gfx6() ||
        !v9x_vi_cp_addresses(&high, &heap_low)) {
        return 0;
    }
    v9x_d3d_vi_flip_guard(draw);
    started = V9X_TIME_BEGIN();

    /* The fixed-function map for target, depth, blend, and the alpha
     * test's and fog's terms: a draw with no texture, specular or key. */
    v9x_d3d_vi_ps_plain = *draw;
    v9x_d3d_vi_ps_plain.texture.object = 0;
    v9x_d3d_vi_ps_plain.texture.levels = 0;
    v9x_d3d_vi_ps_plain.texture1.object = 0;
    v9x_d3d_vi_ps_plain.texture1.levels = 0;
    v9x_d3d_vi_ps_plain.texcoords1 = 0;
    v9x_d3d_vi_ps_plain.specular_enable = 0ul;
    v9x_d3d_vi_ps_plain.color_key_enable = 0ul;
    none.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    none.levels = 1ul;
    none.cube = 0ul;
    none.snorm = 0ul;
    reason = v9x_d3d_vi_map_draw(&v9x_d3d_vi_ps_plain, &none, 0, high,
                                 heap_low, v9x_d3d_vi_depth_bytes(draw),
                                 &state, 0, &ps, &built);
    if (reason != V9X_D3D_VI_REFUSE_NONE) {
        v9x_d3d_vi_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }

    /* Each sampled stage's T# and S#, or (0, 0, 0, 1) for none bound. */
    for (index = 0ul; index < V9X_VI_PS11_TABLE_DWORDS; ++index) {
        v9x_d3d_vi_ps_table[index] = 0ul;
    }
    sampled = shader->program->samples;
    for (stage = 0ul; stage < V9X_R3D_SHADER_STAGES; ++stage) {
        if ((sampled & (1ul << stage)) == 0ul ||
            shader->texture[stage].object == 0) {
            continue;
        }
        resolved.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
        resolved.levels = 1ul;
        v9x_d3d_vi_resolve_texture(&shader->texture[stage], &resolved);
        reason = v9x_d3d_vi_map_shader_unit(&shader->texture[stage],
                                            &resolved, high, heap_low, &unit);
        if (reason != V9X_D3D_VI_REFUSE_NONE) {
            v9x_d3d_vi_count_refusal(draw, reason);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        /* Fenced as a group (vi_cp.c): a texture drawn into since the
         * group's last fence is flushed to memory first. */
        if (!v9x_vi_cp_before_read(unit.texture_low)) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        v9x_vi_3d_texture_sharps(&unit, sharps);
        for (index = 0ul; index < 8ul; ++index) {
            v9x_d3d_vi_ps_table[V9X_VI_PS11_T_SHARP + 8ul * stage + index] =
                sharps[index];
        }
        for (index = 0ul; index < 4ul; ++index) {
            v9x_d3d_vi_ps_table[V9X_VI_PS11_S_SHARP + 4ul * stage + index] =
                sharps[8ul + index];
        }
        bound |= 1ul << stage;
        /* The core marks no stage a cube (d3d_core.c): a stage is one
         * where its texture is a cube create_surface placed. */
        if (resolved.cube != 0ul) {
            cubes |= 1ul << stage;
        }
    }
    v9x_vi_ps11_constants(shader->program, &shader->env,
                          v9x_d3d_vi_ps_table);
    /* The map's fog colour is B, G, R in memory order; the table's R, G,
     * B. Its alpha reference is a float on [0, 1]. */
    v9x_d3d_vi_ps_table[V9X_VI_PS11_FOG] = built.fog_colour[2];
    v9x_d3d_vi_ps_table[V9X_VI_PS11_FOG + 1ul] = built.fog_colour[1];
    v9x_d3d_vi_ps_table[V9X_VI_PS11_FOG + 2ul] = built.fog_colour[0];
    v9x_d3d_vi_ps_table[V9X_VI_PS11_ALPHA_REF] = built.alpha_ref;

    entry = v9x_d3d_vi_ps_entry(shader, sampled & ~bound, cubes,
                                built.fog != 0ul ? 1ul : 0ul,
                                built.alpha_test != 0ul ? 1ul : 0ul,
                                built.alpha_test != 0ul ? built.alpha_func
                                                        : 0ul);
    if (entry == 0) {
        v9x_d3d_vi_count_refusal(draw, V9X_D3D_VI_REFUSE_STATE);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }

    state.texture_low = 0ul;
    state.target_swap = draw->target.format == V9X_R3D_FORMAT_RGB565
                            ? V9X_VI_CB_SWAP_STD_REV : V9X_VI_CB_SWAP_ALT;
    state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS_WIDE);
    state.vs_vgprs = V9X_SI_WIDE_VS_VGPRS;
    state.vertex_stride = V9X_SI_WIDE_VERTEX_BYTES;
    state.params = V9X_SI_WIDE_PARAMS;
    state.ps_vgprs = V9X_VI_PS11_VGPRS;
    state.ps_sgprs = entry->sgprs;
    state.ps_kill = entry->kills;
    state.shader_high = state.target_high;
    state.vertex_high = state.target_high;
    state.fence_high = state.target_high;
    state.ps_table_high = state.target_high;

    while (triangle_count != 0ul) {
        chunk = triangle_count > V9X_D3D_VI_PS_CHUNK ? V9X_D3D_VI_PS_CHUNK
                                                     : triangle_count;
        ps_low = v9x_d3d_vi_arena_copy(entry->code, entry->words,
                                       V9X_VI_CP_SHADER_PAD);
        table_low = ps_low != 0ul
                        ? v9x_d3d_vi_arena_copy(v9x_d3d_vi_ps_table,
                                                V9X_VI_PS11_TABLE_DWORDS, 0ul)
                        : 0ul;
        gpu_low = table_low != 0ul
                      ? v9x_d3d_vi_arena_copy(
                            (const v9x_u32 *)vertices,
                            chunk * 3ul * V9X_D3D_VI_WIDE_DWORDS, 0ul)
                      : 0ul;
        if (gpu_low == 0ul) {
            V9X_TIME_END(V9X_TIME_DECODE, started);
            return 0;
        }
        state.ps_low = ps_low;
        state.ps_table_low = table_low;
        state.vertex_low = gpu_low;
        state.vertex_count = chunk * 3ul;
        state.no_fence = 1ul;
        synced = v9x_vi_cp_sync_due();
        state.no_cache_sync = synced ? 0ul : 1ul;

        v9x_d3d_vi_stream_init(&stream);
        if (v9x_vi_3d_tl_draw(&stream, &state) != V9X_STATUS_OK) {
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
        if (!v9x_vi_cp_submit_open(stream.dwords, stream.count,
                                   state.target_low, state.depth_low,
                                   synced)) {
            return 0;
        }
        v9x_present_note_submission();
        started = V9X_TIME_BEGIN();
        vertices += chunk * 3ul * V9X_D3D_VI_WIDE_DWORDS;
        triangle_count -= chunk;
    }
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return 1;
}

/* One corner of a fill's rectangle: a flat colour, no texture. */
static void v9x_d3d_vi_fill_vertex(V9X_R3D_VERTEX *vertex, LONG x, LONG y,
                                   float z, DWORD color)
{
    vertex->sx = (float)x;
    vertex->sy = (float)y;
    vertex->sz = z;
    vertex->rhw = 1.0f;
    vertex->color = color;
    vertex->specular = 0ul;
    vertex->tu = 0.0f;
    vertex->tv = 0.0f;
}

/*
 * A fill's rectangle as two triangles under state (target, depth and
 * shaders already set), into the arena and out as an open batch: no fence
 * of its own, so a frame's clears join its draws' group (vi_cp.c), and
 * the shader-cache sync only when due. The core's centres are whole
 * coordinates, so the edges cover pixels left to right - 1.
 */
static int v9x_d3d_vi_rect_draw(struct v9x_vi_tl_draw *state,
                                const LONG *rect, float z, DWORD color)
{
    struct v9x_vi_pm4 stream;
    V9X_R3D_VERTEX vertices[6];
    int synced;

    v9x_d3d_vi_fill_vertex(&vertices[0], rect[0], rect[1], z, color);
    v9x_d3d_vi_fill_vertex(&vertices[1], rect[2], rect[1], z, color);
    v9x_d3d_vi_fill_vertex(&vertices[2], rect[0], rect[3], z, color);
    v9x_d3d_vi_fill_vertex(&vertices[3], rect[2], rect[1], z, color);
    v9x_d3d_vi_fill_vertex(&vertices[4], rect[2], rect[3], z, color);
    v9x_d3d_vi_fill_vertex(&vertices[5], rect[0], rect[3], z, color);
    state->vertex_low = v9x_d3d_vi_arena_copy(
        (const v9x_u32 *)vertices, sizeof(vertices) / 4ul, 0ul);
    if (state->vertex_low == 0ul) {
        return 0;
    }
    state->vertex_count = 6ul;
    state->no_fence = 1ul;

    /* After the arena space is taken, which can make the sync due. */
    synced = v9x_vi_cp_sync_due();
    state->no_cache_sync = synced ? 0ul : 1ul;
    v9x_d3d_vi_stream_init(&stream);
    if (v9x_vi_3d_tl_draw(&stream, state) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_vi_pm4_pad(&stream);
    if (stream.overflow != 0ul ||
        !v9x_vi_cp_submit_open(stream.dwords, stream.count,
                               state->target_low, state->depth_low,
                               synced)) {
        return 0;
    }
    v9x_present_note_submission();
    return 1;
}

/*
 * A rectangle of a Z16 at value, drawn on the CP: every depth fill, the
 * whole surface's included (eng_vi.c). The SDMA engine cannot write part
 * of one tiled (Cedar's d3d_eg.c the same way), and a fill of all of it
 * there made the CPU wait for the CP first (docs\decisions\
 * 2026-11-18-gcn-batch-cost.md). Two triangles over it, ZFUNC ALWAYS
 * with writes on; the colour target is the Z buffer's own memory named
 * as RGB565, which only sizes the draw: no channel is written. The batch
 * waits for queued SDMA work itself (vi_cp.c).
 */
int v9x_d3d_vi_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                          DWORD value, int wait)
{
    struct v9x_vi_tl_draw state;
    DWORD high;
    DWORD heap_low;
    DWORD offset;
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
    if (!v9x_d3d_vi_rect_draw(&state, rect, z, 0ul)) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

/*
 * A 5:6:5 value's channels widened to a D3DCOLOR's eight bits, each
 * field's top bits repeated below it: the CB's UNORM conversion of the
 * result back to five and six bits returns the field it came from.
 */
static DWORD v9x_d3d_vi_rgb565_color(DWORD value)
{
    DWORD red = (value >> 11) & 0x1ful;
    DWORD green = (value >> 5) & 0x3ful;
    DWORD blue = value & 0x1ful;

    red = (red << 3) | (red >> 2);
    green = (green << 2) | (green >> 4);
    blue = (blue << 3) | (blue >> 2);
    return 0xff000000ul | (red << 16) | (green << 8) | blue;
}

/*
 * A rectangle of a render target at value (in the surface's own format),
 * drawn on the CP as d3d_core.c's Clear and DirectDraw's colour fills ask
 * (eng_vi.c). On the SDMA engine each one made the CPU wait for the CP
 * to drain first: 31,642 colour fills in one 3DMark 2001 SE run
 * (docs\decisions\2026-11-18-gcn-batch-cost.md). Only a 3D device's
 * surface, in the two layouts the CB draws here: 5:6:5 and 32 bpp, all
 * four bytes written as value has them. Anything else, and any surface
 * the CB's linear-aligned rules refuse, is declined to the SDMA fill.
 */
int v9x_d3d_vi_color_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                          DWORD value)
{
    struct v9x_vi_tl_draw state;
    const V9X_DDPIXELFORMAT *format;
    DWORD high;
    DWORD heap_low;
    DWORD offset;
    DWORD pitch;
    DWORD bytes;
    DWORD color;
    DWORD index;

    if (surface == 0 || rect == 0 || surface->lpGbl == 0 ||
        surface->lpGbl->lPitch <= 0l || !v9x_d3d_vi_ready() ||
        (surface->ddsCaps & V9X_DDSCAPS_3DDEVICE) == 0ul ||
        rect[0] < 0l || rect[1] < 0l || rect[2] <= rect[0] ||
        rect[3] <= rect[1] ||
        rect[2] > (LONG)surface->lpGbl->wWidth ||
        rect[3] > (LONG)surface->lpGbl->wHeight ||
        !v9x_vi_cp_addresses(&high, &heap_low)) {
        return V9X_BLT_DECLINED;
    }

    /* The surface's own format when it carries one, else the display's,
     * as d3d_core.c classifies a render target. */
    format = (surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul
                 ? &surface->lpGbl->ddpfSurface
                 : &v9x_hal->info.vmiData.ddpfDisplay;
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return V9X_BLT_DECLINED;
    }
    for (index = 0ul; index < sizeof(state); ++index) {
        ((unsigned char *)&state)[index] = 0u;
    }
    if (format->dwRGBBitCount == 32ul) {
        bytes = 4ul;
        state.format = V9X_VI_CB_FORMAT_8_8_8_8;
        color = value;
    } else if (format->dwRGBBitCount == 16ul &&
               format->dwRBitMask == 0x0000f800ul &&
               format->dwGBitMask == 0x000007e0ul &&
               format->dwBBitMask == 0x0000001ful) {
        bytes = 2ul;
        state.format = V9X_VI_CB_FORMAT_5_6_5;
        color = v9x_d3d_vi_rgb565_color(value);
    } else {
        return V9X_BLT_DECLINED;
    }

    /* d3d_vi_map.c's target rules: rows of whole 64 bytes and 8-pixel
     * tiles, the slice whole 64-pixel tiles, the base on 256 bytes. */
    pitch = (DWORD)surface->lpGbl->lPitch;
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || heap_low + offset < heap_low ||
        ((heap_low + offset) & (V9X_D3D_VI_SURFACE_ALIGN - 1ul)) != 0ul ||
        (pitch % V9X_D3D_VI_ROW_ALIGN) != 0ul ||
        (pitch % (8ul * bytes)) != 0ul ||
        pitch / bytes < (DWORD)surface->lpGbl->wWidth ||
        ((pitch / bytes) * (DWORD)surface->lpGbl->wHeight) % 64ul != 0ul) {
        return V9X_BLT_DECLINED;
    }

    state.target_low = heap_low + offset;
    state.target_high = high;
    state.pitch_pixels = pitch / bytes;
    state.height = (DWORD)surface->lpGbl->wHeight;
    state.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    state.d3d_pixel_centres = 1ul;
    state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS);
    state.ps_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_PS_FIRST +
                                        V9X_VI_PS_COLOUR);
    state.shader_high = high;
    state.vertex_high = high;
    state.fence_high = high;
    if (!v9x_d3d_vi_rect_draw(&state, rect, 0.0f, color)) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

/*
 * A video-memory cube, placed whole in v9x_vi_3d_cube_layout's order (the
 * note above v9x_d3d_vi_cubes): six square faces of equal levels, +X's top
 * first, and a free slot to remember it by. Declined, for DirectDraw to
 * place, when any of that does not hold; a cube DirectDraw places is
 * sampled as nothing usable, as on the HD 4600 before its step 5c. A DXT
 * cube takes the same level-major order with each face a 1D-thin level
 * (v9x_vi_3d_bc_layout); that combination is unmeasured.
 */
static DWORD v9x_d3d_vi_create_cube(V9X_DDHAL_CREATESURFACEDATA *data,
                                    DWORD format, DWORD bytes)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    v9x_u32 level_offsets[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 slices[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 level_pitches[V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 offsets[V9X_VI_CUBE_FACES * V9X_VI_MIP_LEVELS_MAX];
    v9x_u32 pitches[V9X_VI_CUBE_FACES * V9X_VI_MIP_LEVELS_MAX];
    DWORD seen[V9X_VI_CUBE_FACES];
    V9X_D3D_VI_CUBE *slot = 0;
    v9x_u32 total;
    DWORD side = (DWORD)list[0]->lpGbl->wWidth;
    DWORD levels = data->dwSCnt / V9X_VI_CUBE_FACES;
    DWORD index;
    DWORD face;
    DWORD level;
    DWORD base;

    if (data->dwSCnt % V9X_VI_CUBE_FACES != 0ul || levels == 0ul ||
        levels > V9X_VI_MIP_LEVELS_MAX ||
        side != (DWORD)list[0]->lpGbl->wHeight ||
        side > v9x_d3d_vi_limits.texture_size_max) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (V9X_VI_IMG_FORMAT_IS_BC(format)) {
        /* One face's 1D-thin chain gives each level's slice and pitch in
         * blocks; the six faces of a level go together, as for a linear
         * cube. */
        if (!v9x_vi_3d_bc_layout(bytes, side, side, levels, level_offsets,
                                 slices, level_pitches, &total)) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        total = 0ul;
        for (level = 0ul; level < levels; ++level) {
            level_offsets[level] = total;
            total += slices[level] * V9X_VI_CUBE_FACES;
        }
    } else if (!v9x_vi_3d_cube_layout(bytes, side, levels, level_offsets,
                                      slices, level_pitches, &total)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    for (index = 0ul; index < V9X_D3D_VI_CUBES; ++index) {
        if (v9x_d3d_vi_cubes[index].base == 0ul) {
            slot = &v9x_d3d_vi_cubes[index];
            break;
        }
    }
    if (slot == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    /* Each surface's face from its DDSCAPS2 and level from its edge, each
     * pair once; +X's top must lead the list, the block's first byte. */
    for (face = 0ul; face < V9X_VI_CUBE_FACES; ++face) {
        seen[face] = 0ul;
    }
    for (index = 0ul; index < data->dwSCnt; ++index) {
        const V9X_DD_SURFACE_LCL *surface = list[index];
        DWORD edge;

        if (surface == 0 || surface->lpGbl == 0 ||
            (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        face = v9x_d3d_vi_cube_face(v9x_d3d_vi_caps2(surface));
        edge = (DWORD)surface->lpGbl->wWidth;
        for (level = 0ul; level < levels; ++level) {
            if (edge == ((side >> level) != 0ul ? side >> level : 1ul)) {
                break;
            }
        }
        if (face >= V9X_VI_CUBE_FACES || level == levels ||
            (DWORD)surface->lpGbl->wHeight != edge ||
            (seen[face] & (1ul << level)) != 0ul ||
            (index == 0ul && (face != 0ul || level != 0ul))) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        seen[face] |= 1ul << level;
        offsets[index] = level_offsets[level] + face * slices[level];
        pitches[index] = level_pitches[level] * bytes;
    }
    if (v9x_d3d_place_chain(data, V9X_D3D_VI_SURFACE_ALIGN, total, offsets,
                            pitches, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    slot->base = base;
    slot->side = side;
    slot->levels = levels;
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += total;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
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
 * DirectDraw, and only its level 0 is sampled. A cube goes in one block
 * too (v9x_d3d_vi_create_cube). A signed texture is placed here like any
 * other: the runtime describes Q8W8V8U8 as a FourCC with no bit count,
 * which DirectDraw cannot size (the HD 4600, boot 133).
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
    DWORD snorm = 0ul;
    DWORD format;
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
            height > v9x_d3d_vi_limits.texture_size_max) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        format = v9x_d3d_vi_format_of(surface, &bytes, &has_alpha, &snorm);
        if (format == V9X_D3D_VI_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if ((v9x_d3d_vi_caps2(surface) & V9X_DDSCAPS2_CUBEMAP) != 0ul) {
            return v9x_d3d_vi_create_cube(data, format, bytes);
        }
        /* DXT, one level or a chain, in 1D-thin tiles: rows of blocks
         * rounded to whole tiles, which DirectDraw cannot size. */
        if (V9X_VI_IMG_FORMAT_IS_BC(format) && data->dwSCnt == 1ul) {
            if (!v9x_d3d_vi_chain_layout(format, bytes, width, height, 1ul,
                                         offsets, pitches, &total) ||
                v9x_d3d_place_chain(data, V9X_D3D_VI_SURFACE_ALIGN, total,
                                    offsets, pitches, &base) != 0ul) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            ++v9x_hal->d3d_diagnostics.texture_placed;
            v9x_hal->d3d_diagnostics.texture_placed_bytes += total;
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (data->dwSCnt > 1ul) {
            /* Each level half the last, as v9x_d3d_vi_chain_layout lays
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
            if (!v9x_d3d_vi_chain_layout(format, bytes,
                                         (DWORD)surface->lpGbl->wWidth,
                                         (DWORD)surface->lpGbl->wHeight,
                                         data->dwSCnt, offsets, pitches,
                                         &total)) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
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
        /* One level as a chain's level 0: the sampler widens its rows 64
         * bytes at a time until the level is whole 256-byte blocks, as the
         * chain layout measured. Rows of 64 bytes alone put a 2x2's second
         * row where the sampler did not read it (V9XGLP Small2x2T01, boot
         * 39). The layout takes powers of two only; other sides keep
         * 64-byte rows, unmeasured. */
        if (v9x_vi_3d_mip_layout(bytes, width, height, 1ul, offsets, pitches,
                                 &total)) {
            pitches[0] *= bytes;
        } else {
            pitches[0] = (width * bytes + V9X_D3D_VI_ROW_ALIGN - 1ul) &
                         ~(V9X_D3D_VI_ROW_ALIGN - 1ul);
        }
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

/*
 * DXT levels and the CPU, as the HD 4600's (d3d_hsw.c). DirectDraw keeps a
 * DXT surface's lPitch as a linear size, and whatever writes one - the
 * Direct3D 8 runtime uploading a managed texture, or DirectDraw's own Blt -
 * writes its rows of blocks back to back from the address Lock hands out.
 * The engine keeps the level in 1D-thin tiles (v9x_vi_3d_bc_block_offset),
 * so Lock points the level, for the lock's length, at a linear copy in the
 * locking process's memory, filled from the tiles block by block; Unlock
 * writes it back the same way and points the level at the tiles again.
 * Locks come and go under the Win16 lock, a few at a time. HAL statics
 * are shared by every process; the copy is the locking process's own.
 */
#define V9X_D3D_VI_BC_LOCKS 8ul

typedef struct v9x_d3d_vi_bc_lock {
    const V9X_DD_SURFACE_LCL *surface;
    DWORD process;
    DWORD tiled;
    BYTE *linear;
} V9X_D3D_VI_BC_LOCK;

static V9X_D3D_VI_BC_LOCK v9x_d3d_vi_bc_locks[V9X_D3D_VI_BC_LOCKS];

/* An engine-placed DXT level: its blocks across and down, a block's bytes
 * and its pitch in blocks; 0 for any other surface. */
static int v9x_d3d_vi_bc_level(const V9X_DD_SURFACE_LCL *surface,
                               DWORD *across, DWORD *down, DWORD *block,
                               DWORD *pitch_blocks)
{
    DWORD has_alpha = 0ul;
    DWORD snorm = 0ul;
    DWORD format;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->fpVidMem == 0ul ||
        surface->lpGbl->dwBlockSizeX != 0ul ||
        surface->lpGbl->lPitch <= 0l) {
        return 0;
    }
    format = v9x_d3d_vi_format_of(surface, block, &has_alpha, &snorm);
    if (!V9X_VI_IMG_FORMAT_IS_BC(format)) {
        return 0;
    }
    *across = ((DWORD)surface->lpGbl->wWidth + 3ul) / 4ul;
    *down = ((DWORD)surface->lpGbl->wHeight + 3ul) / 4ul;
    *pitch_blocks = (DWORD)surface->lpGbl->lPitch / *block;
    return *across != 0ul && *down != 0ul && *across <= *pitch_blocks &&
           (*pitch_blocks % V9X_VI_BC_TILE_BLOCKS) == 0ul;
}

/* Every block between the tiles at tiled and the packed rows at linear,
 * to_linear the direction. */
static void v9x_d3d_vi_bc_copy(BYTE *tiled, BYTE *linear, DWORD across,
                               DWORD down, DWORD block, DWORD pitch_blocks,
                               int to_linear)
{
    DWORD bx;
    DWORD by;
    DWORD i;

    for (by = 0ul; by < down; ++by) {
        for (bx = 0ul; bx < across; ++bx) {
            BYTE *t = tiled + v9x_vi_3d_bc_block_offset(block, pitch_blocks,
                                                        bx, by);
            BYTE *l = linear + (by * across + bx) * block;

            for (i = 0ul; i < block; ++i) {
                if (to_linear) {
                    l[i] = t[i];
                } else {
                    t[i] = l[i];
                }
            }
        }
    }
}

/* Nonzero when surface is a DXT level, whether or not it could be pointed
 * at a copy. */
static int v9x_d3d_vi_bc_lock(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_VI_BC_LOCK *slot = 0;
    DWORD across;
    DWORD down;
    DWORD block;
    DWORD pitch_blocks;
    DWORD n;

    if (!v9x_d3d_vi_bc_level(surface, &across, &down, &block,
                             &pitch_blocks)) {
        return 0;
    }
    for (n = 0ul; n < V9X_D3D_VI_BC_LOCKS; ++n) {
        if (v9x_d3d_vi_bc_locks[n].surface == surface) {
            return 1;   /* already pointed at its linear copy */
        }
        if (slot == 0 && v9x_d3d_vi_bc_locks[n].surface == 0) {
            slot = &v9x_d3d_vi_bc_locks[n];
        }
    }
    if (slot == 0) {
        return 1;
    }
    slot->linear = (BYTE *)VirtualAlloc(0, across * down * block,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    if (slot->linear == 0) {
        return 1;
    }
    v9x_d3d_vi_bc_copy((BYTE *)surface->lpGbl->fpVidMem, slot->linear,
                       across, down, block, pitch_blocks, 1);
    slot->surface = surface;
    slot->process = GetCurrentProcessId();
    slot->tiled = surface->lpGbl->fpVidMem;
    surface->lpGbl->fpVidMem = (DWORD)slot->linear;
    return 1;
}

/* Puts a locked DXT level back in its tiles; nonzero when it was one. */
static int v9x_d3d_vi_bc_unlock(const V9X_DD_SURFACE_LCL *surface,
                                int write_back)
{
    V9X_D3D_VI_BC_LOCK *slot = 0;
    DWORD across;
    DWORD down;
    DWORD block;
    DWORD pitch_blocks;
    DWORD n;

    for (n = 0ul; n < V9X_D3D_VI_BC_LOCKS; ++n) {
        if (surface != 0 && v9x_d3d_vi_bc_locks[n].surface == surface) {
            slot = &v9x_d3d_vi_bc_locks[n];
            break;
        }
    }
    if (slot == 0) {
        return 0;
    }
    surface->lpGbl->fpVidMem = slot->tiled;
    /* The copy lives in the process that locked; from another one only
     * the address can be put back. */
    if (slot->process == GetCurrentProcessId()) {
        if (write_back &&
            v9x_d3d_vi_bc_level(surface, &across, &down, &block,
                                &pitch_blocks)) {
            v9x_d3d_vi_bc_copy((BYTE *)slot->tiled, slot->linear, across,
                               down, block, pitch_blocks, 0);
        }
        (void)VirtualFree(slot->linear, 0ul, MEM_RELEASE);
    }
    slot->surface = 0;
    slot->linear = 0;
    return 1;
}

static void v9x_d3d_vi_lock_surface(const V9X_DD_SURFACE_LCL *surface)
{
    DWORD slot;

    if (v9x_d3d_vi_bc_lock(surface)) {
        return;
    }
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
    if (v9x_d3d_vi_bc_unlock(surface, 1)) {
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
    DWORD offset;
    DWORD n;

    /* Destroyed while locked: its memory is no longer a lock to undo. A
     * DXT level is pointed back at its tiles first, so the release below
     * finds the block it placed. */
    (void)v9x_d3d_vi_bc_unlock(surface, 0);
    if (surface != 0 && surface->lpGbl != 0 &&
        surface->lpGbl->fpVidMem != 0ul) {
        slot = v9x_d3d_vi_locked_slot(surface->lpGbl->fpVidMem);
        if (slot != V9X_D3D_VI_LOCKED_MAX) {
            v9x_d3d_vi_locked[slot] = 0ul;
        }
    }
    /* A cube's +X top frees the block; forget the cube that started there,
     * by the offset it had before the release. */
    offset = surface != 0 && surface->lpGbl != 0 ? v9x_surface_offset(surface)
                                                 : 0xfffffffful;
    if (v9x_d3d_place_release(data, V9X_D3D_VI_SURFACE_ALIGN)) {
        for (n = 0ul; n < V9X_D3D_VI_CUBES; ++n) {
            if (v9x_d3d_vi_cubes[n].base == offset) {
                v9x_d3d_vi_cubes[n].base = 0ul;
            }
        }
    }
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

/* The Oland's: the same functions under its two-stage limits, and pixel
 * shaders (step 5 of docs\plans\ati-gcn-ps11.md). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_vi_oland = {
    &v9x_d3d_vi_oland_limits,
    v9x_d3d_vi_texture_format,
    v9x_d3d_vi_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_vi_ready,
    v9x_d3d_vi_create_surface,
    v9x_d3d_vi_destroy_surface,
    v9x_d3d_vi_draw,
    v9x_d3d_vi_accepts,
    v9x_d3d_vi_draw_indexed,            /* draw_indexed: GFX6's VS only */
    0,                                  /* draw_tnl */
    0,                                  /* tnl_available */
    v9x_d3d_vi_lock_surface,
    v9x_d3d_vi_unlock_surface,
    v9x_d3d_vi_draw_shader              /* draw_shader: step 5 */
};
