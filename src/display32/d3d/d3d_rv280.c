/*
 * ATI Radeon 9250 / 9200SE (RV280, R200 3D) Direct3D engine, by register
 * writes.
 *
 * Phase 5 of docs\plans\ati-rv280-radeon-9250.md. The register values come
 * from src\chipsets\ati\rv280\rv280_3d.c and the translation from the
 * neutral draw from d3d_rv280_map.c, both host-tested; the V9XR23D probes
 * drew the same values right on M-HNBJNV at 16 and 32 bpp: flat and Gouraud
 * TL vertices, textures nearest and bilinear, wrap and clamp, decal and
 * modulate, Z16, blending, the alpha test, perspective, fog and specular
 * (docs\decisions\2026-11-15-rv280-3d-draw-probe.md, -texture-probe.md,
 * -z-blend-probe.md, -cull-rhw-fog-probe.md). This file resolves DirectDraw
 * surfaces, places textures and Z buffers, and writes. d3d_rs690.c is the
 * model.
 *
 * There is no CP: every state and vertex dword is a register write over the
 * register BAR, the vertices to SE_PORT_DATA0 behind SE_VF_CNTL, each run of
 * 16 behind a wait for that many free FIFO entries. eng_rv280.c owns the
 * engine: its validation writes the 3D init once per mode, and its idle wait
 * (Lock, flips, blits) finds the drawing done and flushes the destination
 * cache, because every batch ends in WAIT_UNTIL 3D idle-clean.
 *
 * The vertices go as they are: V9X_R3D_VERTEX is D3DTLVERTEX, the builder's
 * X, Y, Z, 1/W, colour, specular, S, T; an untextured draw sends the first
 * six dwords. The core clips every triangle to the target (clip_in_core),
 * culls, and moves Direct3D's pixel centres onto this engine's half-pixel
 * ones (Gouraud and nearest sampling were exact at the pixel centre in the
 * probes, as on the RS690). A triangle with a coordinate that is not finite,
 * or textured with an RHW at or below zero, is left out.
 *
 * Emission order is the refusal contract: the mapping and the state are
 * built first, and only then is the first register written, so a refused
 * draw writes nothing.
 */
#include "d3d_internal.h"
#include "d3d_rv280_map.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rv280_3d.h"

/* The register window the mini-VDD maps (eng_rv280.c's). */
#define V9X_D3D_RV280_MMIO_BYTES 0x00010000ul

/* Textures and Z placed 32-byte aligned (PP_TXOFFSET's rule, and a 16-byte
 * RB3D_DEPTHOFFSET within it). A texture's rows are its width rounded up to
 * 32 bytes, as the builder requires (V9X_RV280_TEX_ROW_ALIGN); a Z16
 * buffer's to 64 bytes, 32 pixels, Mesa's alignment for R100/R200 depth,
 * which RB3D_DEPTHPITCH's multiple of 8 pixels allows. */
#define V9X_D3D_RV280_SURFACE_ALIGN 32ul
#define V9X_D3D_RV280_DEPTH_PITCH_ALIGN 64ul

/* Levels a placed chain may have: 2048 halves to 1 in eleven. Only level
 * 0 is sampled; the others are placed at the same row rule. */
#define V9X_D3D_RV280_TEXTURE_LEVELS 12ul

/* Triangles an SE_VF_CNTL carries here: the four (twelve vertices) the
 * probes drew in one walk. The count field holds far more, but no longer
 * walk was measured, and the extra SE_VF_CNTL write is one dword in
 * twenty-five. */
#define V9X_D3D_RV280_RUN_TRIANGLES (V9X_RV280_3D_MAX_VERTICES / 3u)

/* Writes between FIFO waits, as the probes wrote them. */
#define V9X_D3D_RV280_FIFO_BATCH 16ul

/* Bounded wait for FIFO room: the probes' waits took 3 to 8 reads. */
#define V9X_D3D_RV280_FIFO_SPINS 0x00200000ul

/* WAIT_UNTIL's WAIT_2D_IDLECLEAN (radeon_reg.h RADEON_WAIT_2D_IDLECLEAN),
 * RADEON_SWITCH_TO_3D's wait. */
#define V9X_D3D_RV280_WAIT_2D_IDLECLEAN 0x00010000ul

/* D3DTLVERTEX dwords sent untextured (X, Y, Z, 1/W, colour, specular) and
 * textured (and S, T). */
#define V9X_D3D_RV280_DWORDS_PLAIN    6ul
#define V9X_D3D_RV280_DWORDS_TEXTURED 8ul

static struct v9x_rv280_3d_list v9x_d3d_rv280_list;
static DWORD v9x_d3d_rv280_fb_base;
static DWORD v9x_d3d_rv280_desktop;

/* 16 and 32 bpp targets; Z16, tiled; power-of-two textures to 2048; the
 * core clips and moves Direct3D's vertices half a pixel onto this
 * rasterizer's centres. */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_rv280_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max: COLORPITCH's 1FF8h pixels */
    16ul,           /* target_pitch_align: 8 pixels at 16 bpp */
    2047ul,         /* target_dimension_max: RE_WIDTH_HEIGHT, X's 2047 */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_RV280_SURFACE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core: no guard band measured */
    1ul,            /* depth_pitch_own: RB3D_DEPTHPITCH takes the pitch */
    0ul,            /* depth_fill_shift: Z16 FFFFh is far */
    1ul,            /* texture_units */
    1ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: R100/R200 depth is always tiled */
    32752ul,        /* target_pitch_max_xrgb8888 */
    0ul,            /* depth32_bits: Z16 only */
    0ul,            /* centre_in_engine: the core moves, as it clips */
    1ul,            /* texture_stages */
    0ul,            /* guard_band_ddi6 */
    0ul,            /* dxt_textures */
    0ul,            /* modulate2x */
    0ul,            /* bump_luminance */
    /* render_interface_off: the interface's explicit draws need a scissor
     * and write mask not built here, and the tiled Z rules out the software
     * fallback, so OpenGL and Glide keep their own CPU path (V9XGLP drew
     * nothing through this engine, M-HNBJNV boot 21). */
    1ul
};

static DWORD v9x_d3d_rv280_read(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_d3d_rv280_write(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

/* Non-zero once RBBM_STATUS shows at least `entries` free FIFO entries. */
static int v9x_d3d_rv280_room(DWORD entries)
{
    DWORD spins = V9X_D3D_RV280_FIFO_SPINS;

    while (spins-- != 0ul) {
        if ((v9x_d3d_rv280_read(V9X_RS690_RBBM_STATUS) &
             V9X_RS690_RBBM_FIFO_MASK) >= entries) {
            return 1;
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    return 0;
}

/* A list's writes, each run of 16 behind a wait for room. */
static int v9x_d3d_rv280_write_list(const struct v9x_rv280_3d_list *list)
{
    DWORD index;

    for (index = 0ul; index < list->count; ++index) {
        if (index % V9X_D3D_RV280_FIFO_BATCH == 0ul &&
            !v9x_d3d_rv280_room(V9X_D3D_RV280_FIFO_BATCH)) {
            return 0;
        }
        v9x_d3d_rv280_write(list->offsets[index], list->values[index]);
    }
    return 1;
}

/* The texture formats this engine samples, in the order describe_caps
 * publishes them: RGB565 and ARGB8888 measured by the probes, ARGB1555,
 * ARGB4444 and XRGB8888 from r200_reg.h's codes. */
#define V9X_D3D_RV280_FORMATS 5ul

typedef struct v9x_d3d_rv280_format {
    DWORD bits;
    DWORD alpha_mask;           /* 0: no DDPF_ALPHAPIXELS */
    DWORD red_mask;
    DWORD green_mask;
    DWORD blue_mask;
    DWORD format;               /* V9X_RV280_TEXFMT_* */
} V9X_D3D_RV280_FORMAT;

static const V9X_D3D_RV280_FORMAT
    v9x_d3d_rv280_formats[V9X_D3D_RV280_FORMATS] = {
    { 16ul, 0x00000000ul, 0x0000f800ul, 0x000007e0ul, 0x0000001ful,
      V9X_RV280_TEXFMT_DEFAULT },
    { 16ul, 0x00008000ul, 0x00007c00ul, 0x000003e0ul, 0x0000001ful,
      V9X_RV280_TEXFMT_ARGB1555 },
    { 16ul, 0x0000f000ul, 0x00000f00ul, 0x000000f0ul, 0x0000000ful,
      V9X_RV280_TEXFMT_ARGB4444 },
    { 32ul, 0xff000000ul, 0x00ff0000ul, 0x0000ff00ul, 0x000000fful,
      V9X_RV280_TEXFMT_DEFAULT },
    { 32ul, 0x00000000ul, 0x00ff0000ul, 0x0000ff00ul, 0x000000fful,
      V9X_RV280_TEXFMT_XRGB8888 }
};

/* The surface's entry in v9x_d3d_rv280_formats, or 0 for a format this
 * engine does not sample. */
static const V9X_D3D_RV280_FORMAT *v9x_d3d_rv280_format_of(
    const V9X_DD_SURFACE_LCL *surface)
{
    const V9X_DDPIXELFORMAT *format;
    DWORD index;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return 0;
    }
    for (index = 0ul; index < V9X_D3D_RV280_FORMATS; ++index) {
        const V9X_D3D_RV280_FORMAT *entry = &v9x_d3d_rv280_formats[index];
        int alpha = (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul;

        if (format->dwRGBBitCount == entry->bits &&
            alpha == (entry->alpha_mask != 0ul) &&
            (!alpha || format->dwRGBAlphaBitMask == entry->alpha_mask) &&
            format->dwRBitMask == entry->red_mask &&
            format->dwGBitMask == entry->green_mask &&
            format->dwBBitMask == entry->blue_mask) {
            return entry;
        }
    }
    return 0;
}

/* The engine's format code: the row's index in v9x_d3d_rv280_formats. */
static int v9x_d3d_rv280_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                        DWORD *format_out)
{
    const V9X_D3D_RV280_FORMAT *entry;

    if (format_out == 0) {
        return 0;
    }
    entry = v9x_d3d_rv280_format_of(surface);
    if (entry == 0) {
        *format_out = V9X_D3D_RV280_TEXTURE_UNKNOWN;
        return 0;
    }
    *format_out = (DWORD)(entry - v9x_d3d_rv280_formats);
    return 1;
}

/*
 * What d3d_rv280_map.c accepts and the probes measured: Z and alpha tests
 * with every function (LESS and GREATER measured), the blend factors
 * without destination alpha (SRCALPHA/INVSRCALPHA measured), flat and
 * Gouraud, textures to 2048 in five formats (RGB565 and ARGB8888
 * measured), nearest and bilinear, wrap and clamp, perspective-correct,
 * decal, modulate and modulate-alpha; specular, vertex fog from the
 * specular alpha, and FOGTABLE emulated per vertex in the core (d3d_fog.h).
 * No colour key: TRANSPARENCY is not claimed.
 */
static void v9x_d3d_rv280_describe_caps(V9X_DD_SHARED *shared)
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
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    /* No mip filters: only level 0 is sampled, so claiming them failed
     * V9XD32's T21 (every quad level 0, M-HNBJNV boot 20). A mip filter
     * that arrives anyway is drawn as its level filter. */
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_rv280_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_rv280_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_rv280_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_rv280_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    for (index = 0ul; index < V9X_D3D_RV280_FORMATS; ++index) {
        const V9X_D3D_RV280_FORMAT *entry = &v9x_d3d_rv280_formats[index];
        V9X_DDPIXELFORMAT *pf = &shared->texture_formats[index].ddpfPixelFormat;

        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
        pf->dwSize = sizeof(V9X_DDPIXELFORMAT);
        pf->dwFlags = V9X_DDPF_RGB |
                      (entry->alpha_mask != 0ul ? V9X_DDPF_ALPHAPIXELS : 0ul);
        pf->dwRGBBitCount = entry->bits;
        pf->dwRBitMask = entry->red_mask;
        pf->dwGBitMask = entry->green_mask;
        pf->dwBBitMask = entry->blue_mask;
        pf->dwRGBAlphaBitMask = entry->alpha_mask;
    }
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = V9X_D3D_RV280_FORMATS;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* eng_rv280.c's conditions, CAP_D3D, and the window mapped. */
static int v9x_d3d_rv280_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RV280 &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_RV280_MMIO_BYTES;
}

/* The bound texture's level 0: format, shape, pitch and offset. */
static void v9x_d3d_rv280_resolve_texture(const V9X_R3D_TEXTURE *sampler,
                                          V9X_D3D_RV280_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *surface;
    const V9X_D3D_RV280_FORMAT *entry;
    DWORD offset;

    texture->bytes_per_pixel = V9X_D3D_RV280_TEXTURE_UNKNOWN;
    texture->format = V9X_RV280_TEXFMT_DEFAULT;
    texture->has_alpha = 0;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->pitch_bytes = 0ul;
    texture->offset = 0ul;
    texture->has_key = 0;

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
    entry = v9x_d3d_rv280_format_of(surface);
    if (entry == 0) {
        return;
    }
    texture->bytes_per_pixel = entry->bits / 8ul;
    texture->format = entry->format;
    texture->has_alpha = entry->alpha_mask != 0ul;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->offset = offset;
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1;
    }

    /* Where the texture is, its pitch, and its first 16 bits at the corner
     * and the centre texel, read from the memory the engine samples, as
     * the RS690's records them. */
    {
        const volatile BYTE *base =
            (const volatile BYTE *)(v9x_hal->fb.linear_base + offset);
        const volatile WORD *corner = (const volatile WORD *)base;
        const volatile WORD *centre = (const volatile WORD *)
            (base + (texture->height / 2ul) * texture->pitch_bytes +
             (texture->width / 2ul) * texture->bytes_per_pixel);

        v9x_hal->d3d_diagnostics.texture_last_offset = offset;
        v9x_hal->d3d_diagnostics.texture_last_size = texture->width;
        v9x_hal->d3d_diagnostics.texture_last_caps = surface->ddsCaps;
        v9x_hal->d3d_diagnostics.texture_last_texels =
            ((DWORD)*centre << 16) | (DWORD)*corner;
        v9x_hal->d3d_diagnostics.m64_fog_decal =
            (texture->bytes_per_pixel << 16) |
            (texture->pitch_bytes & 0xfffful);
    }
}

/* Z16 when a depth surface is bound: its bytes per pixel, 0 for none. */
static DWORD v9x_d3d_rv280_depth_bytes(const V9X_R3D_DRAW *draw)
{
    const V9X_DD_SURFACE_LCL *depth =
        (const V9X_DD_SURFACE_LCL *)draw->depth.object;

    if (depth == 0) {
        return 0ul;
    }
    return v9x_d3d_depth_bytes_per_pixel(depth);
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the RS690 counts its own. */
static void v9x_d3d_rv280_count_refusal(const V9X_R3D_DRAW *draw,
                                        v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_RV280_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_RV280_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/* The mapping, then the state into v9x_d3d_rv280_list; on success the
 * draw's state in *state for the vertex layout and the finish. */
static v9x_u32 v9x_d3d_rv280_map(const V9X_R3D_DRAW *draw,
                                 V9X_D3D_RV280_STATE *state)
{
    V9X_D3D_RV280_TEXTURE resolved;
    v9x_u32 reason;
    v9x_status status;

    v9x_d3d_rv280_resolve_texture(&draw->texture, &resolved);
    reason = v9x_d3d_rv280_map_draw(draw, &resolved, v9x_d3d_rv280_fb_base,
                                    v9x_d3d_rv280_desktop,
                                    v9x_hal->fb.vram_bytes,
                                    v9x_d3d_rv280_depth_bytes(draw), state);
    if (reason != V9X_D3D_RV280_REFUSE_NONE) {
        return reason;
    }
    if (state->textured != 0ul) {
        status = v9x_rv280_build_3d_textured_target(
            &state->target, (int)state->gouraud, &state->texture,
            state->combine, &v9x_d3d_rv280_list);
    } else {
        status = v9x_rv280_build_3d_target(&state->target,
                                           (int)state->gouraud,
                                           &v9x_d3d_rv280_list);
    }
    return status == V9X_STATUS_OK ? V9X_D3D_RV280_REFUSE_NONE
                                   : V9X_D3D_RV280_REFUSE_STATE;
}

static int v9x_d3d_rv280_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_RV280_STATE state;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    return v9x_d3d_rv280_map(draw, &state) == V9X_D3D_RV280_REFUSE_NONE;
}

/* The vertex checks, on the floats' bits, as the RS690 makes them. */
#define V9X_D3D_RV280_SIGN_BIT      0x80000000ul
#define V9X_D3D_RV280_EXPONENT_MASK 0x7f800000ul

static int v9x_d3d_rv280_finite(DWORD bits)
{
    return (bits & V9X_D3D_RV280_EXPONENT_MASK) !=
           V9X_D3D_RV280_EXPONENT_MASK;
}

/* A triangle the engine takes as it is: every coordinate finite, and rhw
 * positive with finite texture coordinates when textured. */
static int v9x_d3d_rv280_triangle_ok(const V9X_R3D_VERTEX *triangle,
                                     int textured)
{
    DWORD corner;

    for (corner = 0ul; corner < 3ul; ++corner) {
        const DWORD *words = (const DWORD *)&triangle[corner];
        DWORD rhw = words[3];

        if (!v9x_d3d_rv280_finite(words[0]) ||
            !v9x_d3d_rv280_finite(words[1]) ||
            !v9x_d3d_rv280_finite(words[2]) || !v9x_d3d_rv280_finite(rhw)) {
            return 0;
        }
        if (textured &&
            ((rhw & V9X_D3D_RV280_SIGN_BIT) != 0ul || rhw == 0ul ||
             !v9x_d3d_rv280_finite(words[6]) ||
             !v9x_d3d_rv280_finite(words[7]))) {
            return 0;
        }
    }
    return 1;
}

/* A run of good triangles: SE_VF_CNTL (a triangle list, the vertex data
 * walked, D3DCOLOR order), then each vertex's first `dwords` dwords, each
 * 16 behind a wait for room. */
static int v9x_d3d_rv280_write_run(const V9X_R3D_VERTEX *vertices,
                                   DWORD triangles, DWORD dwords)
{
    DWORD vertex;
    DWORD index;
    DWORD written = 0ul;

    if (!v9x_d3d_rv280_room(1ul)) {
        return 0;
    }
    v9x_d3d_rv280_write(V9X_RV280_SE_VF_CNTL,
                        V9X_RV280_VF_TRIANGLE_LIST | V9X_RV280_VF_WALK_DATA |
                        ((triangles * 3ul) << V9X_RV280_VF_NUM_SHIFT));
    for (vertex = 0ul; vertex < triangles * 3ul; ++vertex) {
        const DWORD *words = (const DWORD *)&vertices[vertex];

        for (index = 0ul; index < dwords; ++index) {
            if (written % V9X_D3D_RV280_FIFO_BATCH == 0ul &&
                !v9x_d3d_rv280_room(V9X_D3D_RV280_FIFO_BATCH)) {
                return 0;
            }
            v9x_d3d_rv280_write(V9X_RV280_SE_PORT_DATA0, words[index]);
            ++written;
        }
    }
    return 1;
}

/*
 * A run of clipped triangles: the state, then runs of consecutive good
 * triangles, then the finish. A triangle that fails the vertex check is
 * dropped alone, as the core's clipper would refuse it.
 */
static int v9x_d3d_rv280_draw(const V9X_R3D_DRAW *draw,
                              const V9X_R3D_VERTEX *vertices,
                              DWORD triangle_count)
{
    V9X_D3D_RV280_STATE state;
    struct v9x_rv280_3d_list finish;
    v9x_u32 reason;
    DWORD at = 0ul;
    DWORD dwords;
    int textured;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (!v9x_d3d_rv280_ready()) {
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.m64_unrenderable;
        }
        return 0;
    }
    /* The engine validated - its 3D init written for this mode - idle
     * after 2D work, and the frame buffer's place, before the state that
     * addresses it. */
    if (!v9x_rv280_3d_start(&v9x_d3d_rv280_fb_base,
                            &v9x_d3d_rv280_desktop)) {
        ++v9x_hal->d3d_diagnostics.m64_unrenderable;
        return 0;
    }
    reason = v9x_d3d_rv280_map(draw, &state);
    if (reason != V9X_D3D_RV280_REFUSE_NONE) {
        ++v9x_hal->d3d_diagnostics.m64_refused;
        v9x_d3d_rv280_count_refusal(draw, reason);
        return 0;
    }
    textured = state.textured != 0ul;
    dwords = textured ? V9X_D3D_RV280_DWORDS_TEXTURED
                      : V9X_D3D_RV280_DWORDS_PLAIN;
    ++v9x_hal->d3d_diagnostics.m64_draws;
    v9x_hal->d3d_diagnostics.m64_triangles += triangle_count;
    if (textured) {
        ++v9x_hal->d3d_diagnostics.m64_texture_draws;
    }

    /* The target, Z buffer and textures may hold CPU writes still in the
     * write-combining buffers. RADEON_SWITCH_TO_3D's wait as the reference
     * writes it, though v9x_rv280_3d_start has already waited. */
    v9x_wc_drain();
    if (!v9x_d3d_rv280_room(1ul)) {
        return 0;
    }
    v9x_d3d_rv280_write(V9X_RV280_WAIT_UNTIL,
                        V9X_D3D_RV280_WAIT_2D_IDLECLEAN);
    if (!v9x_d3d_rv280_write_list(&v9x_d3d_rv280_list)) {
        return 0;
    }
    while (at < triangle_count) {
        DWORD run = 0ul;

        while (at + run < triangle_count &&
               run < V9X_D3D_RV280_RUN_TRIANGLES &&
               v9x_d3d_rv280_triangle_ok(vertices + (at + run) * 3ul,
                                         textured)) {
            ++run;
        }
        if (run == 0ul) {
            ++at;
            continue;
        }
        if (!v9x_d3d_rv280_write_run(vertices + at * 3ul, run, dwords)) {
            return 0;
        }
        at += run;
    }
    /* With Z, its cache is flushed too: a 2D fill of the Z buffer
     * (eng_rv280.c) must not be overwritten by lines the 3D engine still
     * holds. */
    (void)v9x_rv280_build_3d_finish(state.raster.z_enable != 0ul, &finish);
    if (!v9x_d3d_rv280_write_list(&finish)) {
        return 0;
    }
    v9x_present_note_submission();
    return 1;
}

/* One edge the sampler takes: a power of two in range. */
static int v9x_d3d_rv280_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_rv280_limits.texture_size_min &&
           edge <= v9x_d3d_rv280_limits.texture_size_max;
}

static DWORD v9x_d3d_rv280_align(DWORD bytes, DWORD align)
{
    return (bytes + align - 1ul) & ~(align - 1ul);
}

/*
 * Placement. A video-memory texture of a format and size this engine
 * samples gets each level in a block of its own, 32-byte aligned, its rows
 * the width rounded up to 32 bytes (PP_TXOFFSET's rule and the pitch the
 * builder requires). A Z16 buffer gets 32-byte alignment, a 64-byte pitch,
 * and its rows rounded up to V9X_RV280_DEPTH_ROW_ALIGN, so the tiled
 * buffer's whole-surface fill (eng_rv280.c) covers every tile.
 */
static DWORD v9x_d3d_rv280_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[V9X_D3D_RV280_TEXTURE_LEVELS];
    v9x_u32 rows[V9X_D3D_RV280_TEXTURE_LEVELS];
    const V9X_D3D_RV280_FORMAT *entry;
    DWORD bytes;
    DWORD width;
    DWORD height;
    DWORD level;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_RV280_TEXTURE_LEVELS ||
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
        pitches[0] = v9x_d3d_rv280_align(width * 2ul,
                                         V9X_D3D_RV280_DEPTH_PITCH_ALIGN);
        rows[0] = v9x_d3d_rv280_align(height, V9X_RV280_DEPTH_ROW_ALIGN);
    } else {
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            !v9x_d3d_rv280_texture_edge(width) ||
            !v9x_d3d_rv280_texture_edge(height)) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        entry = v9x_d3d_rv280_format_of(surface);
        if (entry == 0) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        bytes = entry->bits / 8ul;
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
            pitches[level] = v9x_d3d_rv280_align(width * bytes,
                                                 V9X_RV280_TEX_ROW_ALIGN);
            rows[level] = height;
        }
    }
    if (v9x_d3d_place_each(data, V9X_D3D_RV280_SURFACE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_rv280_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_RV280_SURFACE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_rv280 = {
    &v9x_d3d_rv280_limits,
    v9x_d3d_rv280_texture_format,
    v9x_d3d_rv280_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_rv280_ready,
    v9x_d3d_rv280_create_surface,
    v9x_d3d_rv280_destroy_surface,
    v9x_d3d_rv280_draw,
    v9x_d3d_rv280_accepts
};
