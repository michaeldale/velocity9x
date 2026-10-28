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

/* 16 and 32 bpp targets, Z16 1D-tiled, textures to 2048; the core clips
 * every triangle to the target (the guard band is not measured); pixel
 * centres at whole coordinates, which the PA is told (PIX_CENTER clear),
 * so the core moves nothing. */
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

/* RGB565 and ARGB8888: the T# data format, or UNKNOWN. */
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
 * What d3d_vi_map.c accepts: the Z test with every function, flat and
 * Gouraud colour and specular, textures in RGB565 and ARGB8888 with point
 * and bilinear filters, wrap, mirror and clamp, DECAL, COPY, MODULATE and
 * MODULATEALPHA. No blending, alpha test, fog, colour key or mip filter
 * yet (the plan's steps 2 and 3): those draws are refused and counted, and
 * the caps do not offer them.
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
                        V9X_D3DPRASTERCAPS_SUBPIXEL;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwSrcBlendCaps = V9X_D3DPBLENDCAPS_ONE;
    tri->dwDestBlendCaps = V9X_D3DPBLENDCAPS_ZERO;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
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

    /* RGB565, ARGB8888. */
    for (index = 0ul; index < 2ul; ++index) {
        V9X_DDPIXELFORMAT *pf = &shared->texture_formats[index].ddpfPixelFormat;

        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
        pf->dwSize = sizeof(V9X_DDPIXELFORMAT);
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0xff000000ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 2ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

static int v9x_d3d_vi_ready(void)
{
    return v9x_vi_cp_ready();
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
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_key = 1ul;
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
                              struct v9x_vi_tl_draw *out, v9x_u32 *ps)
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
    if (draw->texture.object != 0) {
        v9x_d3d_vi_resolve_texture(&draw->texture, &texture);
    }
    return v9x_d3d_vi_map_draw(draw, &texture, high, heap_low,
                               v9x_d3d_vi_depth_bytes(draw), out, ps);
}

static int v9x_d3d_vi_accepts(const V9X_R3D_DRAW *draw)
{
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    reason = v9x_d3d_vi_map(draw, &out, &ps);
    if (reason != V9X_D3D_VI_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_VI_REFUSE_NONE;
}

/*
 * A run of triangles, in batches of at most V9X_D3D_VI_CHUNK_TRIANGLES:
 * each batch's vertices copied into the CP's arena as they are, then the
 * TL draw's state, the draw and its fence.
 */
static int v9x_d3d_vi_draw(const V9X_R3D_DRAW *draw,
                           const V9X_R3D_VERTEX *vertices,
                           DWORD triangle_count)
{
    struct v9x_vi_tl_draw state;
    struct v9x_vi_pm4 stream;
    volatile DWORD *linear;
    const DWORD *words;
    DWORD gpu_low;
    DWORD chunk;
    DWORD dwords;
    DWORD index;
    DWORD started;
    v9x_u32 ps;
    v9x_u32 reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        !v9x_d3d_vi_ready()) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    reason = v9x_d3d_vi_map(draw, &state, &ps);
    if (reason != V9X_D3D_VI_REFUSE_NONE) {
        v9x_d3d_vi_count_refusal(draw, reason);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    state.vs_low = v9x_vi_cp_shader_low(V9X_VI_CP_SLOT_VS);
    state.ps_low = v9x_vi_cp_shader_low(v9x_d3d_vi_ps_slot[ps]);
    state.shader_high = state.target_high;
    state.vertex_high = state.target_high;
    state.fence_high = state.target_high;

    while (triangle_count != 0ul) {
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

/*
 * Placement, as Cedar's (d3d_eg.c), on GFX8's rules (d3d_vi_map.h). An
 * offscreen render target alone gets its rows whole 64 bytes and whole
 * 8-pixel tiles, 256-aligned. A Z16 buffer gets its pitch whole 8-pixel
 * tiles and its rows padded to 8, the DB's 1D tile, 256-aligned. A
 * video-memory texture of a format this engine samples gets its rows whole
 * 64 bytes, which a linear-aligned T# needs. Mip chains are not placed
 * here yet: only level 0 is sampled.
 */
static DWORD v9x_d3d_vi_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 pitches[1];
    v9x_u32 rows[1];
    DWORD bytes = 0ul;
    DWORD has_alpha = 0ul;
    DWORD width;
    DWORD height;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt != 1ul ||
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
        if (v9x_d3d_depth_bytes_per_pixel(surface) != 2ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[0] = (width * 2ul + V9X_D3D_VI_DEPTH_PITCH_UNIT - 1ul) &
                     ~(V9X_D3D_VI_DEPTH_PITCH_UNIT - 1ul);
        rows[0] = (height + V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul) &
                  ~(V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul);
    } else {
        if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
            (surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul ||
            width > v9x_d3d_vi_limits.texture_size_max ||
            height > v9x_d3d_vi_limits.texture_size_max ||
            v9x_d3d_vi_format_of(surface, &bytes, &has_alpha) ==
                V9X_D3D_VI_TEXTURE_UNKNOWN) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
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

static void v9x_d3d_vi_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
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
    v9x_d3d_vi_accepts
};
