/*
 * NVIDIA RIVA 128 Direct3D engine: the D3D object (class 17h).
 *
 * Step 5 of docs\plans\nvidia-riva128-d3d.md. Every word this engine sends
 * comes from nv3_3d.c, host-tested against what V9XN32D /d3d and /d3dstate
 * drew on M-7V636C (docs\decisions\2026-11-03-riva128-first-triangle.md and
 * 2026-11-03-riva128-d3d-state.md). This file resolves DirectDraw surfaces,
 * keeps the swizzled copies the chip samples, and emits.
 *
 * Textures. The chip samples square, swizzled textures only. DirectDraw
 * and the application see an ordinary linear surface, which
 * v9x_d3d_nv3_create_surface places in a block with room after it for the
 * swizzled square copy. The copy is rebuilt by the CPU before a draw that
 * samples the texture whenever the linear texels may have changed: after an
 * Unlock or a Blt into it (the core's surface_written). A narrower texture
 * is repeated across the square and its coordinates scaled (nv3_3d.h,
 * u_shift). Mipmap chains are placed, but only level 0 is sampled.
 *
 * Synchronous: a batch begins with the engine brought up and idle
 * (eng_nv3.c, v9x_nv3_3d_begin) and ends idle, so a CPU Lock or the 2D side
 * never meets 3D work in flight.
 *
 * The chip draws X1R5G5B5. On a 5:6:5 desktop that is the wrong layout and
 * colours come out wrong; decision B of docs\plans\nvidia-riva128.md moves
 * 16 bpp desktops to 5:5:5 (step 3), and until then a 5:6:5 target is drawn
 * anyway, for geometry.
 */
#include "d3d_internal.h"
#include "velocity9x/nv3_3d.h"

#define V9X_D3D_NV3_SPINS 0x00200000ul

/* Bounded by the scratch-free swizzle's row buffer: 256 texels a row. */
#define V9X_D3D_NV3_TEXTURE_MAX 256ul
#define V9X_D3D_NV3_TEXTURE_MAX_LOG2 8ul
#define V9X_D3D_NV3_TEXTURE_LEVELS 9ul
/* The texture offset's alignment was not measured; the probes drew from
 * 4 KiB-aligned textures, and copies are placed on 256 bytes. */
#define V9X_D3D_NV3_TEXTURE_ALIGN 256ul
#define V9X_D3D_NV3_PITCH_ALIGN 16ul
#define V9X_D3D_NV3_TEXTURES 512ul
#define V9X_D3D_NV3_VERTEX_BYTES (V9X_NV3_D3D_VERTEX_METHODS * 4ul)
#define V9X_D3D_NV3_STATE_BYTES 64ul
/* An explicit draw's write mask with every channel on (r3d_abi.h
 * V9X_R3D_ABI_WRITE_RGB, which the HAL's engines do not include). */
#define V9X_D3D_NV3_WRITE_RGB 7ul

/* Refusal reasons, counted in the Mach64's policy counters as the other
 * engines do (V9XTRACE M64PolicyNN). */
#define V9X_D3D_NV3_REFUSE_NONE        0ul
#define V9X_D3D_NV3_REFUSE_EXPLICIT    1ul
#define V9X_D3D_NV3_REFUSE_TARGET      2ul
#define V9X_D3D_NV3_REFUSE_TEXTURE     3ul
#define V9X_D3D_NV3_REFUSE_TEXTURE_OP  4ul
#define V9X_D3D_NV3_REFUSE_ADDRESS     5ul
#define V9X_D3D_NV3_REFUSE_FILTER      6ul
#define V9X_D3D_NV3_REFUSE_BLEND       7ul
#define V9X_D3D_NV3_REFUSE_STATE       8ul
#define V9X_D3D_NV3_REFUSE_SHADE       9ul
#define V9X_D3D_NV3_REFUSE_SURFACE     10ul
/* 11-16 are the state refusals' causes (v9x_d3d_nv3_count_state_refusal). */
#define V9X_D3D_NV3_REFUSE_KEY         17ul

/* Where an accepted batch stopped, in M64RefuseLast (V9XTRACE): the
 * Mach64's counters again, since only V9XTRACE reads them. */
#define V9X_D3D_NV3_STOP_BEGIN         1ul
#define V9X_D3D_NV3_STOP_ROOM_STATE    2ul
#define V9X_D3D_NV3_STOP_ROOM_VERTEX   3ul
#define V9X_D3D_NV3_STOP_IDLE          4ul

/* One placed texture: its linear levels and its swizzled square copy,
 * all video memory offsets. A table, since HAL statics are shared by
 * every process (they are global to the DLL). The copy holds `levels`
 * squares back to back, the chain down to 8x8 at most
 * (V9X_NV3_MIP_LOG2_MIN); a texture with no chain has one. */
typedef struct v9x_d3d_nv3_texture {
    DWORD linear;
    DWORD swizzled;
    DWORD pitch;
    DWORD levels;
    DWORD level_linear[V9X_D3D_NV3_TEXTURE_LEVELS];
    DWORD level_pitch[V9X_D3D_NV3_TEXTURE_LEVELS];
    DWORD width;
    DWORD height;
    DWORD log2_side;
    DWORD format;
    int has_alpha;
    int dirty;
    /* A4R4G4B4 only: a second copy as X1R5G5B5 for opaque draws, which
     * the blend cannot draw from partial alphas (nv3_3d.h); under the
     * alpha test, the test's verdict in its top bit. `opaque_test` is
     * which of those it holds (V9X_D3D_NV3_OPAQUE_*). */
    DWORD opaque;
    int opaque_dirty;
    DWORD opaque_test;
    /* Whether level 0's alphas (A1R5G5B5's or X1R5G5B5's top bit) are
     * all 1, all 0 or mixed, for SRCALPHA/ONE (nv3_3d.h) and the colour
     * key: read from the linear level when first asked after a write. */
    DWORD alpha_full;
} V9X_D3D_NV3_TEXTURE;

#define V9X_D3D_NV3_ALPHA_UNKNOWN 0ul
#define V9X_D3D_NV3_ALPHA_ALL_ONE 1ul
#define V9X_D3D_NV3_ALPHA_PARTIAL 2ul
#define V9X_D3D_NV3_ALPHA_ALL_ZERO 3ul

/*
 * Which texture an A4R4G4B4 draw samples: its own swizzled copy (NONE),
 * the opaque copy with top bit 0 (PLAIN), or the opaque copy with the top
 * bit set where Direct3D's alpha test passes (TESTED | func << 8 | ref).
 */
#define V9X_D3D_NV3_OPAQUE_NONE    0ul
#define V9X_D3D_NV3_OPAQUE_PLAIN   1ul
#define V9X_D3D_NV3_OPAQUE_TESTED  0x10000ul

/*
 * The chip's own test of a TESTED copy: the one bit, read as alpha 0 or
 * FFh, against the midpoint. Under bilinear a filtered alpha between
 * them still splits the texels at the halfway point Direct3D's test
 * would put them nearest to, whether or not the chip filters alpha.
 */
#define V9X_D3D_NV3_TESTED_REFERENCE 0x7ful

static V9X_D3D_NV3_TEXTURE v9x_d3d_nv3_textures[V9X_D3D_NV3_TEXTURES];

/*
 * The surface registers take a pitch up to 1FF0h in 16-byte units; the
 * core clips to the target, within the 16-bit 1/16-pixel coordinates.
 * Z16, programmed at its own pitch. Direct3D's integer pixel centres are
 * the chip's (the edge and perspective cases of the state record).
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_nv3_limits = {
    16ul,           /* target_bits_per_pixel */
    V9X_NV3_PITCH_MAX, /* target_pitch_max */
    V9X_D3D_NV3_PITCH_ALIGN, /* target_pitch_align */
    2048ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    V9X_D3D_NV3_TEXTURE_MAX, /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_NV3_TEXTURE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own */
    /* Zeta holds z x 65536: FFFFh is far, a fill needs no shift. */
    0ul,            /* depth_fill_shift */
    1ul,            /* texture_units */
    0ul,            /* pixel_centre_half */
    0ul,            /* depth_tiled */
    0ul,            /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits */
    0ul,            /* centre_in_engine */
    1ul             /* texture_stages */
};

static DWORD v9x_d3d_nv3_read(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_d3d_nv3_write(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

static void v9x_d3d_nv3_emit(const struct v9x_nv3_write *writes,
                             DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        v9x_d3d_nv3_write(writes[index].offset, writes[index].value);
    }
}

/* Room in the channel's FIFO for `bytes` of methods: the method writes do
 * not wait on their own (riva_hw.h RIVA_FIFO_FREE). */
static int v9x_d3d_nv3_room(DWORD bytes)
{
    DWORD spins = V9X_D3D_NV3_SPINS;

    while (spins-- != 0ul) {
        if ((v9x_d3d_nv3_read(V9X_NV3_USER_FREE) & V9X_NV3_FREE_MASK) >=
            bytes) {
            return 1;
        }
    }
    return 0;
}

static int v9x_d3d_nv3_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_NV3 &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.reserve_offset != 0ul &&
        v9x_hal->fb.linear_base != 0ul &&
        v9x_hal->fb.bits_per_pixel == 16ul;
}

/* The four formats the object names (nv3_3d.h), by their masks. Only
 * A1R5G5B5 was drawn. */
static int v9x_d3d_nv3_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (surface == 0 || surface->lpGbl == 0 || format_out == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul ||
        format->dwRGBBitCount != 16ul) {
        return 0;
    }
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
        if (format->dwRBitMask == 0x0000f800ul &&
            format->dwGBitMask == 0x000007e0ul &&
            format->dwBBitMask == 0x0000001ful) {
            *format_out = V9X_NV3_TEX_R5G6B5;
            return 1;
        }
        if (format->dwRBitMask == 0x00007c00ul &&
            format->dwGBitMask == 0x000003e0ul &&
            format->dwBBitMask == 0x0000001ful) {
            *format_out = V9X_NV3_TEX_X1R5G5B5;
            return 1;
        }
        return 0;
    }
    if (format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *format_out = V9X_NV3_TEX_A1R5G5B5;
        return 1;
    }
    if (format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *format_out = V9X_NV3_TEX_A4R4G4B4;
        return 1;
    }
    return 0;
}

/*
 * What the object does, by the state record. No specular colour (the HAL
 * adds it, v9x_d3d_nv3_specular_pass, but does not claim it); vertex
 * fog only (table fog is emulated per vertex in the core); one texture
 * level; the blend pairs nv3_3d.h maps. The core culls.
 */
static void v9x_d3d_nv3_describe_caps(V9X_DD_SHARED *shared)
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
                      V9X_D3DPMISCCAPS_CULLCCW;
    /* No SUBPIXEL: coverage drops the vertex's fraction (edge cases). */
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_ZTEST |
                        V9X_D3DPRASTERCAPS_FOGVERTEX |
                        V9X_D3DPRASTERCAPS_FOGTABLE;
    /* LESS and GREATER drawn; the rest are the same field's values. */
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    /* ONE/ZERO, SRCALPHA/INVSRCALPHA, ONE/ONE, SRCALPHA/ONE, DESTCOLOR/ZERO
     * and ZERO/SRCCOLOR (v9x_d3d_nv3_blend). */
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_DESTCOLOR;
    tri->dwDestBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_INVSRCALPHA | V9X_D3DPBLENDCAPS_SRCCOLOR;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    /* Mipmaps to 8x8, the nearest level; no blend between levels. */
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR;
    /* DECALALPHA in two passes where it has to be (v9x_d3d_nv3_map). */
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    /* WRAP drawn; MIRROR and CLAMP are the same field's values. */
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_nv3_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_nv3_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_nv3_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_nv3_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* ARGB1555 (drawn), RGB565 and ARGB4444: the shared block has three
     * slots. */
    for (index = 0ul; index < 3ul; ++index) {
        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddpfPixelFormat.dwSize =
            sizeof(V9X_DDPIXELFORMAT);
        shared->texture_formats[index].ddpfPixelFormat.dwRGBBitCount = 16ul;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[1].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 3ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

static DWORD v9x_d3d_nv3_log2(DWORD value)
{
    DWORD log2 = 0ul;

    while ((1ul << log2) < value) {
        ++log2;
    }
    return log2;
}

static V9X_D3D_NV3_TEXTURE *v9x_d3d_nv3_find(DWORD linear)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_NV3_TEXTURES; ++index) {
        if (v9x_d3d_nv3_textures[index].linear == linear &&
            linear != 0ul) {
            return &v9x_d3d_nv3_textures[index];
        }
    }
    return 0;
}

static V9X_D3D_NV3_TEXTURE *v9x_d3d_nv3_find_surface(
    const V9X_DD_SURFACE_LCL *surface)
{
    DWORD offset;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return 0;
    }
    return v9x_d3d_nv3_find(offset);
}

/*
 * The swizzled square copy, rebuilt from the linear levels: every texel
 * of each level's square, (u, v) taken from (u mod width, v mod height),
 * so a narrower texture repeats across it, the levels one after another.
 * The engine is idle (the caller has begun the batch), so nothing reads
 * the copy meanwhile. `opaque` names the copy (V9X_D3D_NV3_OPAQUE_*).
 */
static void v9x_d3d_nv3_swizzle(V9X_D3D_NV3_TEXTURE *texture, DWORD opaque)
{
    v9x_u16 row[V9X_D3D_NV3_TEXTURE_MAX];
    const v9x_u16 *source;
    v9x_u16 *copy;
    DWORD level;
    DWORD log2_side;
    DWORD side;
    DWORD width;
    DWORD height;
    DWORD u;
    DWORD v;

    copy = (v9x_u16 *)(v9x_hal->fb.linear_base +
                       (opaque ? texture->opaque : texture->swizzled));
    for (level = 0ul; level < texture->levels; ++level) {
        log2_side = texture->log2_side - level;
        side = 1ul << log2_side;
        width = texture->width >> level;
        height = texture->height >> level;
        for (v = 0ul; v < side; ++v) {
            /* A row is read once into the buffer: video memory reads are
             * slow, and each texel is written side / width times. */
            source = (const v9x_u16 *)(v9x_hal->fb.linear_base +
                                       texture->level_linear[level] +
                                       (v % height) *
                                       texture->level_pitch[level]);
            for (u = 0ul; u < width; ++u) {
                if (opaque == V9X_D3D_NV3_OPAQUE_NONE) {
                    row[u] = source[u];
                } else if (opaque == V9X_D3D_NV3_OPAQUE_PLAIN) {
                    row[u] = v9x_nv3_3d_opaque_texel(source[u]);
                } else {
                    row[u] = v9x_nv3_3d_tested_texel(source[u],
                                                     (opaque >> 8) & 0xfful,
                                                     opaque & 0xfful);
                }
            }
            for (u = 0ul; u < side; ++u) {
                copy[v9x_nv3_3d_swizzle(u, v, log2_side)] = row[u % width];
            }
        }
        copy += side * side;
    }
    v9x_wc_drain();
    if (opaque != V9X_D3D_NV3_OPAQUE_NONE) {
        texture->opaque_dirty = 0;
        texture->opaque_test = opaque;
    } else {
        texture->dirty = 0;
    }
}

/*
 * Whether every texel of level 0 has alpha 1, read once after each write
 * (Unlock, Blt) and kept. SRCALPHA/ONE folds only the vertex alpha into
 * the colour, so a texel alpha below 1 would add too much.
 */
static DWORD v9x_d3d_nv3_alpha_state(V9X_D3D_NV3_TEXTURE *texture)
{
    const v9x_u16 *row;
    v9x_u16 mask;
    v9x_u16 bits;
    v9x_u16 first;
    DWORD u;
    DWORD v;

    if (texture->alpha_full == V9X_D3D_NV3_ALPHA_UNKNOWN) {
        mask = texture->format == V9X_NV3_TEX_A4R4G4B4 ? 0xf000u : 0x8000u;
        first = (v9x_u16)(*(const v9x_u16 *)(v9x_hal->fb.linear_base +
                                             texture->linear) & mask);
        texture->alpha_full = first == mask ? V9X_D3D_NV3_ALPHA_ALL_ONE :
                              first == 0u ? V9X_D3D_NV3_ALPHA_ALL_ZERO :
                              V9X_D3D_NV3_ALPHA_PARTIAL;
        for (v = 0ul; v < texture->height &&
             texture->alpha_full != V9X_D3D_NV3_ALPHA_PARTIAL; ++v) {
            row = (const v9x_u16 *)(v9x_hal->fb.linear_base +
                                    texture->linear + v * texture->pitch);
            for (u = 0ul; u < texture->width; ++u) {
                bits = (v9x_u16)(row[u] & mask);
                if (bits != first) {
                    texture->alpha_full = V9X_D3D_NV3_ALPHA_PARTIAL;
                    break;
                }
            }
        }
    }
    return texture->alpha_full;
}

static int v9x_d3d_nv3_alpha_all_one(V9X_D3D_NV3_TEXTURE *texture)
{
    return v9x_d3d_nv3_alpha_state(texture) == V9X_D3D_NV3_ALPHA_ALL_ONE;
}

/*
 * Direct3D's source colour key in the texel the chip compares, or 0 when
 * no one value keys exactly what Direct3D would. The chip compares all 16
 * bits of the texel it reads, alpha included (V9XDDP ColorKey, boot 30:
 * key 7C1Fh left FC1Fh texels drawn); Direct3D compares the colour alone.
 * - The A4R4G4B4 opaque copy: the key converted as its texels were. In
 *   the alpha-tested copy with top bit 1: texels with 0 fail the test
 *   and draw nothing whatever the key, so keying the passing ones is
 *   exact.
 * - R5G6B5: the key as given.
 * - A1R5G5B5 and X1R5G5B5: the colour with the top bit the texels all
 *   share. Under the source alpha blend alpha-0 texels draw nothing, so
 *   keying only the alpha-1 ones is exact there whatever the mix.
 * - A4R4G4B4 blended: the colour with alpha F, exact when all alphas are.
 */
static int v9x_d3d_nv3_key(V9X_D3D_NV3_TEXTURE *texture, DWORD opaque,
                           v9x_u32 blend, DWORD key, v9x_u32 *texel)
{
    DWORD alpha;

    if (opaque != V9X_D3D_NV3_OPAQUE_NONE) {
        *texel = v9x_nv3_3d_opaque_texel((v9x_u16)(key & 0xfffful));
        if (opaque != V9X_D3D_NV3_OPAQUE_PLAIN) {
            *texel |= 0x8000ul;
        }
        return 1;
    }
    if (texture->format == V9X_NV3_TEX_R5G6B5) {
        *texel = key & 0xfffful;
        return 1;
    }
    alpha = v9x_d3d_nv3_alpha_state(texture);
    if (texture->format == V9X_NV3_TEX_A4R4G4B4) {
        *texel = (key & 0x0ffful) | 0xf000ul;
        return alpha == V9X_D3D_NV3_ALPHA_ALL_ONE;
    }
    if (alpha == V9X_D3D_NV3_ALPHA_ALL_ZERO) {
        *texel = key & 0x7ffful;
        return 1;
    }
    *texel = (key & 0x7ffful) | 0x8000ul;
    return alpha == V9X_D3D_NV3_ALPHA_ALL_ONE ||
           (blend == V9X_NV3_BLEND_SRC_ALPHA &&
            texture->format == V9X_NV3_TEX_A1R5G5B5);
}

static void v9x_d3d_nv3_count_refusal(const V9X_R3D_DRAW *draw,
                                      DWORD reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_NV3_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_NV3_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/*
 * Why the builder refused a state, by the checks it makes, in policy
 * slots 11-16 beside reason 8 (V9XTRACE M64PolicyNN). For the last one
 * of no named cause, its texture in M64Degenerate (format, side, u and v
 * shifts, a byte each) and M64FogDraws (offset). Boot 23 refused 96.
 */
#define V9X_D3D_NV3_STATE_OPAQUE_4444  11ul
#define V9X_D3D_NV3_STATE_TEXTURE      12ul
#define V9X_D3D_NV3_STATE_Z_FUNC       13ul
#define V9X_D3D_NV3_STATE_ALPHA        14ul
#define V9X_D3D_NV3_STATE_SHIFT        15ul
#define V9X_D3D_NV3_STATE_OTHER        16ul

static void v9x_d3d_nv3_count_state_refusal(
    const struct v9x_nv3_3d_state *state)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;
    DWORD cause = V9X_D3D_NV3_STATE_OTHER;
    DWORD bytes;

    bytes = state->texture_log2 <= V9X_NV3_TEX_LOG2_MAX ?
            v9x_nv3_3d_chain_bytes(state->texture_log2,
                                   state->texture_levels) : 0ul;
    if (state->blend == V9X_NV3_BLEND_OPAQUE &&
        state->texture_format == V9X_NV3_TEX_A4R4G4B4) {
        cause = V9X_D3D_NV3_STATE_OPAQUE_4444;
    } else if (bytes == 0ul || state->texture_offset >= state->vram_bytes ||
               bytes > state->vram_bytes - state->texture_offset) {
        cause = V9X_D3D_NV3_STATE_TEXTURE;
    } else if (state->z_enable &&
               (state->z_func < V9X_NV3_CMP_NEVER ||
                state->z_func > V9X_NV3_CMP_ALWAYS)) {
        cause = V9X_D3D_NV3_STATE_Z_FUNC;
    } else if (state->alpha_test &&
               (state->alpha_func < V9X_NV3_CMP_NEVER ||
                state->alpha_func > V9X_NV3_CMP_ALWAYS)) {
        cause = V9X_D3D_NV3_STATE_ALPHA;
    } else if (state->u_shift > state->texture_log2 ||
               state->v_shift > state->texture_log2) {
        cause = V9X_D3D_NV3_STATE_SHIFT;
    }
    ++diagnostics->m64_policy_counts[cause];
    if (cause == V9X_D3D_NV3_STATE_OTHER) {
        diagnostics->m64_degenerate = (state->texture_format << 24) |
                                      (state->texture_log2 << 16) |
                                      (state->u_shift << 8) |
                                      state->v_shift;
        diagnostics->m64_fog_draws = state->texture_offset;
    }
}

/* Direct3D's blend pair as one the chip has, or 0. */
static int v9x_d3d_nv3_blend(const V9X_R3D_DRAW *draw, v9x_u32 *blend)
{
    v9x_u32 src = draw->src_blend;
    v9x_u32 dst = draw->dst_blend;

    if (!draw->blend_enable ||
        (src == V9X_D3DBLEND_ONE && dst == V9X_D3DBLEND_ZERO)) {
        *blend = V9X_NV3_BLEND_OPAQUE;
        return 1;
    }
    if (src == V9X_D3DBLEND_SRCALPHA && dst == V9X_D3DBLEND_INVSRCALPHA) {
        *blend = V9X_NV3_BLEND_SRC_ALPHA;
        return 1;
    }
    if (src == V9X_D3DBLEND_ONE && dst == V9X_D3DBLEND_ONE) {
        *blend = V9X_NV3_BLEND_ADD;
        return 1;
    }
    /*
     * ONE/INVSRCCOLOR, Unreal Tournament's translucency (5,488 of 37,035
     * batches refused on M-7V636C's first run, 2026-11-04), has no
     * counterpart: the chip scales the destination by the source alpha or
     * the destination colour, never by the source colour per channel. It
     * is drawn as the saturating add, src + dst for src + dst x (1 - src):
     * the same where either is black, brighter where both are bright. An
     * approximation by Michael's choice (2026-11-04), as the Matrox HAL's
     * additive key: refused, the effects were not drawn at all. Not in the
     * blend caps.
     */
    if (src == V9X_D3DBLEND_ONE && dst == V9X_D3DBLEND_INVSRCCOLOR) {
        *blend = V9X_NV3_BLEND_ADD;
        return 1;
    }
    /* Half-Life's glows and sprites; v9x_d3d_nv3_map checks the texture. */
    if (src == V9X_D3DBLEND_SRCALPHA && dst == V9X_D3DBLEND_ONE) {
        *blend = V9X_NV3_BLEND_ADD_SRC_ALPHA;
        return 1;
    }
    if ((src == V9X_D3DBLEND_DESTCOLOR && dst == V9X_D3DBLEND_ZERO) ||
        (src == V9X_D3DBLEND_ZERO && dst == V9X_D3DBLEND_SRCCOLOR)) {
        *blend = V9X_NV3_BLEND_MODULATE;
        return 1;
    }
    return 0;
}

/*
 * The draw in the builder's terms, and the texture it samples (null for
 * the white one). Nothing is written; a non-zero reason refuses the draw.
 */
static DWORD v9x_d3d_nv3_map(const V9X_R3D_DRAW *draw,
                             struct v9x_nv3_3d_target *target,
                             struct v9x_nv3_3d_state *state,
                             V9X_D3D_NV3_TEXTURE **sampled,
                             DWORD *use_opaque, int *decal_alpha)
{
    const V9X_DD_SURFACE_LCL *surface;
    V9X_D3D_NV3_TEXTURE *texture;
    v9x_u32 blend;
    DWORD side;
    int decal_alpha_op = 0;

    *sampled = 0;
    *use_opaque = V9X_D3D_NV3_OPAQUE_NONE;
    *decal_alpha = 0;
    if (draw->texcoords1 != 0 || draw->texture1.object != 0) {
        return V9X_D3D_NV3_REFUSE_EXPLICIT;
    }
    /*
     * The render interface's draws (OpenGL, Glide, 3DCIF) state a scissor
     * and a channel mask, and may carry texture levels in CPU memory.
     * Nothing here programs a scissor or a mask, and the chip samples only
     * surfaces placed by v9x_d3d_nv3_create_surface. So an explicit draw is
     * drawn when it changes nothing Direct3D's would not: the whole target,
     * every channel, a surface texture or none, as the ViRGE takes them.
     * CPU levels with no surface must be refused here, not read below as
     * untextured and drawn white. Until 2026-11-07 every explicit draw was
     * refused, and with no fallback for this chip Glide and 3DCIF drew
     * nothing (docs\issues\2026-11-07-riva128-render-interface-has-no-
     * engine.md).
     */
    if (draw->explicit_state != 0ul &&
        (draw->texture.levels != 0 || draw->texture.level_count != 0ul ||
         draw->write_mask != V9X_D3D_NV3_WRITE_RGB ||
         draw->scissor_left != 0ul || draw->scissor_top != 0ul ||
         draw->scissor_right != draw->target.width ||
         draw->scissor_bottom != draw->target.height)) {
        return V9X_D3D_NV3_REFUSE_EXPLICIT;
    }
    if (draw->target.format != V9X_R3D_FORMAT_RGB565 &&
        draw->target.format != V9X_R3D_FORMAT_XRGB1555) {
        return V9X_D3D_NV3_REFUSE_TARGET;
    }
    if (draw->shade_mode != V9X_R3D_SHADE_FLAT &&
        draw->shade_mode != V9X_R3D_SHADE_GOURAUD) {
        return V9X_D3D_NV3_REFUSE_SHADE;
    }
    if (!v9x_d3d_nv3_blend(draw, &blend)) {
        return V9X_D3D_NV3_REFUSE_BLEND;
    }

    /* With no depth buffer the zeta surface is the target's, never read
     * or written: Z is ALWAYS and its writes NEVER. */
    target->vram_bytes = v9x_hal->fb.vram_bytes;
    target->colour_offset = draw->target.offset;
    target->colour_pitch_bytes = draw->target.pitch;
    target->zeta_offset = draw->target.offset;
    target->zeta_pitch_bytes = draw->target.pitch;
    target->width = draw->target.width;
    target->height = draw->target.height;
    if (draw->depth.object != 0) {
        target->zeta_offset = draw->depth.offset;
        target->zeta_pitch_bytes = draw->depth.pitch;
    }

    /* RAMIN included: the white texture is there (eng_nv3.c). */
    state->vram_bytes = v9x_nv3_3d_vram_bytes();
    state->colour_key = 0;
    state->colour_key_texel = 0ul;
    state->filter = V9X_NV3_FILTER_POINT;
    state->wrap_u = V9X_NV3_WRAP;
    state->wrap_v = V9X_NV3_WRAP;
    state->z_enable = draw->depth_enable != 0ul && draw->depth.object != 0;
    state->z_write = draw->depth_write != 0ul;
    state->z_func = draw->depth_func;
    state->alpha_test = draw->alpha_test_enable != 0ul;
    state->alpha_func = draw->alpha_func;
    state->alpha_reference = draw->alpha_ref & 0xfful;
    state->blend = blend;
    state->fog = draw->fog_enable != 0ul;
    state->fog_colour = draw->fog_color;
    state->u_shift = 0ul;
    state->v_shift = 0ul;
    state->vertex_white = 0;
    state->texture_levels = 1ul;

    surface = (const V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0) {
        /* Untextured: the white texture, sampled point, modulated. */
        state->texture_offset = v9x_hal->engine.reserve_offset;
        state->texture_format = V9X_NV3_TEX_A1R5G5B5;
        state->texture_log2 = V9X_NV3_D3D_WHITE_LOG2;
        return V9X_D3D_NV3_REFUSE_NONE;
    }
    texture = v9x_d3d_nv3_find_surface(surface);
    if (texture == 0) {
        return V9X_D3D_NV3_REFUSE_TEXTURE;
    }
    switch (draw->texture.op) {
    case V9X_R3D_TEXOP_MODULATE:
    case V9X_R3D_TEXOP_MODULATEALPHA:
        break;
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        state->vertex_white = 1;
        break;
    case V9X_R3D_TEXOP_DECALALPHA:
        state->vertex_white = 1;
        decal_alpha_op = 1;
        break;
    default:
        return V9X_D3D_NV3_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no function here. */
    if (draw->texture.wrap_u != 0ul || draw->texture.wrap_v != 0ul ||
        draw->texture.address < V9X_R3D_ADDRESS_WRAP ||
        draw->texture.address > V9X_R3D_ADDRESS_CLAMP) {
        return V9X_D3D_NV3_REFUSE_ADDRESS;
    }
    state->wrap_u = draw->texture.address;
    state->wrap_v = draw->texture.address;
    /* One level: a mip filter takes its within-level half. */
    switch (draw->texture.mag_filter) {
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        state->filter = V9X_NV3_FILTER_LINEAR;
        break;
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        break;
    default:
        return V9X_D3D_NV3_REFUSE_FILTER;
    }
    if (blend == V9X_NV3_BLEND_ADD_SRC_ALPHA && texture->has_alpha &&
        !v9x_d3d_nv3_alpha_all_one(texture)) {
        return V9X_D3D_NV3_REFUSE_BLEND;
    }
    /*
     * DECALALPHA: vertex x (1 - At) + texel x At, alpha the vertex's. A
     * texture whose alphas are all 1, or that has none, makes it DECAL.
     * Otherwise it is two passes a triangle (v9x_d3d_nv3_draw): the vertex
     * colour opaque, then the texel over it by the source alpha blend with
     * the vertex white - this state. Only where the draw replaces the
     * pixel and covers every pixel the first pass did: no blend, no alpha
     * test, no key. Fog is linear, so the two fogged passes sum to the
     * fogged combine.
     */
    if (decal_alpha_op && texture->has_alpha &&
        !v9x_d3d_nv3_alpha_all_one(texture)) {
        if (blend != V9X_NV3_BLEND_OPAQUE || state->alpha_test ||
            (draw->color_key_enable != 0ul &&
             (surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul)) {
            return V9X_D3D_NV3_REFUSE_TEXTURE_OP;
        }
        blend = V9X_NV3_BLEND_SRC_ALPHA;
        state->blend = blend;
        *decal_alpha = 1;
    }
    side = 1ul << texture->log2_side;
    state->texture_offset = texture->swizzled;
    state->texture_format = texture->format;
    /*
     * Opaque from A4R4G4B4: the alpha-free copy. Under the alpha test, the
     * copy carries the test's verdict as A1R5G5B5's alpha, and the chip
     * tests that bit: the builder forces the vertex alpha to FFh, so what
     * passes is drawn whole and the blend sees no partial alpha. Until
     * 2026-11-07 the alpha-tested draw was refused (Half-Life, 0.3% of
     * its batches, boot 27). As with A1R5G5B5 under the test, the vertex
     * alpha takes no part in it.
     */
    if (blend == V9X_NV3_BLEND_OPAQUE && texture->opaque != 0ul) {
        state->texture_offset = texture->opaque;
        state->texture_format = V9X_NV3_TEX_X1R5G5B5;
        *use_opaque = V9X_D3D_NV3_OPAQUE_PLAIN;
        if (state->alpha_test) {
            *use_opaque = V9X_D3D_NV3_OPAQUE_TESTED |
                          ((state->alpha_func & 0xfful) << 8) |
                          state->alpha_reference;
            state->texture_format = V9X_NV3_TEX_A1R5G5B5;
            state->alpha_func = V9X_NV3_CMP_GREATER;
            state->alpha_reference = V9X_D3D_NV3_TESTED_REFERENCE;
        }
    }
    if (draw->color_key_enable != 0ul &&
        (surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        state->colour_key = 1;
        if (!v9x_d3d_nv3_key(texture, *use_opaque, blend,
                             surface->ddckCKSrcBltLow,
                             &state->colour_key_texel)) {
            return V9X_D3D_NV3_REFUSE_KEY;
        }
    }
    state->texture_log2 = texture->log2_side;
    /* A mip filter samples the chain, the level nearest the footprint;
     * the chip has no blend between two levels, so LINEARMIP* take the
     * nearest one too (V9XDDP's MipTriDegraded accepts that). */
    if (draw->texture.min_filter >= V9X_R3D_FILTER_MIPNEAREST &&
        draw->texture.min_filter <= V9X_R3D_FILTER_LINEARMIPLINEAR) {
        state->texture_levels = texture->levels;
    }
    state->u_shift = v9x_d3d_nv3_log2(side / texture->width);
    state->v_shift = v9x_d3d_nv3_log2(side / texture->height);
    *sampled = texture;
    return V9X_D3D_NV3_REFUSE_NONE;
}

static v9x_u32 v9x_d3d_nv3_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } pun;

    pun.f = value;
    return pun.u;
}

/* `centre` is taken off u and v: half a texel under bilinear, else 0
 * (v9x_d3d_nv3_draw). */
static void v9x_d3d_nv3_vertex_of(const V9X_R3D_VERTEX *from,
                                  const V9X_R3D_VERTEX *flat,
                                  const float *centre,
                                  struct v9x_nv3_3d_vertex *to)
{
    to->sx = v9x_d3d_nv3_bits(from->sx);
    to->sy = v9x_d3d_nv3_bits(from->sy);
    to->sz = v9x_d3d_nv3_bits(from->sz);
    to->rhw = v9x_d3d_nv3_bits(from->rhw);
    to->colour = flat->color;
    to->specular = flat->specular;
    to->tu = v9x_d3d_nv3_bits(from->tu - centre[0]);
    to->tv = v9x_d3d_nv3_bits(from->tv - centre[1]);
}

/*
 * The first accepted batch since the HAL loaded, as written, in the scan
 * pairs V9XTRACE dumps (ScanReg; only Intel's scanout fills them
 * otherwise): its target and clip, its state, its first vertex, and
 * PGRAPH's interrupt and trap registers read once it went idle. Boot 21
 * ran 425 batches that left nothing in their targets.
 */
#define V9X_D3D_NV3_RECORD_PAIRS 24ul
#define V9X_D3D_NV3_PGRAPH_INTR_0      0x400100ul
#define V9X_D3D_NV3_PGRAPH_INTR_1      0x400104ul
#define V9X_D3D_NV3_PGRAPH_TRAP_ADDR   0x4006b4ul
#define V9X_D3D_NV3_PGRAPH_TRAP_DATA   0x4006b8ul
static int v9x_d3d_nv3_recorded;

static void v9x_d3d_nv3_record(const struct v9x_nv3_write *writes,
                               DWORD count, DWORD *at)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;
    DWORD index;

    for (index = 0ul; index < count && *at < V9X_D3D_NV3_RECORD_PAIRS;
         ++index) {
        diagnostics->scan_reg_offset[*at] = writes[index].offset;
        diagnostics->scan_reg_value[*at] = writes[index].value;
        ++*at;
    }
}

static void v9x_d3d_nv3_record_after(DWORD *at)
{
    struct v9x_nv3_write reads[4];
    DWORD index;

    reads[0].offset = V9X_D3D_NV3_PGRAPH_INTR_0;
    reads[1].offset = V9X_D3D_NV3_PGRAPH_INTR_1;
    reads[2].offset = V9X_D3D_NV3_PGRAPH_TRAP_ADDR;
    reads[3].offset = V9X_D3D_NV3_PGRAPH_TRAP_DATA;
    for (index = 0ul; index < 4ul; ++index) {
        reads[index].value = v9x_d3d_nv3_read(reads[index].offset);
    }
    v9x_d3d_nv3_record(reads, 4ul, at);
}

static int v9x_d3d_nv3_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_nv3_3d_target target;
    struct v9x_nv3_3d_state state;
    struct v9x_nv3_3d_words words;
    struct v9x_nv3_op op;
    V9X_D3D_NV3_TEXTURE *texture;
    DWORD opaque;
    int decal_alpha;

    if (draw == 0 || !v9x_d3d_nv3_ready()) {
        return 0;
    }
    return v9x_d3d_nv3_map(draw, &target, &state, &texture, &opaque,
                           &decal_alpha) == V9X_D3D_NV3_REFUSE_NONE &&
           v9x_nv3_3d_build_target(&target, &op) == V9X_STATUS_OK &&
           v9x_nv3_3d_build_state(&state, &words) == V9X_STATUS_OK;
}

/* D3DCMP_EQUAL, which the chip compares in Direct3D's sense (nv3_3d.h). */
#define V9X_D3D_NV3_CMP_EQUAL 3ul

/* What a triangle's vertex colours are (v9x_d3d_nv3_triangle). */
#define V9X_D3D_NV3_COLOUR_DIFFUSE     0
#define V9X_D3D_NV3_COLOUR_PLUS_SPEC   1 /* untextured: specular folded */
#define V9X_D3D_NV3_COLOUR_SPECULAR    2 /* the specular pass */

/* One triangle's three vertices into slots 0, 1 and 2, the third drawing;
 * 0 when the FIFO has no room. */
static int v9x_d3d_nv3_triangle(const V9X_R3D_DRAW *draw,
                                const V9X_R3D_VERTEX *first,
                                const struct v9x_nv3_3d_words *words,
                                int colours, const float *centre,
                                DWORD *record_at)
{
    static const v9x_u32 v9x_d3d_nv3_others[2] = { 0ul, 1ul };
    struct v9x_nv3_3d_vertex vertex;
    struct v9x_nv3_write methods[V9X_NV3_D3D_VERTEX_METHODS];
    const V9X_R3D_VERTEX *corner;
    DWORD slot;

    for (slot = 0ul; slot < 3ul; ++slot) {
        corner = &first[slot];
        v9x_d3d_nv3_vertex_of(corner,
                              draw->shade_mode == V9X_R3D_SHADE_FLAT ?
                              first : corner, centre, &vertex);
        if (colours == V9X_D3D_NV3_COLOUR_PLUS_SPEC) {
            vertex.colour = v9x_nv3_3d_add_specular(vertex.colour,
                                                    vertex.specular);
        } else if (colours == V9X_D3D_NV3_COLOUR_SPECULAR) {
            /* Its alpha byte is the fog factor, and the add ignores it. */
            vertex.colour = (vertex.specular & 0x00fffffful) | 0xff000000ul;
        }
        if (v9x_nv3_3d_build_vertex(&vertex, slot,
                                    slot == 2ul ? v9x_d3d_nv3_others : 0,
                                    words, methods) != V9X_STATUS_OK ||
            !v9x_d3d_nv3_room(V9X_D3D_NV3_VERTEX_BYTES)) {
            return 0;
        }
        v9x_d3d_nv3_emit(methods, V9X_NV3_D3D_VERTEX_METHODS);
        if (record_at != 0 && slot == 0ul) {
            v9x_d3d_nv3_record(methods, V9X_NV3_D3D_VERTEX_METHODS,
                               record_at);
        }
    }
    return 1;
}

/* A state's texture made the white one, sampled plainly. */
static void v9x_d3d_nv3_untextured(struct v9x_nv3_3d_state *state)
{
    state->texture_offset = v9x_hal->engine.reserve_offset;
    state->texture_format = V9X_NV3_TEX_A1R5G5B5;
    state->texture_log2 = V9X_NV3_D3D_WHITE_LOG2;
    state->texture_levels = 1ul;
    state->filter = V9X_NV3_FILTER_POINT;
    state->wrap_u = V9X_NV3_WRAP;
    state->wrap_v = V9X_NV3_WRAP;
    state->u_shift = 0ul;
    state->v_shift = 0ul;
    state->vertex_white = 0;
}

/* A second pass over the pixels `first` drew: Z compared as it was, or
 * EQUAL where `first` wrote zeta, and zeta left alone. */
static void v9x_d3d_nv3_follow_z(struct v9x_nv3_3d_state *second,
                                 const struct v9x_nv3_3d_state *first)
{
    second->z_write = 0;
    if (first->z_enable && first->z_write) {
        second->z_func = V9X_D3D_NV3_CMP_EQUAL;
    }
}

/*
 * Direct3D adds the specular after the texture; the object has no input
 * for it. A textured batch gets a second pass per triangle: the white
 * texture, the specular as the vertex colour, the saturating add, over
 * the pixels the first pass drew (Z as it compared, EQUAL where it wrote
 * zeta). That sum is Direct3D's only where the first pass replaced the
 * pixel or added to it, drew every pixel of the triangle (no alpha test,
 * no key) and was not fogged (fog comes after the specular). Elsewhere
 * the specular is left out, as it always was here; refusing the batch
 * would lose the whole draw. 1 and `pass` built when it applies.
 */
static int v9x_d3d_nv3_specular_pass(const V9X_R3D_DRAW *draw,
                                     const V9X_R3D_VERTEX *vertices,
                                     DWORD triangle_count,
                                     const struct v9x_nv3_3d_state *state,
                                     struct v9x_nv3_3d_words *pass)
{
    struct v9x_nv3_3d_state second;
    DWORD index;

    if (draw->specular_enable == 0ul ||
        (state->blend != V9X_NV3_BLEND_OPAQUE &&
         state->blend != V9X_NV3_BLEND_ADD) ||
        state->alpha_test || state->colour_key || state->fog) {
        return 0;
    }
    for (index = 0ul; index < triangle_count * 3ul; ++index) {
        if ((vertices[index].specular & 0x00fffffful) != 0ul) {
            break;
        }
    }
    if (index == triangle_count * 3ul) {
        return 0;
    }

    second = *state;
    v9x_d3d_nv3_untextured(&second);
    second.blend = V9X_NV3_BLEND_ADD;
    v9x_d3d_nv3_follow_z(&second, state);
    return v9x_nv3_3d_build_state(&second, pass) == V9X_STATUS_OK;
}

/*
 * DECALALPHA's first pass (v9x_d3d_nv3_map): the vertex colour alone,
 * opaque, as the state the batch starts with; `state` becomes the second,
 * the texel over it, testing Z after the first. 1 and `first` built.
 */
static int v9x_d3d_nv3_decal_alpha_first(struct v9x_nv3_3d_state *state,
                                         struct v9x_nv3_3d_words *first)
{
    struct v9x_nv3_3d_state colour;

    colour = *state;
    v9x_d3d_nv3_untextured(&colour);
    colour.blend = V9X_NV3_BLEND_OPAQUE;
    colour.colour_key = 0;
    colour.colour_key_texel = 0ul;
    v9x_d3d_nv3_follow_z(state, &colour);
    return v9x_nv3_3d_build_state(&colour, first) == V9X_STATUS_OK;
}

/*
 * One batch: every word built before the first is written, so a refused
 * draw writes nothing; then the target, the clip, the binding (a bring-up
 * of the 16-bit side clears it), the state and, a triangle at a time, its
 * three vertices into slots 0, 1 and 2, the third drawing.
 */
static int v9x_d3d_nv3_draw(const V9X_R3D_DRAW *draw,
                            const V9X_R3D_VERTEX *vertices,
                            DWORD triangle_count)
{
    struct v9x_nv3_3d_target target;
    struct v9x_nv3_3d_state state;
    struct v9x_nv3_3d_words words;
    struct v9x_nv3_3d_words pass;
    struct v9x_nv3_3d_words first;
    const struct v9x_nv3_3d_words *lead;
    const struct v9x_nv3_3d_words *follow;
    struct v9x_nv3_op op;
    V9X_D3D_NV3_TEXTURE *texture;
    DWORD opaque;
    int decal_alpha;
    int colours;
    int follow_colours;
    float centre[2];
    DWORD triangle;
    DWORD reason;
    DWORD record_at;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (!v9x_d3d_nv3_ready()) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.m64_unrenderable;
        }
        return 0;
    }
    reason = v9x_d3d_nv3_map(draw, &target, &state, &texture, &opaque,
                             &decal_alpha);
    if (reason == V9X_D3D_NV3_REFUSE_NONE &&
        v9x_nv3_3d_build_target(&target, &op) != V9X_STATUS_OK) {
        /* The refused target's place, for the record: offset in
         * M64FogDropped, pitch and width in M64FogDecal. */
        v9x_hal->d3d_diagnostics.m64_fog_dropped = target.colour_offset;
        v9x_hal->d3d_diagnostics.m64_fog_decal =
            (target.colour_pitch_bytes << 16) | (target.width & 0xfffful);
        reason = V9X_D3D_NV3_REFUSE_SURFACE;
    }
    /* Ahead of the state, which it turns into DECALALPHA's second pass. */
    if (reason == V9X_D3D_NV3_REFUSE_NONE && decal_alpha &&
        !v9x_d3d_nv3_decal_alpha_first(&state, &first)) {
        reason = V9X_D3D_NV3_REFUSE_STATE;
        v9x_d3d_nv3_count_state_refusal(&state);
    }
    if (reason == V9X_D3D_NV3_REFUSE_NONE &&
        v9x_nv3_3d_build_state(&state, &words) != V9X_STATUS_OK) {
        reason = V9X_D3D_NV3_REFUSE_STATE;
        v9x_d3d_nv3_count_state_refusal(&state);
    }
    if (reason != V9X_D3D_NV3_REFUSE_NONE) {
        ++v9x_hal->d3d_diagnostics.m64_refused;
        v9x_d3d_nv3_count_refusal(draw, reason);
        return 0;
    }

    if (!v9x_nv3_3d_begin()) {
        v9x_hal->d3d_diagnostics.m64_refuse_last = V9X_D3D_NV3_STOP_BEGIN;
        return 0;
    }
    ++v9x_hal->d3d_diagnostics.m64_draws;
    v9x_hal->d3d_diagnostics.m64_triangles += triangle_count;
    if (texture != 0) {
        ++v9x_hal->d3d_diagnostics.m64_texture_draws;
    }
    if (state.z_enable) {
        ++v9x_hal->d3d_diagnostics.m64_depth_draws;
    }
    if (state.blend != V9X_NV3_BLEND_OPAQUE) {
        ++v9x_hal->d3d_diagnostics.m64_blend_draws;
    }
    /*
     * The opaque copy is rebuilt when written to or wanted with another
     * alpha test. A plain draw reads any copy as X1R5G5B5, top bit unused,
     * unless it is keyed: the chip's key compares that bit too.
     */
    if (texture != 0 && opaque != V9X_D3D_NV3_OPAQUE_NONE &&
        (texture->opaque_dirty ||
         (texture->opaque_test != opaque &&
          (opaque != V9X_D3D_NV3_OPAQUE_PLAIN || state.colour_key)))) {
        v9x_d3d_nv3_swizzle(texture, opaque);
    } else if (texture != 0 && opaque == V9X_D3D_NV3_OPAQUE_NONE &&
               texture->dirty) {
        v9x_d3d_nv3_swizzle(texture, V9X_D3D_NV3_OPAQUE_NONE);
    }
    /* The target, depth buffer and textures may hold CPU writes still in
     * the write-combining buffers. */
    v9x_wc_drain();
    v9x_d3d_nv3_emit(op.surface, op.surface_count);
    if (!v9x_d3d_nv3_room(V9X_D3D_NV3_STATE_BYTES)) {
        v9x_hal->d3d_diagnostics.m64_refuse_last =
            V9X_D3D_NV3_STOP_ROOM_STATE;
        return 0;
    }
    v9x_d3d_nv3_emit(op.methods, op.method_count);
    v9x_d3d_nv3_write(V9X_NV3_USER +
                      V9X_NV3_SUB_D3D * V9X_NV3_SUBCHANNEL_STEP,
                      V9X_NV3_D3D_HANDLE);
    lead = decal_alpha ? &first : &words;
    v9x_d3d_nv3_emit(lead->methods, 6ul);
    v9x_present_note_submission();
    record_at = 0ul;
    if (!v9x_d3d_nv3_recorded) {
        v9x_d3d_nv3_record(op.surface, op.surface_count, &record_at);
        v9x_d3d_nv3_record(op.methods, op.method_count, &record_at);
        v9x_d3d_nv3_record(lead->methods, 6ul, &record_at);
        v9x_hal->d3d_diagnostics.scan_sample_offset = target.colour_offset;
    }

    /* Untextured, the specular goes into the colours; textured, into a
     * second pass after each triangle, so a later triangle covers an
     * earlier one's specular as it covers the rest of it. DECALALPHA's
     * two passes go the same way, the vertex colour leading. */
    colours = V9X_D3D_NV3_COLOUR_DIFFUSE;
    follow = 0;
    follow_colours = V9X_D3D_NV3_COLOUR_DIFFUSE;
    if (texture == 0 && draw->specular_enable != 0ul) {
        colours = V9X_D3D_NV3_COLOUR_PLUS_SPEC;
    }
    if (decal_alpha) {
        follow = &words;
    } else if (texture != 0 &&
               v9x_d3d_nv3_specular_pass(draw, vertices, triangle_count,
                                         &state, &pass)) {
        follow = &pass;
        follow_colours = V9X_D3D_NV3_COLOUR_SPECULAR;
    }
    /*
     * Bilinear centres a texel at u = i / W, half a texel left (and up) of
     * Direct3D's (i + 0.5) / W (/d3dstate TexelFoh, 2026-11-03), so a
     * bilinear draw's coordinates move half of level 0's texel back. Under
     * a mip chain a smaller level is left a fraction off. The white
     * texture of the other passes is one colour, so it does not mind.
     */
    centre[0] = 0.0f;
    centre[1] = 0.0f;
    if (texture != 0 && state.filter == V9X_NV3_FILTER_LINEAR) {
        centre[0] = 0.5f / (float)texture->width;
        centre[1] = 0.5f / (float)texture->height;
    }
    for (triangle = 0ul; triangle < triangle_count; ++triangle) {
        if (follow != 0 && triangle != 0ul) {
            if (!v9x_d3d_nv3_room(V9X_D3D_NV3_STATE_BYTES)) {
                v9x_hal->d3d_diagnostics.m64_refuse_last =
                    V9X_D3D_NV3_STOP_ROOM_STATE;
                return 0;
            }
            v9x_d3d_nv3_emit(lead->methods, 6ul);
        }
        if (!v9x_d3d_nv3_triangle(draw, &vertices[triangle * 3ul], lead,
                                  colours, centre,
                                  !v9x_d3d_nv3_recorded && triangle == 0ul ?
                                  &record_at : 0)) {
            v9x_hal->d3d_diagnostics.m64_refuse_last =
                V9X_D3D_NV3_STOP_ROOM_VERTEX;
            return 0;
        }
        if (follow == 0) {
            continue;
        }
        if (!v9x_d3d_nv3_room(V9X_D3D_NV3_STATE_BYTES)) {
            v9x_hal->d3d_diagnostics.m64_refuse_last =
                V9X_D3D_NV3_STOP_ROOM_STATE;
            return 0;
        }
        v9x_d3d_nv3_emit(follow->methods, 6ul);
        if (!v9x_d3d_nv3_triangle(draw, &vertices[triangle * 3ul], follow,
                                  follow_colours, centre, 0)) {
            v9x_hal->d3d_diagnostics.m64_refuse_last =
                V9X_D3D_NV3_STOP_ROOM_VERTEX;
            return 0;
        }
    }
    if (!v9x_nv3_3d_idle()) {
        v9x_hal->d3d_diagnostics.m64_refuse_last = V9X_D3D_NV3_STOP_IDLE;
        return 0;
    }
    if (!v9x_d3d_nv3_recorded) {
        v9x_d3d_nv3_record_after(&record_at);
        v9x_d3d_nv3_recorded = 1;
    }
    return 1;
}

static int v9x_d3d_nv3_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul && edge >= 1ul &&
           edge <= V9X_D3D_NV3_TEXTURE_MAX;
}

static DWORD v9x_d3d_nv3_pitch(DWORD edge)
{
    return (edge * 2ul + V9X_D3D_NV3_PITCH_ALIGN - 1ul) &
           ~(V9X_D3D_NV3_PITCH_ALIGN - 1ul);
}

/*
 * A video-memory texture of a format and size this engine samples: its
 * levels linear, as DirectDraw would place them, each on the alignment,
 * and the swizzled copy of the levels the chip samples after them, in one
 * block.
 */
static DWORD v9x_d3d_nv3_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    V9X_D3D_NV3_TEXTURE *record;
    v9x_u32 offsets[V9X_D3D_NV3_TEXTURE_LEVELS];
    v9x_u32 pitches[V9X_D3D_NV3_TEXTURE_LEVELS];
    DWORD align = V9X_D3D_NV3_TEXTURE_ALIGN;
    DWORD format;
    DWORD width;
    DWORD height;
    DWORD log2_side;
    DWORD copy;
    DWORD opaque;
    DWORD bytes = 0ul;
    DWORD level;
    DWORD levels;
    DWORD base;
    DWORD place;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_NV3_TEXTURE_LEVELS ||
        data->lplpSList == 0 || !v9x_d3d_nv3_ready()) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & (V9X_DDSCAPS_SYSTEMMEMORY |
                             V9X_DDSCAPS_ZBUFFER)) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    if (!v9x_d3d_nv3_texture_edge(width) ||
        !v9x_d3d_nv3_texture_edge(height) ||
        !v9x_d3d_nv3_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    record = 0;
    for (level = 0ul; level < V9X_D3D_NV3_TEXTURES; ++level) {
        if (v9x_d3d_nv3_textures[level].linear == 0ul) {
            record = &v9x_d3d_nv3_textures[level];
            break;
        }
    }
    if (record == 0) {
        /* Out of video memory, not NOTHANDLED: see the placement below. */
        data->ddRVal = V9X_DDERR_OUTOFVIDEOMEMORY;
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *member = list[level];
        DWORD edge = width >> level;
        DWORD rows = height >> level;

        if (edge == 0ul && rows == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if (edge == 0ul) {
            edge = 1ul;
        }
        if (rows == 0ul) {
            rows = 1ul;
        }
        if (member == 0 || member->lpGbl == 0 ||
            (data->dwSCnt > 1ul &&
             (member->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) ||
            (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)member->lpGbl->wWidth != edge ||
            (DWORD)member->lpGbl->wHeight != rows) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[level] = (bytes + align - 1ul) & ~(align - 1ul);
        pitches[level] = v9x_d3d_nv3_pitch(edge);
        bytes = offsets[level] + pitches[level] * rows;
    }
    log2_side = v9x_d3d_nv3_log2(width > height ? width : height);

    /* The levels the chip can sample: each one both edges halved (an edge
     * stopped at 1 would change the copies' scale), and none under 8x8,
     * whose offsets the chip rounds onto the level before (nv3_3d.h). */
    levels = 1ul;
    while (levels < data->dwSCnt &&
           (width >> levels) != 0ul && (height >> levels) != 0ul &&
           log2_side - levels >= V9X_NV3_MIP_LOG2_MIN) {
        ++levels;
    }
    copy = (bytes + align - 1ul) & ~(align - 1ul);
    bytes = copy + v9x_nv3_3d_chain_bytes(log2_side, levels);
    opaque = 0ul;
    if (format == V9X_NV3_TEX_A4R4G4B4) {
        opaque = (bytes + align - 1ul) & ~(align - 1ul);
        bytes = opaque + v9x_nv3_3d_chain_bytes(log2_side, levels);
    }
    /*
     * No room is out of video memory, not NOTHANDLED. Declined, the texture
     * is placed by DirectDraw as linear levels alone, which other engines
     * sample but this one cannot: it samples only its swizzled copy, so
     * every draw with that texture was refused. The block here is about
     * twice DirectDraw's, so under pressure DirectDraw still finds room:
     * 3DMark 99 Max at 800x600x16 on M-7V636C had 3,701 of 15,430 textures
     * not placed here and 11,837 of 32,260 draws refused as unplaced
     * (boot 42, 2026-11-07). Told, the program manages its textures as it
     * would on any full card.
     */
    place = v9x_d3d_place_chain(data, align, bytes, offsets, pitches,
                                &base);
    if (place == V9X_D3D_PLACE_ALLOC) {
        data->ddRVal = V9X_DDERR_OUTOFVIDEOMEMORY;
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    if (place != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    record->linear = base + offsets[0];
    record->swizzled = base + copy;
    record->opaque = opaque != 0ul ? base + opaque : 0ul;
    record->opaque_dirty = 1;
    record->opaque_test = V9X_D3D_NV3_OPAQUE_PLAIN;
    record->pitch = pitches[0];
    record->levels = levels;
    for (level = 0ul; level < levels; ++level) {
        record->level_linear[level] = base + offsets[level];
        record->level_pitch[level] = pitches[level];
    }
    record->width = width;
    record->height = height;
    record->log2_side = log2_side;
    record->format = format;
    record->has_alpha = format == V9X_NV3_TEX_A1R5G5B5 ||
                        format == V9X_NV3_TEX_A4R4G4B4;
    record->dirty = 1;
    record->alpha_full = V9X_D3D_NV3_ALPHA_UNKNOWN;
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += bytes;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_nv3_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    V9X_D3D_NV3_TEXTURE *record = 0;

    if (data != 0 && v9x_hal != 0) {
        record = v9x_d3d_nv3_find_surface(
            (const V9X_DD_SURFACE_LCL *)data->lpDDSurface);
    }
    if (v9x_d3d_place_release(data, V9X_D3D_NV3_TEXTURE_ALIGN) &&
        record != 0) {
        record->linear = 0ul;
        record->dirty = 0;
        record->opaque = 0ul;
        record->opaque_dirty = 0;
    }
}

/* The placed texture one of whose sampled levels is at `linear`, or 0. */
static V9X_D3D_NV3_TEXTURE *v9x_d3d_nv3_find_level(DWORD linear)
{
    DWORD index;
    DWORD level;

    for (index = 0ul; index < V9X_D3D_NV3_TEXTURES; ++index) {
        if (v9x_d3d_nv3_textures[index].linear == 0ul) {
            continue;
        }
        for (level = 0ul; level < v9x_d3d_nv3_textures[index].levels;
             ++level) {
            if (v9x_d3d_nv3_textures[index].level_linear[level] == linear) {
                return &v9x_d3d_nv3_textures[index];
            }
        }
    }
    return 0;
}

/* An Unlock or a Blt into a placed texture, any level of it: its copy is
 * stale. */
static void v9x_d3d_nv3_surface_written(const V9X_DD_SURFACE_LCL *surface)
{
    V9X_D3D_NV3_TEXTURE *record;
    DWORD offset;

    if (v9x_hal == 0 || surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    record = offset != 0xfffffffful ? v9x_d3d_nv3_find_level(offset) : 0;
    if (record != 0) {
        record->dirty = 1;
        record->opaque_dirty = 1;
        record->alpha_full = V9X_D3D_NV3_ALPHA_UNKNOWN;
    }
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_nv3 = {
    &v9x_d3d_nv3_limits,
    v9x_d3d_nv3_texture_format,
    v9x_d3d_nv3_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_nv3_ready,
    v9x_d3d_nv3_create_surface,
    v9x_d3d_nv3_destroy_surface,
    v9x_d3d_nv3_draw,
    v9x_d3d_nv3_accepts,
    0,                                  /* draw_indexed */
    0,                                  /* draw_tnl */
    0,                                  /* tnl_available */
    0,                                  /* lock_surface */
    0,                                  /* unlock_surface */
    0,                                  /* draw_shader */
    v9x_d3d_nv3_surface_written
};
