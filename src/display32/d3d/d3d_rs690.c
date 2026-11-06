/*
 * ATI Radeon X1200 IGP (RS690) Direct3D engine, by register writes.
 *
 * Phase 5 of docs\plans\ati-rs690-x1200.md. The register values come from
 * src\chipsets\ati\rs690\rs690_3d.c and the translation from the neutral
 * draw from d3d_rs690_map.c, both host-tested; the probes drew the same
 * values right on M-GB8KRE at 16 and 32 bpp: Gouraud TL vertices, Z16,
 * blend, the alpha test, textures nearest and bilinear, perspective
 * (docs\decisions\2026-11-05-rs690-3d-draw-probe.md, -texture-probe.md,
 * -z-blend-probe.md). This file resolves DirectDraw surfaces, places
 * textures and Z buffers, and writes.
 *
 * There is no CP: every state and vertex dword is a register write over
 * BAR2, the vertices to SE_PORT_DATA0 behind VAP_VF_CNTL, each run of 16
 * behind a wait for that many free FIFO entries, as the probe wrote them.
 * eng_rs690.c owns the engine: its validation writes the 3D init once per
 * mode, and its idle wait (Lock, flips, blits) finds the drawing done,
 * because every batch ends in the 3D cache flushes and WAIT_UNTIL 3D
 * idle-clean.
 *
 * The vertices go as they are: V9X_R3D_VERTEX is the builder's TL vertex.
 * The core clips every triangle to the target (clip_in_core) and moves
 * Direct3D's pixel centres onto this engine's half-pixel ones; a triangle
 * with a coordinate that is not finite, or textured with an RHW at or
 * below zero, is left out.
 *
 * Emission order is the refusal contract: the mapping and the state are
 * built first, and only then is the first register written, so a refused
 * draw writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_rs690_map.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rs690_3d.h"

/* The register window the mini-VDD maps (eng_rs690.c's). */
#define V9X_D3D_RS690_MMIO_BYTES 0x00010000ul

/* Textures and Z placed 32-byte aligned (TX_OFFSET's rule) with 64-byte
 * pitches: the RS690 steps a linear texture's rows 64 bytes apart
 * whatever TX_FORMAT2 says (V9XD32's T6 texture on M-GB8KRE, 2026-11-05),
 * and Z16 takes Mesa's same 32-pixel alignment. */
#define V9X_D3D_RS690_SURFACE_ALIGN 32ul
#define V9X_D3D_RS690_PITCH_ALIGN   V9X_RS690_TEX_PITCH_ALIGN

/* Levels a placed chain may have: 2048 halves to 1 in eleven. Only level
 * 0 is sampled. */
#define V9X_D3D_RS690_TEXTURE_LEVELS 12ul

/* Triangles a VF_CNTL carries here; the register's count field holds far
 * more, this only bounds the scan for a run of good triangles. */
#define V9X_D3D_RS690_RUN_TRIANGLES 64ul

/* Writes between FIFO waits, as the probe wrote them. */
#define V9X_D3D_RS690_FIFO_BATCH 16ul

/* Bounded wait for FIFO room: the probe's waits took 2 to 34 reads. */
#define V9X_D3D_RS690_FIFO_SPINS 0x00200000ul

static struct v9x_rs690_3d_list v9x_d3d_rs690_list;
static DWORD v9x_d3d_rs690_fb_base;
static DWORD v9x_d3d_rs690_desktop;

/* 16 and 32 bpp targets; Z16; textures to 2048; the core clips, and moves
 * Direct3D's vertices half a pixel onto this rasterizer's centres, which
 * the probes found at x + 0.5 (Gouraud within 0.66 of a step against the
 * centre model, a texture drawn texel for texel from whole-pixel edges). */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_rs690_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max: COLORPITCH's 8-pixel units */
    16ul,           /* target_pitch_align: 8 pixels at 16 bpp */
    2048ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_RS690_SURFACE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core: no guard band measured */
    1ul,            /* depth_pitch_own: ZB_DEPTHPITCH takes the pitch */
    0ul,            /* depth_fill_shift: Z16 FFFFh is far */
    1ul,            /* texture_units */
    1ul,            /* pixel_centre_half */
    0ul,            /* depth_tiled: linear */
    32752ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits: Z16 only */
    0ul,            /* centre_in_engine: the core moves, as it clips */
    1ul             /* texture_stages */
};

static DWORD v9x_d3d_rs690_read(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_d3d_rs690_write(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

/* Non-zero once RBBM_STATUS shows at least `entries` free FIFO entries. */
static int v9x_d3d_rs690_room(DWORD entries)
{
    DWORD spins = V9X_D3D_RS690_FIFO_SPINS;

    while (spins-- != 0ul) {
        if ((v9x_d3d_rs690_read(V9X_RS690_RBBM_STATUS) &
             V9X_RS690_RBBM_FIFO_MASK) >= entries) {
            return 1;
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    return 0;
}

/* A list's writes, each run of 16 behind a wait for room. */
static int v9x_d3d_rs690_write_list(const struct v9x_rs690_3d_list *list)
{
    DWORD index;

    for (index = 0ul; index < list->count; ++index) {
        if (index % V9X_D3D_RS690_FIFO_BATCH == 0ul &&
            !v9x_d3d_rs690_room(V9X_D3D_RS690_FIFO_BATCH)) {
            return 0;
        }
        v9x_d3d_rs690_write(list->offsets[index], list->values[index]);
    }
    return 1;
}

/* RGB565, ARGB1555 and ARGB4444 at 16 bits, ARGB8888 at 32: the builder's
 * format, or 0 for one this engine does not sample. */
static DWORD v9x_d3d_rs690_format_of(const V9X_DD_SURFACE_LCL *surface,
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
                   V9X_RS690_TEXFMT_RGB565 : 0ul;
        }
        if (format->dwRBitMask == 0x00007c00ul &&
            format->dwGBitMask == 0x000003e0ul &&
            format->dwBBitMask == 0x0000001ful &&
            format->dwRGBAlphaBitMask == 0x00008000ul) {
            return V9X_RS690_TEXFMT_ARGB1555;
        }
        if (format->dwRBitMask == 0x00000f00ul &&
            format->dwGBitMask == 0x000000f0ul &&
            format->dwBBitMask == 0x0000000ful &&
            format->dwRGBAlphaBitMask == 0x0000f000ul) {
            return V9X_RS690_TEXFMT_ARGB4444;
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
        return V9X_RS690_TEXFMT_ARGB8888;
    }
    return 0ul;
}

static int v9x_d3d_rs690_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                        DWORD *format_out)
{
    DWORD bytes = 0ul;
    DWORD format;

    if (format_out == 0) {
        return 0;
    }
    format = v9x_d3d_rs690_format_of(surface, &bytes);
    *format_out = format != 0ul ? format : V9X_D3D_RS690_TEXTURE_UNKNOWN;
    return format != 0ul;
}

/*
 * What d3d_rs690_map.c accepts and the probes measured: Z and alpha tests
 * with every function (LESS and GREATER measured), the blend factors
 * without destination alpha (SRCALPHA/INVSRCALPHA measured), Gouraud,
 * textures to 2048 in four formats (RGB565 and ARGB8888 measured), nearest
 * and bilinear, wrap and clamp, perspective-correct; flat shading and
 * specular from Mesa's encodings; vertex fog in the shader (TlFog and
 * TlFogSpecular measured), and FOGTABLE emulated per vertex in the core
 * (d3d_fog.h).
 */
static void v9x_d3d_rs690_describe_caps(V9X_DD_SHARED *shared)
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
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    /* TRANSPARENCY: source colour keys on RGB565 textures by the texture
     * unit's chroma key; other formats' keyed draws are refused. */
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
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
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY |
                              V9X_D3DPTBLENDCAPS_ADD;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_rs690_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_rs690_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_rs690_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_rs690_limits.texture_size_max;
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

/* eng_rs690.c's conditions, CAP_D3D, and the window mapped. */
static int v9x_d3d_rs690_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RS690 &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_RS690_MMIO_BYTES;
}

/*
 * How many of a mip chain's levels lie where the chip will look for them:
 * level n at the base plus v9x_rs690_mip_offset, at v9x_rs690_mip_pitch
 * (the layout V9XRS3D measured; create_surface places chains so). The
 * walk follows each level's mipmap attachment and stops at the first that
 * is missing or elsewhere, recording it as the VI engine does (V9XTRACE
 * MipGapExpected, MipGapActual, D3dMipChainDelta).
 */
static void v9x_d3d_rs690_resolve_chain(const V9X_DD_SURFACE_LCL *surface,
                                        DWORD bytes,
                                        V9X_D3D_RS690_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *level = surface;
    DWORD longest = texture->width > texture->height ? texture->width
                                                     : texture->height;
    DWORD possible = 1ul;
    DWORD found = 1ul;

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    while (longest > 1ul) {
        longest >>= 1;
        ++possible;
    }
    if (texture->pitch_bytes !=
        v9x_rs690_mip_pitch(texture->width, bytes, 0ul)) {
        return;
    }
    while (found < possible) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD expected = texture->offset +
                         v9x_rs690_mip_offset(texture->width, texture->height,
                                              bytes, found);
        DWORD pitch = v9x_rs690_mip_pitch(texture->width, bytes, found);

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0 || next->lpGbl == 0 ||
            v9x_surface_offset(next) != expected ||
            (DWORD)next->lpGbl->lPitch != pitch) {
            if (next != 0) {
                ++v9x_hal->d3d_diagnostics.mip_gap_shape;
                v9x_hal->d3d_diagnostics.mip_gap_expected = expected;
                v9x_hal->d3d_diagnostics.mip_gap_actual =
                    next->lpGbl != 0 ? v9x_surface_offset(next) : 0ul;
                v9x_hal->d3d_diagnostics.mip_chain_delta =
                    (next->lpGbl != 0 ?
                     ((DWORD)next->lpGbl->lPitch << 16) : 0ul) |
                    (pitch & 0xfffful);
            }
            break;
        }
        ++found;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = found;
    texture->levels = found;
}

/* The bound texture's level 0: format, shape, pitch and offset. */
static void v9x_d3d_rs690_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                          V9X_D3D_RS690_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface;
    DWORD bytes = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_RS690_TEXTURE_UNKNOWN;
    texture->has_alpha = 0;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->pitch_bytes = 0ul;
    texture->offset = 0ul;
    texture->has_key = 0;
    texture->key = 0ul;
    texture->levels = 0ul;

    surface = (const V9X_DD_SURFACE_LCL *)sampler->object;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l) {
        return;
    }
    texture->format = v9x_d3d_rs690_format_of(surface, &bytes);
    if (texture->format == 0ul) {
        texture->format = V9X_D3D_RS690_TEXTURE_UNKNOWN;
        return;
    }
    texture->has_alpha = texture->format != V9X_RS690_TEXFMT_RGB565;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->offset = offset;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1;
        texture->key = surface->ddckCKSrcBltLow;
    }
    texture->levels = 1ul;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        v9x_d3d_rs690_resolve_chain(surface, bytes, texture);
    }

    /* The ViRGE's instrument (d3d_virge.c): where the texture is, and its
     * first 16 bits at the corner and the centre texel, read from the
     * memory the engine samples - "reads the wrong place" against "nobody
     * wrote this". */
    {
        const volatile BYTE *base =
            (const volatile BYTE *)(v9x_hal->fb.linear_base + offset);
        const volatile WORD *corner = (const volatile WORD *)base;
        const volatile WORD *centre = (const volatile WORD *)
            (base + (texture->height / 2ul) * texture->pitch_bytes +
             (texture->width / 2ul) * bytes);

        v9x_hal->d3d_diagnostics.texture_last_offset = offset;
        v9x_hal->d3d_diagnostics.texture_last_size = texture->width;
        v9x_hal->d3d_diagnostics.texture_last_caps = surface->ddsCaps;
        v9x_hal->d3d_diagnostics.texture_last_texels =
            ((DWORD)*centre << 16) | (DWORD)*corner;
        /* The format resolved and the pitch, in M64FogDecal: this engine
         * draws no fog, and the RIVA 128's records use the field so. */
        v9x_hal->d3d_diagnostics.m64_fog_decal =
            (texture->format << 16) | (texture->pitch_bytes & 0xfffful);
    }
}

/* Z16 when a depth surface is bound: its bytes per pixel, 0 for none. */
static DWORD v9x_d3d_rs690_depth_bytes(const V9X_R3D_DRAW *draw)
{
    const V9X_DD_SURFACE_LCL *depth =
        (const V9X_DD_SURFACE_LCL *)draw->depth.object;

    if (depth == 0) {
        return 0ul;
    }
    return v9x_d3d_depth_bytes_per_pixel(depth);
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the Rage 128 counts its own. */
static void v9x_d3d_rs690_count_refusal(const V9X_R3D_DRAW *draw,
                                        v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_RS690_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_RS690_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/* The mapping, then the state into v9x_d3d_rs690_list. */
static v9x_u32 v9x_d3d_rs690_map(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_RS690_TEXTURE resolved;
    struct v9x_rs690_3d_state state;
    v9x_u32 reason;

    v9x_d3d_rs690_resolve_texture(&draw->texture, &resolved);
    reason = v9x_d3d_rs690_map_draw(draw, &resolved, v9x_d3d_rs690_fb_base,
                                    v9x_d3d_rs690_desktop,
                                    v9x_hal->fb.vram_bytes,
                                    v9x_d3d_rs690_depth_bytes(draw), &state);
    if (reason == V9X_D3D_RS690_REFUSE_NONE &&
        v9x_rs690_build_3d_state(&state, &v9x_d3d_rs690_list) !=
            V9X_STATUS_OK) {
        reason = V9X_D3D_RS690_REFUSE_STATE;
    }
    /* The colour MC addresses mapped draws write, lowest and highest, and
     * the base of the last, in fields this engine does not otherwise use
     * (M64FogDropped, ScanSampleOffset, M64RefuseLast): the T15 hunt
     * (docs\issues\2026-11-05-rs690-t15-heap-words-zeroed-once.md) needs
     * them from a second process. */
    if (reason == V9X_D3D_RS690_REFUSE_NONE) {
        V9X_D3D_DIAGNOSTICS *record = &v9x_hal->d3d_diagnostics;
        DWORD colour = state.target.fb_gpu_base + state.target.offset;

        if (record->m64_fog_dropped == 0ul || colour < record->m64_fog_dropped) {
            record->m64_fog_dropped = colour;
        }
        if (colour > record->scan_sample_offset) {
            record->scan_sample_offset = colour;
        }
        record->m64_refuse_last = state.target.fb_gpu_base;
    }
    return reason;
}

static int v9x_d3d_rs690_accepts(const V9X_R3D_DRAW *draw)
{
    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    return v9x_d3d_rs690_map(draw) == V9X_D3D_RS690_REFUSE_NONE;
}

/* The vertex checks, on the floats' bits, as the Rage 128 makes them. */
#define V9X_D3D_RS690_SIGN_BIT      0x80000000ul
#define V9X_D3D_RS690_EXPONENT_MASK 0x7f800000ul

static int v9x_d3d_rs690_finite(DWORD bits)
{
    return (bits & V9X_D3D_RS690_EXPONENT_MASK) !=
           V9X_D3D_RS690_EXPONENT_MASK;
}

/* A triangle the engine takes as it is: every coordinate finite, and rhw
 * positive with finite texture coordinates when textured. */
static int v9x_d3d_rs690_triangle_ok(const V9X_R3D_VERTEX *triangle,
                                     int textured)
{
    DWORD corner;

    for (corner = 0ul; corner < 3ul; ++corner) {
        const DWORD *words = (const DWORD *)&triangle[corner];
        DWORD rhw = words[3];

        if (!v9x_d3d_rs690_finite(words[0]) ||
            !v9x_d3d_rs690_finite(words[1]) ||
            !v9x_d3d_rs690_finite(words[2]) || !v9x_d3d_rs690_finite(rhw)) {
            return 0;
        }
        if (textured &&
            ((rhw & V9X_D3D_RS690_SIGN_BIT) != 0ul || rhw == 0ul ||
             !v9x_d3d_rs690_finite(words[6]) ||
             !v9x_d3d_rs690_finite(words[7]))) {
            return 0;
        }
    }
    return 1;
}

/* A run of good triangles: VF_CNTL, then each vertex's eight dwords, each
 * 16 behind a wait for room. */
static int v9x_d3d_rs690_write_run(const V9X_R3D_VERTEX *vertices,
                                   DWORD triangles)
{
    const DWORD *words = (const DWORD *)vertices;
    DWORD dwords = triangles * 3ul * V9X_RS690_TL_VERTEX_DWORDS;
    DWORD index;

    if (!v9x_d3d_rs690_room(1ul)) {
        return 0;
    }
    v9x_d3d_rs690_write(V9X_RS690_VAP_VF_CNTL,
                        v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES,
                                             triangles * 3ul));
    for (index = 0ul; index < dwords; ++index) {
        if (index % V9X_D3D_RS690_FIFO_BATCH == 0ul &&
            !v9x_d3d_rs690_room(V9X_D3D_RS690_FIFO_BATCH)) {
            return 0;
        }
        v9x_d3d_rs690_write(V9X_RS690_SE_PORT_DATA0, words[index]);
    }
    return 1;
}

/*
 * The idle wait's cost, carried in diagnostics fields this engine leaves
 * unused (V9XTRACE names them for the Mach64): M64Degenerate the cycles
 * the wait before each draw took, M64PolicyLast the cycles of the whole
 * draws, both summed in units of 256; M64DepthDraws the draws whose wait
 * took more than an idle engine's few status reads. The time-stamp
 * counter's low half, as glide_core.c reads it.
 */
#define V9X_D3D_RS690_CYCLE_SHIFT 8
#define V9X_D3D_RS690_IDLE_CYCLES 4096ul

static DWORD v9x_d3d_rs690_cycles(void);
#pragma aux v9x_d3d_rs690_cycles = 0x0f 0x31 value [eax] modify exact [eax edx];

/*
 * A run of clipped triangles: the state, then runs of consecutive good
 * triangles, then the finish. A triangle that fails the vertex check is
 * dropped alone, as the core's clipper would refuse it.
 */
static int v9x_d3d_rs690_draw(const V9X_R3D_DRAW *draw,
                              const V9X_R3D_VERTEX *vertices,
                              DWORD triangle_count)
{
    struct v9x_rs690_3d_list finish;
    v9x_u32 reason;
    DWORD at = 0ul;
    DWORD started;
    DWORD waited;
    int textured;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (!v9x_d3d_rs690_ready()) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.m64_unrenderable;
        }
        return 0;
    }
    /* The engine validated - its 3D init written for this mode - and
     * idle on the CPU's poll, and the frame buffer's place, before the
     * state that addresses it. The idle wait is what keeps a draw off a
     * 2D fill still running (T15); the WAIT_UNTIL below does not. */
    started = v9x_d3d_rs690_cycles();
    if (!v9x_rs690_3d_begin(&v9x_d3d_rs690_fb_base,
                            &v9x_d3d_rs690_desktop)) {
        ++v9x_hal->d3d_diagnostics.m64_unrenderable;
        return 0;
    }
    waited = v9x_d3d_rs690_cycles() - started;
    reason = v9x_d3d_rs690_map(draw);
    if (reason != V9X_D3D_RS690_REFUSE_NONE) {
        ++v9x_hal->d3d_diagnostics.m64_refused;
        v9x_d3d_rs690_count_refusal(draw, reason);
        return 0;
    }
    textured = draw->texture.object != 0;
    ++v9x_hal->d3d_diagnostics.m64_draws;
    v9x_hal->d3d_diagnostics.m64_triangles += triangle_count;
    if (textured) {
        ++v9x_hal->d3d_diagnostics.m64_texture_draws;
    }

    /* The target, Z buffer and textures may hold CPU writes still in the
     * write-combining buffers. RADEON_SWITCH_TO_3D's wait stays as the
     * reference writes it, though with the CP off it was measured not to
     * hold the 3D engine back (v9x_rs690_3d_begin's comment). */
    v9x_wc_drain();
    if (!v9x_d3d_rs690_room(1ul)) {
        return 0;
    }
    v9x_d3d_rs690_write(V9X_RS690_WAIT_UNTIL, V9X_RS690_WAIT_2D_IDLECLEAN);
    if (!v9x_d3d_rs690_write_list(&v9x_d3d_rs690_list)) {
        return 0;
    }
    while (at < triangle_count) {
        DWORD run = 0ul;

        while (at + run < triangle_count &&
               run < V9X_D3D_RS690_RUN_TRIANGLES &&
               v9x_d3d_rs690_triangle_ok(vertices + (at + run) * 3ul,
                                         textured)) {
            ++run;
        }
        if (run == 0ul) {
            ++at;
            continue;
        }
        if (!v9x_d3d_rs690_write_run(vertices + at * 3ul, run)) {
            return 0;
        }
        at += run;
    }
    (void)v9x_rs690_build_3d_finish(&finish);
    if (!v9x_d3d_rs690_write_list(&finish)) {
        return 0;
    }
    v9x_present_note_submission();

    /* What the idle wait costs: cycles waiting and cycles in the whole
     * draw, each summed in units of 256 (V9X_D3D_RS690_CYCLE_SHIFT), and
     * the draws whose wait found the engine still busy. */
    v9x_hal->d3d_diagnostics.m64_degenerate +=
        waited >> V9X_D3D_RS690_CYCLE_SHIFT;
    v9x_hal->d3d_diagnostics.m64_policy_last +=
        (v9x_d3d_rs690_cycles() - started) >> V9X_D3D_RS690_CYCLE_SHIFT;
    if (waited > V9X_D3D_RS690_IDLE_CYCLES) {
        ++v9x_hal->d3d_diagnostics.m64_depth_draws;
    }
    return 1;
}

/* One edge the sampler takes: a power of two in range. */
static int v9x_d3d_rs690_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_rs690_limits.texture_size_min &&
           edge <= v9x_d3d_rs690_limits.texture_size_max;
}

static DWORD v9x_d3d_rs690_align(DWORD bytes)
{
    return (bytes + V9X_D3D_RS690_PITCH_ALIGN - 1ul) &
           ~(V9X_D3D_RS690_PITCH_ALIGN - 1ul);
}

/*
 * Placement. A video-memory texture of a format and size this engine
 * samples gets a block 32-byte aligned, its pitch rounded to 64 bytes
 * (TX_OFFSET's rule and the rows the chip steps); a mip chain gets one
 * block with every level where the sampler finds it (below). A Z16 buffer
 * gets 32-byte alignment and a 64-byte pitch, 32
 * pixels, which ZB_DEPTHPITCH needs here and DirectDraw's 8-byte heap
 * alignment does not give.
 */
static DWORD v9x_d3d_rs690_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_D3D_RS690_TEXTURE_LEVELS];
    v9x_u32 rows[V9X_D3D_RS690_TEXTURE_LEVELS];
    v9x_u32 offsets[V9X_D3D_RS690_TEXTURE_LEVELS];
    DWORD bytes = 0ul;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_RS690_TEXTURE_LEVELS ||
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

    if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (data->dwSCnt != 1ul ||
            v9x_d3d_depth_bytes_per_pixel(surface) != 2ul || width == 0ul ||
            height == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = v9x_d3d_rs690_align(width * 2ul);
        rows[0] = height;
    } else {
        DWORD base_width = width;
        DWORD base_height = height;

        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            !v9x_d3d_rs690_texture_edge(width) ||
            !v9x_d3d_rs690_texture_edge(height) ||
            v9x_d3d_rs690_format_of(surface, &bytes) == 0ul) {
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
            pitches[level] = v9x_rs690_mip_pitch(base_width, bytes, level);
            rows[level] = height;
            offsets[level] = v9x_rs690_mip_offset(base_width, base_height,
                                                  bytes, level);
        }

        /* A mip chain in one block, each level where the sampler looks for
         * it: rows rounded to 64 bytes, each level straight after the last
         * (measured, docs\decisions\2026-11-06-rs690-mipmaps.md). */
        if (data->dwSCnt > 1ul) {
            DWORD base = 0ul;

            if (v9x_d3d_place_chain(data, V9X_D3D_RS690_SURFACE_ALIGN,
                                    v9x_rs690_mip_offset(base_width,
                                                         base_height, bytes,
                                                         data->dwSCnt),
                                    offsets, pitches, &base) != 0ul) {
                return V9X_DDHAL_DRIVER_NOTHANDLED;
            }
            ++v9x_hal->d3d_diagnostics.texture_placed;
            data->ddRVal = V9X_DD_OK;
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    if (v9x_d3d_place_each(data, V9X_D3D_RS690_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_rs690_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_RS690_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_rs690 = {
    &v9x_d3d_rs690_limits,
    v9x_d3d_rs690_texture_format,
    v9x_d3d_rs690_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_rs690_ready,
    v9x_d3d_rs690_create_surface,
    v9x_d3d_rs690_destroy_surface,
    v9x_d3d_rs690_draw,
    v9x_d3d_rs690_accepts
};
